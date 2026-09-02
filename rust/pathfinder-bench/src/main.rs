use bench_common::{
    Backend, BenchmarkDriver, BenchmarkScene, CliOptions, RuntimeMetadata, Workload, locate_image,
    run_benchmark, sha256_file, write_ppm,
};
use image::{ColorType, ImageFormat as OutputImageFormat};
use pathfinder_canvas::{
    Canvas, CanvasFontContext, FillRule, LineCap, LineJoin, Path2D, RectF, Transform2F, Vector2F,
    vec2f,
};
use pathfinder_color::{ColorF, ColorU};
use pathfinder_content::gradient::Gradient;
use pathfinder_content::pattern::{Image as PathfinderImage, Pattern};
use pathfinder_geometry::vector::{Vector2I, vec2i};
use pathfinder_gl::{GLDevice, GLVersion};
use pathfinder_renderer::concurrent::executor::SequentialExecutor;
use pathfinder_renderer::gpu::options::{
    DestFramebuffer, RendererLevel, RendererMode, RendererOptions,
};
use pathfinder_renderer::gpu::renderer::Renderer;
use pathfinder_renderer::options::BuildOptions;
use pathfinder_renderer::scene::Scene;
use pathfinder_resources::embedded::EmbeddedResourceLoader;
use pathfinder_simd::default::F32x2;
use sdl2::event::Event;
use sdl2::keyboard::Keycode;
use sdl2::video::{GLProfile, SwapInterval};
use std::ffi::CStr;
use std::path::Path;
use std::sync::Arc;

const PATHFINDER_VERSION: &str = "0.5.0";

fn main() {
    if let Err(error) = main_result() {
        eprintln!("pathfinder-bench: {error}");
        std::process::exit(1);
    }
}

fn main_result() -> Result<(), String> {
    let opts = CliOptions::parse(Backend::Gl)?;
    if opts.backend != Backend::Gl {
        return Err("Pathfinder supports only --backend=gl".into());
    }
    println!("Pathfinder {PATHFINDER_VERSION} native Rust Canvas benchmark");
    println!("Benchmark: {}", opts.benchmark.as_str());
    println!("Backend:   {}", opts.backend.as_str());
    println!("Scene:     {}", opts.scene_mode.as_str());
    println!("GPU sync:  {}", opts.gpu_sync);

    let mut driver = PathfinderDriver::new(&opts)?;
    run_benchmark(&opts, &mut driver)?;
    Ok(())
}

struct PathfinderDriver {
    renderer: Renderer<GLDevice>,
    _gl_context: sdl2::video::GLContext,
    window: sdl2::video::Window,
    _sdl: sdl2::Sdl,
    event_pump: sdl2::EventPump,
    data: BenchmarkScene,
    image: Option<Pattern>,
    scene: Scene,
    runtime: RuntimeMetadata,
    width: u32,
    height: u32,
}

impl PathfinderDriver {
    fn new(opts: &CliOptions) -> Result<Self, String> {
        let sdl = sdl2::init().map_err(|error| format!("failed to initialize SDL: {error}"))?;
        let video = sdl
            .video()
            .map_err(|error| format!("failed to initialize SDL video: {error}"))?;
        {
            let attributes = video.gl_attr();
            attributes.set_context_profile(GLProfile::Core);
            attributes.set_context_version(3, 3);
            attributes.set_red_size(8);
            attributes.set_green_size(8);
            attributes.set_blue_size(8);
            attributes.set_alpha_size(8);
            attributes.set_depth_size(24);
            attributes.set_stencil_size(8);
            attributes.set_double_buffer(true);
            attributes.set_multisample_buffers(0);
            attributes.set_multisample_samples(0);
            attributes.set_accelerated_visual(true);
        }
        let mut window = video
            .window(
                &format!("{} Pathfinder", opts.benchmark.as_str()),
                opts.width,
                opts.height,
            )
            .position_centered()
            .allow_highdpi()
            .opengl()
            .build()
            .map_err(|error| format!("failed to create SDL OpenGL window: {error}"))?;
        let gl_context = window
            .gl_create_context()
            .map_err(|error| format!("failed to create OpenGL context: {error}"))?;
        window
            .gl_make_current(&gl_context)
            .map_err(|error| format!("failed to make OpenGL context current: {error}"))?;
        let (width, height) = adjust_gl_drawable_size(&mut window, opts.width, opts.height)?;
        let gl_contract = validate_gl_contract(&video)?;
        video
            .gl_set_swap_interval(if opts.vsync {
                SwapInterval::VSync
            } else {
                SwapInterval::Immediate
            })
            .map_err(|error| format!("failed to set requested swap interval: {error}"))?;
        let actual_swap_interval = video.gl_get_swap_interval();
        if !opts.vsync && actual_swap_interval != SwapInterval::Immediate {
            return Err(format!(
                "VSync disable request was not honored: {actual_swap_interval:?}"
            ));
        }

        gl::load_with(|name| video.gl_get_proc_address(name) as *const _);
        unsafe {
            gl::Disable(gl::MULTISAMPLE);
            gl::Disable(gl::FRAMEBUFFER_SRGB);
        }

        let framebuffer = DestFramebuffer::full_window(Vector2I::new(width as i32, height as i32));
        let renderer = Renderer::new(
            GLDevice::new(GLVersion::GL3, 0),
            &EmbeddedResourceLoader::new(),
            RendererMode {
                level: RendererLevel::D3D9,
            },
            RendererOptions {
                dest: framebuffer,
                background_color: Some(ColorF::black()),
                show_debug_ui: false,
            },
        );
        let event_pump = sdl
            .event_pump()
            .map_err(|error| format!("failed to create SDL event pump: {error}"))?;
        let (image, asset_hash) = load_image(opts)?;
        let gl_version = gl_string(gl::VERSION);
        let runtime = RuntimeMetadata {
            engine: "pathfinder".into(),
            engine_version: PATHFINDER_VERSION.into(),
            engine_revision: "unknown".into(),
            scene_model: "immediate".into(),
            graphics_api: format!(
                "OpenGL {} (core {}.{}) / Pathfinder GL3 (D3D9 level)",
                gl_version, gl_contract.major, gl_contract.minor,
            ),
            gpu_device: gl_string(gl::RENDERER),
            gpu_vendor: gl_string(gl::VENDOR),
            gpu_driver: gl_version,
            gpu_completion: if opts.gpu_sync {
                "glFinish".into()
            } else {
                "none".into()
            },
            actual_width: width,
            actual_height: height,
            pixel_format: "RGBA8".into(),
            vsync_actual: actual_swap_interval != SwapInterval::Immediate,
            present_mode: format!("sdl-gl-{actual_swap_interval:?}").to_ascii_lowercase(),
            asset_hash,
        };

        Ok(Self {
            renderer,
            _gl_context: gl_context,
            window,
            _sdl: sdl,
            event_pump,
            data: BenchmarkScene::new_with_size(opts, width, height),
            image,
            scene: Scene::new(),
            runtime,
            width,
            height,
        })
    }

    fn build_canvas_scene(&self) -> Scene {
        let canvas = Canvas::new(vec2f(self.width as f32, self.height as f32));
        let mut context = canvas.get_context_2d(CanvasFontContext::from_system_source());
        context.set_line_cap(LineCap::Butt);
        context.set_line_join(LineJoin::Miter);
        context.set_miter_limit(4.0);
        let workload = self.data.workload;

        if workload.uses_circles() {
            for (circle, transform) in self.data.circles.iter().zip(&self.data.transforms) {
                context.set_transform(&pathfinder_transform(
                    transform.affine(circle.center_x, circle.center_y),
                ));
                let mut path = Path2D::new();
                path.ellipse(
                    vec2f(circle.center_x, circle.center_y),
                    vec2f(circle.radius, circle.radius),
                    0.0,
                    0.0,
                    std::f32::consts::TAU,
                );
                let primary = pathfinder_color(circle.rgba);
                match workload {
                    Workload::Circle => context.set_fill_style(primary),
                    Workload::RadialGradient => {
                        let mut gradient = Gradient::radial(
                            vec2f(circle.center_x, circle.center_y),
                            F32x2::new(0.0, circle.radius),
                        );
                        gradient.add_color_stop(primary, 0.0);
                        gradient.add_color_stop(inverse_color(circle.rgba), 1.0);
                        context.set_fill_style(gradient);
                    }
                    _ => unreachable!("circle workload routing is exhaustive"),
                }
                context.fill_path(path, FillRule::Winding);
            }
            return context.into_canvas().into_scene();
        }

        if workload == Workload::Image {
            let pattern = self
                .image
                .as_ref()
                .expect("image workload loaded its asset");
            for (rect, transform) in self.data.rects.iter().zip(&self.data.transforms) {
                context.set_transform(&pathfinder_image_transform(rect, transform, pattern));
                context.draw_image(pattern.clone(), Vector2F::zero());
            }
            return context.into_canvas().into_scene();
        }

        for (rect, transform) in self.data.rects.iter().zip(&self.data.transforms) {
            let center_x = rect.x + rect.width * 0.5;
            let center_y = rect.y + rect.height * 0.5;
            context.set_transform(&pathfinder_transform(transform.affine(center_x, center_y)));
            let bounds = RectF::new(vec2f(rect.x, rect.y), vec2f(rect.width, rect.height));
            let primary = pathfinder_color(rect.rgba);
            match workload {
                Workload::Rect => {
                    context.set_fill_style(primary);
                    context.fill_rect(bounds);
                }
                Workload::Stroke => {
                    context.set_stroke_style(primary);
                    context.set_line_width(3.0 + (rect.width + rect.height) * 0.02);
                    context.stroke_rect(bounds);
                }
                Workload::StrokeRect => {
                    context.set_fill_style(primary);
                    context.fill_rect(bounds);
                    context.set_stroke_style(ColorU::white());
                    context.set_line_width(3.0);
                    context.stroke_rect(bounds);
                }
                Workload::LinearGradient => {
                    let mut gradient = Gradient::linear_from_points(
                        vec2f(rect.x, rect.y),
                        vec2f(rect.x + rect.width, rect.y + rect.height),
                    );
                    gradient.add_color_stop(primary, 0.0);
                    gradient.add_color_stop(inverse_color(rect.rgba), 1.0);
                    context.set_fill_style(gradient);
                    context.fill_rect(bounds);
                }
                _ => unreachable!("rectangle workload routing is exhaustive"),
            }
        }
        context.into_canvas().into_scene()
    }

    fn capture_front_buffer(&self) -> Vec<u8> {
        let mut pixels = vec![0; self.width as usize * self.height as usize * 4];
        unsafe {
            gl::ReadBuffer(gl::BACK);
            gl::PixelStorei(gl::PACK_ALIGNMENT, 1);
            gl::ReadPixels(
                0,
                0,
                self.width as i32,
                self.height as i32,
                gl::RGBA,
                gl::UNSIGNED_BYTE,
                pixels.as_mut_ptr().cast(),
            );
        }
        let stride = self.width as usize * 4;
        for row in 0..self.height as usize / 2 {
            let opposite = self.height as usize - 1 - row;
            let (before, after) = pixels.split_at_mut(opposite * stride);
            before[row * stride..(row + 1) * stride].swap_with_slice(&mut after[..stride]);
        }
        pixels
    }
}

fn adjust_gl_drawable_size(
    window: &mut sdl2::video::Window,
    target_width: u32,
    target_height: u32,
) -> Result<(u32, u32), String> {
    let (window_width, window_height) = window.size();
    let (drawable_width, drawable_height) = window.drawable_size();
    if window_width == 0 || window_height == 0 || drawable_width == 0 || drawable_height == 0 {
        return Err("SDL reported a zero-sized OpenGL window or drawable".into());
    }
    if (drawable_width, drawable_height) != (target_width, target_height) {
        let adjusted_width = (f64::from(target_width) * f64::from(window_width)
            / f64::from(drawable_width))
        .round()
        .max(1.0) as u32;
        let adjusted_height = (f64::from(target_height) * f64::from(window_height)
            / f64::from(drawable_height))
        .round()
        .max(1.0) as u32;
        window
            .set_size(adjusted_width, adjusted_height)
            .map_err(|error| format!("failed to resize HiDPI OpenGL window: {error}"))?;
    }
    let actual = window.drawable_size();
    if actual != (target_width, target_height) {
        return Err(format!(
            "requested OpenGL drawable {target_width}x{target_height}, but HiDPI adjustment produced {}x{}",
            actual.0, actual.1
        ));
    }
    Ok(actual)
}

struct GlContract {
    major: u8,
    minor: u8,
}

fn validate_gl_contract(video: &sdl2::VideoSubsystem) -> Result<GlContract, String> {
    let attributes = video.gl_attr();
    let (major, minor) = attributes.context_version();
    if (major, minor) < (3, 3) {
        return Err(format!(
            "OpenGL context is {major}.{minor}; GL 3.3 or newer is required"
        ));
    }
    let profile = attributes.context_profile();
    if profile != GLProfile::Core {
        return Err(format!(
            "OpenGL context profile is {profile:?}; core profile is required"
        ));
    }
    let color_bits = [
        attributes.red_size(),
        attributes.green_size(),
        attributes.blue_size(),
        attributes.alpha_size(),
    ];
    if color_bits != [8, 8, 8, 8] {
        return Err(format!(
            "OpenGL drawable color bits are {color_bits:?}; exact RGBA8 is required"
        ));
    }
    let depth_bits = attributes.depth_size();
    let stencil_bits = attributes.stencil_size();
    if depth_bits < 24 || stencil_bits < 8 {
        return Err(format!(
            "OpenGL drawable has depth={depth_bits}, stencil={stencil_bits}; depth>=24 and stencil>=8 are required"
        ));
    }
    if !attributes.double_buffer() {
        return Err("OpenGL drawable is not double-buffered".into());
    }
    let multisample_buffers = attributes.multisample_buffers();
    let multisample_samples = attributes.multisample_samples();
    if multisample_buffers != 0 || multisample_samples != 0 {
        return Err(format!(
            "OpenGL drawable has MSAA buffers={multisample_buffers}, samples={multisample_samples}; zero MSAA is required"
        ));
    }
    if !attributes.accelerated_visual() {
        return Err("OpenGL drawable is not hardware accelerated".into());
    }
    Ok(GlContract { major, minor })
}

impl BenchmarkDriver for PathfinderDriver {
    fn pump_events(&mut self) -> Result<bool, String> {
        for event in self.event_pump.poll_iter() {
            if matches!(
                event,
                Event::Quit { .. }
                    | Event::KeyDown {
                        keycode: Some(Keycode::Escape),
                        ..
                    }
            ) {
                return Ok(false);
            }
        }
        let drawable = self.window.drawable_size();
        if drawable != (self.width, self.height) {
            return Err(format!(
                "drawable resized from {}x{} to {}x{} during benchmark",
                self.width, self.height, drawable.0, drawable.1
            ));
        }
        Ok(true)
    }

    fn update(&mut self, frame_index: u32) -> Result<(), String> {
        self.data.update(frame_index);
        self.scene = self.build_canvas_scene();
        Ok(())
    }

    fn render(&mut self) -> Result<(), String> {
        self.scene.build_and_render(
            &mut self.renderer,
            BuildOptions::default(),
            SequentialExecutor,
        );
        Ok(())
    }

    fn present(&mut self) -> Result<(), String> {
        self.window.gl_swap_window();
        Ok(())
    }

    fn finish_gpu(&mut self) -> Result<(), String> {
        unsafe { gl::Finish() };
        Ok(())
    }

    fn capture(&mut self, path: &Path) -> Result<(), String> {
        let pixels = self.capture_front_buffer();
        if path.extension().is_some_and(|extension| extension == "ppm") {
            return write_ppm(path, &pixels, self.width, self.height);
        }
        image::save_buffer_with_format(
            path,
            &pixels,
            self.width,
            self.height,
            ColorType::Rgba8,
            OutputImageFormat::Png,
        )
        .map_err(|error| format!("failed to save {}: {error}", path.display()))
    }

    fn runtime_metadata(&self) -> &RuntimeMetadata {
        &self.runtime
    }
}

fn load_image(opts: &CliOptions) -> Result<(Option<Pattern>, String), String> {
    if opts.benchmark != Workload::Image {
        return Ok((None, String::new()));
    }
    let path = locate_image(&opts.image_ext)?;
    let hash = sha256_file(&path)?;
    let decoded = image::open(&path)
        .map_err(|error| format!("failed to decode {}: {error}", path.display()))?
        .to_rgba8();
    let (width, height) = decoded.dimensions();
    let pixels = decoded
        .into_raw()
        .chunks_exact(4)
        .map(|pixel| ColorU::new(pixel[0], pixel[1], pixel[2], pixel[3]))
        .collect();
    let image = PathfinderImage::new(vec2i(width as i32, height as i32), Arc::new(pixels));
    let mut pattern = Pattern::from_image(image);
    pattern.set_smoothing_enabled(true);
    Ok((Some(pattern), hash))
}

fn pathfinder_color(rgba: [u8; 4]) -> ColorU {
    ColorU::new(rgba[0], rgba[1], rgba[2], rgba[3])
}

fn inverse_color(rgba: [u8; 4]) -> ColorU {
    ColorU::new(255 - rgba[0], 255 - rgba[1], 255 - rgba[2], rgba[3])
}

fn pathfinder_transform(affine: [f64; 6]) -> Transform2F {
    Transform2F::row_major(
        affine[0] as f32,
        affine[2] as f32,
        affine[4] as f32,
        affine[1] as f32,
        affine[3] as f32,
        affine[5] as f32,
    )
}

fn pathfinder_image_transform(
    rect: &bench_common::RectData,
    transform: &bench_common::TransformData,
    pattern: &Pattern,
) -> Transform2F {
    let size = pattern.size();
    let image_width = size.x() as f32;
    let image_height = size.y() as f32;
    pathfinder_transform(transform.fit_affine(rect, image_width, image_height))
}

fn gl_string(name: gl::types::GLenum) -> String {
    unsafe {
        let pointer = gl::GetString(name);
        if pointer.is_null() {
            return "unknown".into();
        }
        CStr::from_ptr(pointer.cast())
            .to_string_lossy()
            .into_owned()
    }
}

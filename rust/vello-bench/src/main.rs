use bench_common::{
    Backend, BenchmarkDriver, BenchmarkScene, CliOptions, RuntimeMetadata, Workload, locate_image,
    run_benchmark, sha256_file, write_ppm,
};
use image::{ColorType, ImageFormat as OutputImageFormat};
use sdl2::event::Event;
use sdl2::keyboard::Keycode;
use std::path::Path;
use vello::kurbo::{Affine, Cap, Circle, Join, Rect, Stroke};
use vello::peniko::{
    Blob, Color, Extend, Fill, Gradient, ImageAlphaType, ImageBrush, ImageData, ImageFormat,
    ImageQuality, color::palette,
};
use vello::wgpu;
use vello::wgpu::rwh::{DisplayHandle, HandleError, HasDisplayHandle, RawDisplayHandle};
use vello::wgpu::util::TextureBlitter;
use vello::{AaConfig, AaSupport, RenderParams, Renderer, RendererOptions, Scene};

const VELLO_VERSION: &str = "0.10.0";
const SURFACE_STARTUP_TIMEOUT: std::time::Duration = std::time::Duration::from_secs(5);
const SURFACE_STARTUP_RETRY_MS: u32 = 10;

#[derive(Debug)]
struct SdlDisplayHandle(RawDisplayHandle);

// SAFETY: The handle is only used while the owning SDL context/window remains
// alive in `VelloDriver`; native display handles are thread-safe connection IDs.
unsafe impl Send for SdlDisplayHandle {}
// SAFETY: See the `Send` justification above. The wrapper is immutable.
unsafe impl Sync for SdlDisplayHandle {}

impl HasDisplayHandle for SdlDisplayHandle {
    fn display_handle(&self) -> Result<DisplayHandle<'_>, HandleError> {
        // SAFETY: `self.0` remains valid for the lifetime of this wrapper.
        Ok(unsafe { DisplayHandle::borrow_raw(self.0) })
    }
}

fn main() {
    if let Err(error) = main_result() {
        eprintln!("vello-bench: {error}");
        std::process::exit(1);
    }
}

fn main_result() -> Result<(), String> {
    let opts = CliOptions::parse(Backend::WebGpu)?;
    if opts.backend != Backend::WebGpu {
        return Err("Vello supports only --backend=webgpu".into());
    }
    println!("Vello {VELLO_VERSION} benchmark");
    println!("Benchmark: {}", opts.benchmark.as_str());
    println!("Backend:   {}", opts.backend.as_str());
    println!("Scene:     {}", opts.scene_mode.as_str());
    println!("GPU sync:  {}", opts.gpu_sync);

    let mut driver = VelloDriver::new(&opts)?;
    run_benchmark(&opts, &mut driver)?;
    Ok(())
}

struct VelloDriver {
    // Surface must be dropped before its raw SDL window handle.
    surface: wgpu::Surface<'static>,
    window: sdl2::video::Window,
    _sdl: sdl2::Sdl,
    event_pump: sdl2::EventPump,
    device: wgpu::Device,
    queue: wgpu::Queue,
    config: wgpu::SurfaceConfiguration,
    target_texture: wgpu::Texture,
    target_view: wgpu::TextureView,
    blitter: TextureBlitter,
    renderer: Renderer,
    scene: Scene,
    data: BenchmarkScene,
    image: Option<ImageBrush>,
    pending_frame: Option<wgpu::SurfaceTexture>,
    runtime: RuntimeMetadata,
}

impl VelloDriver {
    fn new(opts: &CliOptions) -> Result<Self, String> {
        let sdl = sdl2::init().map_err(|error| format!("failed to initialize SDL: {error}"))?;
        let video = sdl
            .video()
            .map_err(|error| format!("failed to initialize SDL video: {error}"))?;
        let mut window = video
            .window(
                &format!("{} Vello", opts.benchmark.as_str()),
                opts.width,
                opts.height,
            )
            .position_centered()
            .allow_highdpi()
            .metal_view()
            .always_on_top()
            .build()
            .map_err(|error| format!("failed to create SDL window: {error}"))?;
        window.show();
        window.raise();
        let (actual_width, actual_height) =
            adjust_webgpu_drawable_size(&mut window, opts.width, opts.height)?;
        let event_pump = sdl
            .event_pump()
            .map_err(|error| format!("failed to create SDL event pump: {error}"))?;
        let raw_display = window
            .display_handle()
            .map_err(|error| format!("failed to read SDL display handle: {error}"))?
            .as_raw();
        let instance =
            wgpu::Instance::new(wgpu::InstanceDescriptor::new_with_display_handle_from_env(
                Box::new(SdlDisplayHandle(raw_display)),
            ));
        // SAFETY: `surface` is declared before `window` in `VelloDriver`, so the
        // surface is dropped first. The window is never moved out of the driver.
        let surface: wgpu::Surface<'static> = unsafe {
            instance.create_surface_unsafe(
                wgpu::SurfaceTargetUnsafe::from_window(&window)
                    .map_err(|error| format!("failed to read SDL window handles: {error}"))?,
            )
        }
        .map_err(|error| format!("failed to create wgpu surface: {error}"))?;
        let adapter = pollster::block_on(instance.request_adapter(&wgpu::RequestAdapterOptions {
            power_preference: wgpu::PowerPreference::HighPerformance,
            force_fallback_adapter: false,
            compatible_surface: Some(&surface),
        }))
        .map_err(|error| format!("no compatible GPU adapter: {error}"))?;

        let available_features = adapter.features();
        let optional_features = wgpu::Features::CLEAR_TEXTURE | wgpu::Features::PIPELINE_CACHE;
        let (device, queue) = pollster::block_on(adapter.request_device(&wgpu::DeviceDescriptor {
            label: Some("vello-bench device"),
            required_features: available_features & optional_features,
            required_limits: wgpu::Limits::default(),
            ..Default::default()
        }))
        .map_err(|error| format!("failed to create wgpu device: {error}"))?;

        let capabilities = surface.get_capabilities(&adapter);
        let format = capabilities
            .formats
            .iter()
            .copied()
            .find(|format| *format == wgpu::TextureFormat::Bgra8Unorm)
            .ok_or_else(|| "surface has no BGRA8 non-sRGB format".to_string())?;
        let present_mode = choose_present_mode(opts.vsync, &capabilities.present_modes)?;
        let config = wgpu::SurfaceConfiguration {
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            format,
            width: actual_width,
            height: actual_height,
            present_mode,
            desired_maximum_frame_latency: if present_mode == wgpu::PresentMode::Mailbox {
                3
            } else {
                2
            },
            alpha_mode: wgpu::CompositeAlphaMode::Auto,
            view_formats: vec![],
        };
        surface.configure(&device, &config);

        let target_texture = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("vello-bench intermediate target"),
            size: wgpu::Extent3d {
                width: actual_width,
                height: actual_height,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::STORAGE_BINDING
                | wgpu::TextureUsages::TEXTURE_BINDING
                | wgpu::TextureUsages::COPY_SRC,
            view_formats: &[],
        });
        let target_view = target_texture.create_view(&wgpu::TextureViewDescriptor::default());
        let blitter = TextureBlitter::new(&device, format);
        let renderer = Renderer::new(
            &device,
            RendererOptions {
                use_cpu: false,
                antialiasing_support: AaSupport::area_only(),
                ..Default::default()
            },
        )
        .map_err(|error| format!("failed to create Vello renderer: {error}"))?;

        let (image, asset_hash) = load_image(opts)?;
        let adapter_info = adapter.get_info();
        let graphics_api = format!("WebGPU {:?}", adapter_info.backend);
        let runtime = RuntimeMetadata {
            engine: "vello".into(),
            engine_version: VELLO_VERSION.into(),
            engine_revision: "crates.io:vello@0.10.0".into(),
            scene_model: "immediate".into(),
            graphics_api,
            gpu_device: adapter_info.name,
            gpu_vendor: format!("0x{:04x}", adapter_info.vendor),
            gpu_driver: format!("{} {}", adapter_info.driver, adapter_info.driver_info)
                .trim()
                .into(),
            gpu_completion: if opts.gpu_sync {
                "wgpu Device::poll(wait_indefinitely)".into()
            } else {
                "none".into()
            },
            actual_width,
            actual_height,
            pixel_format: "BGRA8".into(),
            vsync_actual: present_mode != wgpu::PresentMode::Immediate,
            present_mode: format!("{present_mode:?}").to_ascii_lowercase(),
            asset_hash,
        };

        let mut driver = Self {
            surface,
            window,
            _sdl: sdl,
            event_pump,
            device,
            queue,
            config,
            target_texture,
            target_view,
            blitter,
            renderer,
            scene: Scene::new(),
            data: BenchmarkScene::new_with_size(opts, actual_width, actual_height),
            image,
            pending_frame: None,
            runtime,
        };
        driver.wait_for_surface_ready()?;
        Ok(driver)
    }

    fn wait_for_surface_ready(&mut self) -> Result<(), String> {
        let started = std::time::Instant::now();
        loop {
            if !self.pump_events()? {
                return Err("window closed before the WebGPU surface became ready".into());
            }
            let status = self.surface.get_current_texture();
            match status {
                wgpu::CurrentSurfaceTexture::Success(frame) => {
                    // This readiness probe is intentionally outside run_benchmark's
                    // frame timer. Normal frame acquisition remains in render().
                    drop(frame);
                    return Ok(());
                }
                status if is_retryable_startup_surface_status(&status) => {
                    if started.elapsed() >= SURFACE_STARTUP_TIMEOUT {
                        return Err(format!(
                            "WebGPU surface remained unavailable during the {} ms startup window: {status:?}",
                            SURFACE_STARTUP_TIMEOUT.as_millis()
                        ));
                    }
                    if self
                        .event_pump
                        .wait_event_timeout(SURFACE_STARTUP_RETRY_MS)
                        .is_some_and(|event| is_exit_event(&event))
                    {
                        return Err("window closed before the WebGPU surface became ready".into());
                    }
                }
                wgpu::CurrentSurfaceTexture::Suboptimal(frame) => {
                    drop(frame);
                    return Err("WebGPU surface was suboptimal during startup".into());
                }
                status => {
                    return Err(format!(
                        "failed to acquire WebGPU surface texture during startup: {status:?}"
                    ));
                }
            }
        }
    }

    fn rebuild_scene(&mut self) {
        self.scene.reset();
        let workload = self.data.workload;
        if workload.uses_circles() {
            for (circle, transform) in self.data.circles.iter().zip(&self.data.transforms) {
                let shape = Circle::new(
                    (circle.center_x as f64, circle.center_y as f64),
                    circle.radius as f64,
                );
                let affine = Affine::new(transform.affine(circle.center_x, circle.center_y));
                let primary = color(circle.rgba);
                match workload {
                    Workload::Circle => {
                        self.scene
                            .fill(Fill::NonZero, affine, primary, None, &shape);
                    }
                    Workload::RadialGradient => {
                        let inverse = Color::from_rgba8(
                            255 - circle.rgba[0],
                            255 - circle.rgba[1],
                            255 - circle.rgba[2],
                            circle.rgba[3],
                        );
                        let gradient = Gradient::new_radial(
                            (circle.center_x as f64, circle.center_y as f64),
                            circle.radius,
                        )
                        .with_extend(Extend::Pad)
                        .with_stops([(0.0, primary), (1.0, inverse)]);
                        self.scene
                            .fill(Fill::NonZero, affine, &gradient, None, &shape);
                    }
                    _ => unreachable!("circle workload routing is exhaustive"),
                }
            }
            return;
        }

        if workload == Workload::Image {
            let image = self
                .image
                .as_ref()
                .expect("image workload loaded its asset");
            for (rect, transform) in self.data.rects.iter().zip(&self.data.transforms) {
                self.scene
                    .draw_image(image.as_ref(), image_affine(rect, transform, image));
            }
            return;
        }

        for (rect, transform) in self.data.rects.iter().zip(&self.data.transforms) {
            let shape = Rect::new(
                rect.x as f64,
                rect.y as f64,
                (rect.x + rect.width) as f64,
                (rect.y + rect.height) as f64,
            );
            let center_x = rect.x + rect.width * 0.5;
            let center_y = rect.y + rect.height * 0.5;
            let affine = Affine::new(transform.affine(center_x, center_y));
            let primary = color(rect.rgba);
            match workload {
                Workload::Rect => {
                    self.scene
                        .fill(Fill::NonZero, affine, primary, None, &shape);
                }
                Workload::Stroke => {
                    let stroke = standard_stroke((3.0 + (rect.width + rect.height) * 0.02) as f64);
                    self.scene.stroke(&stroke, affine, primary, None, &shape);
                }
                Workload::StrokeRect => {
                    self.scene
                        .fill(Fill::NonZero, affine, primary, None, &shape);
                    self.scene.stroke(
                        &standard_stroke(3.0),
                        affine,
                        palette::css::WHITE,
                        None,
                        &shape,
                    );
                }
                Workload::LinearGradient => {
                    let inverse = Color::from_rgba8(
                        255 - rect.rgba[0],
                        255 - rect.rgba[1],
                        255 - rect.rgba[2],
                        rect.rgba[3],
                    );
                    let gradient = Gradient::new_linear(
                        (rect.x as f64, rect.y as f64),
                        ((rect.x + rect.width) as f64, (rect.y + rect.height) as f64),
                    )
                    .with_extend(Extend::Pad)
                    .with_stops([(0.0, primary), (1.0, inverse)]);
                    self.scene
                        .fill(Fill::NonZero, affine, &gradient, None, &shape);
                }
                _ => unreachable!("rectangle workload routing is exhaustive"),
            }
        }
    }

    fn read_target_rgba(&self) -> Result<Vec<u8>, String> {
        let unpadded_bytes_per_row = self.config.width * 4;
        let padded_bytes_per_row = unpadded_bytes_per_row.div_ceil(256) * 256;
        let buffer_size = u64::from(padded_bytes_per_row) * u64::from(self.config.height);
        let buffer = self.device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("vello-bench capture readback"),
            size: buffer_size,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });
        let mut encoder = self
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("vello-bench capture encoder"),
            });
        encoder.copy_texture_to_buffer(
            self.target_texture.as_image_copy(),
            wgpu::TexelCopyBufferInfo {
                buffer: &buffer,
                layout: wgpu::TexelCopyBufferLayout {
                    offset: 0,
                    bytes_per_row: Some(padded_bytes_per_row),
                    rows_per_image: Some(self.config.height),
                },
            },
            wgpu::Extent3d {
                width: self.config.width,
                height: self.config.height,
                depth_or_array_layers: 1,
            },
        );
        self.queue.submit([encoder.finish()]);
        let (sender, receiver) = std::sync::mpsc::sync_channel(1);
        buffer
            .slice(..)
            .map_async(wgpu::MapMode::Read, move |result| {
                let _ = sender.send(result);
            });
        self.device
            .poll(wgpu::PollType::wait_indefinitely())
            .map_err(|error| format!("capture GPU wait failed: {error}"))?;
        receiver
            .recv()
            .map_err(|error| format!("capture mapping callback was lost: {error}"))?
            .map_err(|error| format!("capture buffer mapping failed: {error}"))?;
        let mapped = buffer.slice(..).get_mapped_range();
        let mut rgba =
            Vec::with_capacity(unpadded_bytes_per_row as usize * self.config.height as usize);
        for row in mapped.chunks_exact(padded_bytes_per_row as usize) {
            rgba.extend_from_slice(&row[..unpadded_bytes_per_row as usize]);
        }
        drop(mapped);
        buffer.unmap();
        Ok(rgba)
    }
}

fn adjust_webgpu_drawable_size(
    window: &mut sdl2::video::Window,
    target_width: u32,
    target_height: u32,
) -> Result<(u32, u32), String> {
    let (window_width, window_height) = window.size();
    let (drawable_width, drawable_height) = webgpu_drawable_size(window);
    if window_width == 0 || window_height == 0 || drawable_width == 0 || drawable_height == 0 {
        return Err("SDL reported a zero-sized WebGPU window or drawable".into());
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
            .map_err(|error| format!("failed to resize HiDPI WebGPU window: {error}"))?;
    }
    let actual = webgpu_drawable_size(window);
    if actual != (target_width, target_height) {
        return Err(format!(
            "requested WebGPU drawable {target_width}x{target_height}, but HiDPI adjustment produced {}x{}",
            actual.0, actual.1
        ));
    }
    Ok(actual)
}

#[cfg(target_os = "macos")]
fn webgpu_drawable_size(window: &sdl2::video::Window) -> (u32, u32) {
    let mut width = 0;
    let mut height = 0;
    unsafe {
        sdl2::sys::SDL_Metal_GetDrawableSize(window.raw(), &mut width, &mut height);
    }
    (width as u32, height as u32)
}

#[cfg(not(target_os = "macos"))]
fn webgpu_drawable_size(window: &sdl2::video::Window) -> (u32, u32) {
    window.size()
}

impl BenchmarkDriver for VelloDriver {
    fn pump_events(&mut self) -> Result<bool, String> {
        for event in self.event_pump.poll_iter() {
            if is_exit_event(&event) {
                return Ok(false);
            }
        }
        let drawable = webgpu_drawable_size(&self.window);
        if drawable != (self.config.width, self.config.height) {
            return Err(format!(
                "drawable resized from {}x{} to {}x{} during benchmark",
                self.config.width, self.config.height, drawable.0, drawable.1
            ));
        }
        Ok(true)
    }

    fn update(&mut self, frame_index: u32) -> Result<(), String> {
        self.data.update(frame_index);
        self.rebuild_scene();
        Ok(())
    }

    fn render(&mut self) -> Result<(), String> {
        if self.pending_frame.is_some() {
            return Err("render called while a surface frame is still pending".into());
        }
        let frame = match self.surface.get_current_texture() {
            wgpu::CurrentSurfaceTexture::Success(frame) => frame,
            wgpu::CurrentSurfaceTexture::Suboptimal(_) => {
                return Err("WebGPU surface became suboptimal during benchmark".into());
            }
            status => {
                return Err(format!(
                    "failed to acquire WebGPU surface texture: {status:?}"
                ));
            }
        };
        self.renderer
            .render_to_texture(
                &self.device,
                &self.queue,
                &self.scene,
                &self.target_view,
                &RenderParams {
                    base_color: palette::css::BLACK,
                    width: self.config.width,
                    height: self.config.height,
                    antialiasing_method: AaConfig::Area,
                },
            )
            .map_err(|error| format!("Vello render failed: {error}"))?;
        let surface_view = frame
            .texture
            .create_view(&wgpu::TextureViewDescriptor::default());
        let mut encoder = self
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("vello-bench present blit"),
            });
        self.blitter
            .copy(&self.device, &mut encoder, &self.target_view, &surface_view);
        self.queue.submit([encoder.finish()]);
        self.pending_frame = Some(frame);
        Ok(())
    }

    fn present(&mut self) -> Result<(), String> {
        self.pending_frame
            .take()
            .ok_or_else(|| "present called without a rendered frame".to_string())?
            .present();
        Ok(())
    }

    fn finish_gpu(&mut self) -> Result<(), String> {
        self.device
            .poll(wgpu::PollType::wait_indefinitely())
            .map_err(|error| format!("GPU completion wait failed: {error}"))?;
        Ok(())
    }

    fn capture(&mut self, path: &Path) -> Result<(), String> {
        let rgba = self.read_target_rgba()?;
        if path.extension().is_some_and(|extension| extension == "ppm") {
            return write_ppm(path, &rgba, self.config.width, self.config.height);
        }
        image::save_buffer_with_format(
            path,
            &rgba,
            self.config.width,
            self.config.height,
            ColorType::Rgba8,
            OutputImageFormat::Png,
        )
        .map_err(|error| format!("failed to save {}: {error}", path.display()))
    }

    fn runtime_metadata(&self) -> &RuntimeMetadata {
        &self.runtime
    }
}

fn is_exit_event(event: &Event) -> bool {
    matches!(
        event,
        Event::Quit { .. }
            | Event::KeyDown {
                keycode: Some(Keycode::Escape),
                ..
            }
    )
}

fn is_retryable_startup_surface_status(status: &wgpu::CurrentSurfaceTexture) -> bool {
    matches!(
        status,
        wgpu::CurrentSurfaceTexture::Occluded | wgpu::CurrentSurfaceTexture::Timeout
    )
}

fn choose_present_mode(
    vsync: bool,
    supported: &[wgpu::PresentMode],
) -> Result<wgpu::PresentMode, String> {
    if vsync {
        return supported
            .contains(&wgpu::PresentMode::Fifo)
            .then_some(wgpu::PresentMode::Fifo)
            .ok_or_else(|| "surface does not support FIFO VSync".into());
    }
    if supported.contains(&wgpu::PresentMode::Immediate) {
        return Ok(wgpu::PresentMode::Immediate);
    }
    if supported.contains(&wgpu::PresentMode::Mailbox) {
        return Ok(wgpu::PresentMode::Mailbox);
    }
    supported
        .contains(&wgpu::PresentMode::Fifo)
        .then_some(wgpu::PresentMode::Fifo)
        .ok_or_else(|| "surface exposes no supported present mode".into())
}

fn standard_stroke(width: f64) -> Stroke {
    Stroke::new(width)
        .with_caps(Cap::Butt)
        .with_join(Join::Miter)
        .with_miter_limit(4.0)
}

fn load_image(opts: &CliOptions) -> Result<(Option<ImageBrush>, String), String> {
    if opts.benchmark != Workload::Image {
        return Ok((None, String::new()));
    }
    let path = locate_image(&opts.image_ext)?;
    let hash = sha256_file(&path)?;
    let decoded = image::open(&path)
        .map_err(|error| format!("failed to decode {}: {error}", path.display()))?
        .to_rgba8();
    let (width, height) = decoded.dimensions();
    let data = ImageData {
        data: Blob::from(decoded.into_raw()),
        format: ImageFormat::Rgba8,
        alpha_type: ImageAlphaType::Alpha,
        width,
        height,
    };
    Ok((
        Some(ImageBrush::new(data).with_quality(ImageQuality::Medium)),
        hash,
    ))
}

fn color(rgba: [u8; 4]) -> Color {
    Color::from_rgba8(rgba[0], rgba[1], rgba[2], rgba[3])
}

fn image_affine(
    rect: &bench_common::RectData,
    transform: &bench_common::TransformData,
    image: &ImageBrush,
) -> Affine {
    let image_width = image.image.width as f32;
    let image_height = image.image.height as f32;
    Affine::new(transform.fit_affine(rect, image_width, image_height))
}

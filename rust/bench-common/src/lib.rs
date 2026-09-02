//! Shared benchmark contract for the Rust GPU adapters.

use serde::Serialize;
use sha2::{Digest, Sha256};
use std::env;
use std::fmt;
use std::fs::{self, File};
use std::io::{BufWriter, Write};
use std::path::{Path, PathBuf};
use std::str::FromStr;
use std::time::{Instant, SystemTime, UNIX_EPOCH};

pub const DEFAULT_OBJECT_COUNT: u32 = 5_000;

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum Backend {
    Gl,
}

impl Backend {
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Gl => "gl",
        }
    }
}

impl FromStr for Backend {
    type Err = String;

    fn from_str(value: &str) -> Result<Self, Self::Err> {
        match value {
            "gl" => Ok(Self::Gl),
            _ => Err(format!("invalid backend: {value}")),
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum SceneMode {
    Default,
    Rotation,
}

impl SceneMode {
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Default => "default",
            Self::Rotation => "rotation",
        }
    }
}

impl FromStr for SceneMode {
    type Err = String;

    fn from_str(value: &str) -> Result<Self, Self::Err> {
        match value {
            "default" => Ok(Self::Default),
            "rotation" => Ok(Self::Rotation),
            _ => Err(format!("invalid scene mode: {value}")),
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
pub enum Workload {
    #[serde(rename = "rect")]
    Rect,
    #[serde(rename = "circle")]
    Circle,
    #[serde(rename = "stroke")]
    Stroke,
    #[serde(rename = "image")]
    Image,
    #[serde(rename = "lineargradient")]
    LinearGradient,
    #[serde(rename = "radialgradient")]
    RadialGradient,
    #[serde(rename = "strokerect")]
    StrokeRect,
}

impl Workload {
    pub const ALL: [Self; 7] = [
        Self::Rect,
        Self::Circle,
        Self::Stroke,
        Self::Image,
        Self::LinearGradient,
        Self::RadialGradient,
        Self::StrokeRect,
    ];

    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Rect => "rect",
            Self::Circle => "circle",
            Self::Stroke => "stroke",
            Self::Image => "image",
            Self::LinearGradient => "lineargradient",
            Self::RadialGradient => "radialgradient",
            Self::StrokeRect => "strokerect",
        }
    }

    pub const fn uses_circles(self) -> bool {
        matches!(self, Self::Circle | Self::RadialGradient)
    }
}

impl FromStr for Workload {
    type Err = String;

    fn from_str(value: &str) -> Result<Self, Self::Err> {
        match value {
            "rect" | "rectbench" => Ok(Self::Rect),
            "circle" | "circlebench" => Ok(Self::Circle),
            "stroke" | "strokebench" => Ok(Self::Stroke),
            "image" | "imagebench" => Ok(Self::Image),
            "lineargradient" | "lineargradientbench" => Ok(Self::LinearGradient),
            "radialgradient" | "radialgradientbench" => Ok(Self::RadialGradient),
            "strokerect" | "strokerectbench" => Ok(Self::StrokeRect),
            _ => Err(format!("invalid benchmark: {value}")),
        }
    }
}

#[derive(Clone, Debug)]
pub struct CliOptions {
    pub backend: Backend,
    pub benchmark: Workload,
    pub scene_mode: SceneMode,
    pub image_ext: String,
    pub seed: u64,
    pub frames: u32,
    pub warmup: u32,
    pub width: u32,
    pub height: u32,
    pub vsync: bool,
    pub gpu_sync: bool,
    pub output_path: Option<PathBuf>,
    pub capture_path: Option<PathBuf>,
}

impl CliOptions {
    pub fn parse(default_backend: Backend) -> Result<Self, String> {
        let mut opts = Self {
            backend: default_backend,
            benchmark: Workload::Rect,
            scene_mode: SceneMode::Default,
            image_ext: "png".into(),
            seed: 12_345,
            frames: 1_000,
            warmup: 120,
            width: 2_560,
            height: 1_440,
            vsync: false,
            gpu_sync: true,
            output_path: None,
            capture_path: None,
        };

        for argument in env::args().skip(1) {
            if argument == "--help" || argument == "-h" {
                return Err(usage());
            }
            let (key, value) = argument
                .split_once('=')
                .ok_or_else(|| format!("expected --key=value, got: {argument}"))?;
            match key {
                "--backend" => opts.backend = value.parse()?,
                "--benchmark" => opts.benchmark = value.parse()?,
                "--scene" => opts.scene_mode = value.parse()?,
                "--image" if matches!(value, "png" | "jpg") => opts.image_ext = value.into(),
                "--image" => return Err(format!("invalid image type: {value}")),
                "--seed" => opts.seed = parse_number(key, value)?,
                "--frames" => opts.frames = parse_number(key, value)?,
                "--warmup" => opts.warmup = parse_number(key, value)?,
                "--width" => opts.width = parse_number(key, value)?,
                "--height" => opts.height = parse_number(key, value)?,
                "--vsync" => opts.vsync = parse_bool(key, value)?,
                "--gpu_sync" => opts.gpu_sync = parse_bool(key, value)?,
                "--output" => opts.output_path = Some(value.into()),
                "--capture" => opts.capture_path = Some(value.into()),
                _ => return Err(format!("unknown option: {key}")),
            }
        }

        if opts.frames == 0 {
            return Err("--frames must be greater than zero".into());
        }
        if opts.width == 0 || opts.height == 0 {
            return Err("--width and --height must be greater than zero".into());
        }
        Ok(opts)
    }
}

fn parse_number<T>(key: &str, value: &str) -> Result<T, String>
where
    T: FromStr,
    T::Err: fmt::Display,
{
    value
        .parse()
        .map_err(|error| format!("invalid {key} value {value:?}: {error}"))
}

fn parse_bool(key: &str, value: &str) -> Result<bool, String> {
    match value {
        "1" | "true" | "yes" => Ok(true),
        "0" | "false" | "no" => Ok(false),
        _ => Err(format!("invalid {key} boolean: {value}")),
    }
}

pub fn usage() -> String {
    format!(
        "Usage: pathfinder-bench [options]\n\
         --benchmark={}\n\
         --backend=gl --scene=default|rotation --image=png|jpg\n\
         --seed=INT --frames=INT --warmup=INT\n\
         --width=INT --height=INT --vsync=0|1 --gpu_sync=0|1\n\
         --output=PATH --capture=PATH",
        Workload::ALL
            .iter()
            .map(|value| value.as_str())
            .collect::<Vec<_>>()
            .join("|")
    )
}

/// PCG-XSH-RR with the same seeding and float conversion as `src/common/rng.hpp`.
#[derive(Clone, Debug)]
pub struct Pcg32 {
    state: u64,
}

impl Pcg32 {
    pub fn new(seed: u64) -> Self {
        let mut rng = Self { state: 0 };
        rng.next_u32();
        rng.state = rng.state.wrapping_add(seed);
        rng.next_u32();
        rng
    }

    pub fn next_u32(&mut self) -> u32 {
        let old_state = self.state;
        self.state = old_state
            .wrapping_mul(6_364_136_223_846_793_005)
            .wrapping_add(3);
        let xorshifted = (((old_state >> 18) ^ old_state) >> 27) as u32;
        let rotation = (old_state >> 59) as u32;
        xorshifted.rotate_right(rotation)
    }

    pub fn next_float_01(&mut self) -> f32 {
        self.next_u32() as f32 / u32::MAX as f32
    }

    pub fn next_float(&mut self, minimum: f32, maximum: f32) -> f32 {
        self.next_float_01().mul_add(maximum - minimum, minimum)
    }

    pub fn next_u8(&mut self, minimum: u8, maximum: u8) -> u8 {
        let range = u32::from(maximum) - u32::from(minimum) + 1;
        minimum + (self.next_u32() % range) as u8
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct RectData {
    pub x: f32,
    pub y: f32,
    pub width: f32,
    pub height: f32,
    pub rgba: [u8; 4],
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct CircleData {
    pub center_x: f32,
    pub center_y: f32,
    pub radius: f32,
    pub rgba: [u8; 4],
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TransformData {
    pub dx: f32,
    pub dy: f32,
    pub rotation_degrees: f32,
    pub scale: f32,
}

impl TransformData {
    fn linear_components(self, extra_scale: f32) -> (f32, f32, f32, f32) {
        let scale = self.scale * extra_scale;
        if self.rotation_degrees == 0.0 {
            return (scale, 0.0, 0.0, scale);
        }
        let radians = self.rotation_degrees * std::f32::consts::PI / 180.0;
        let (sine, cosine) = radians.sin_cos();
        (cosine * scale, -sine * scale, sine * scale, cosine * scale)
    }

    /// Returns `[xx, yx, xy, yy, dx, dy]`, rotating/scaling around `center`.
    pub fn affine(self, center_x: f32, center_y: f32) -> [f64; 6] {
        self.centered_affine(center_x, center_y, center_x, center_y, 1.0)
    }

    /// Returns an affine transform mapping a source center to a target center.
    fn centered_affine(
        self,
        target_x: f32,
        target_y: f32,
        source_x: f32,
        source_y: f32,
        extra_scale: f32,
    ) -> [f64; 6] {
        let (xx, xy, yx, yy) = self.linear_components(extra_scale);
        let dx = self.dx + target_x - (xx * source_x + xy * source_y);
        let dy = self.dy + target_y - (yx * source_x + yy * source_y);
        [
            xx as f64, yx as f64, xy as f64, yy as f64, dx as f64, dy as f64,
        ]
    }

    pub fn fit_affine(self, target: &RectData, source_width: f32, source_height: f32) -> [f64; 6] {
        let fit_scale = (target.width / source_width).min(target.height / source_height);
        self.centered_affine(
            target.x + target.width * 0.5,
            target.y + target.height * 0.5,
            source_width * 0.5,
            source_height * 0.5,
            fit_scale,
        )
    }
}

pub fn generate_rects(seed: u64, frame_index: u32, width: u32, height: u32) -> Vec<RectData> {
    let combined_seed = seed ^ u64::from(frame_index).wrapping_mul(2_654_435_761);
    let mut rng = Pcg32::new(combined_seed);
    (0..DEFAULT_OBJECT_COUNT)
        .map(|_| {
            let rect_width = rng.next_float(10.0, 200.0);
            let rect_height = rng.next_float(10.0, 200.0);
            RectData {
                x: rng.next_float(-rect_width * 0.5, width as f32 - rect_width * 0.5),
                y: rng.next_float(-rect_height * 0.5, height as f32 - rect_height * 0.5),
                width: rect_width,
                height: rect_height,
                rgba: [
                    rng.next_u8(0, 255),
                    rng.next_u8(0, 255),
                    rng.next_u8(0, 255),
                    rng.next_u8(240, 255),
                ],
            }
        })
        .collect()
}

pub fn generate_circles(seed: u64, frame_index: u32, width: u32, height: u32) -> Vec<CircleData> {
    let combined_seed = seed ^ u64::from(frame_index).wrapping_mul(2_654_435_761);
    let mut rng = Pcg32::new(combined_seed);
    (0..DEFAULT_OBJECT_COUNT)
        .map(|_| CircleData {
            radius: rng.next_float(10.0, 200.0) * 0.5,
            center_x: rng.next_float(0.0, width as f32),
            center_y: rng.next_float(0.0, height as f32),
            rgba: [
                rng.next_u8(0, 255),
                rng.next_u8(0, 255),
                rng.next_u8(0, 255),
                rng.next_u8(240, 255),
            ],
        })
        .collect()
}

fn fill_transforms(
    transforms: &mut Vec<TransformData>,
    seed: u64,
    frame_index: u32,
    shape_count: u32,
    allow_rotation: bool,
) {
    let combined_seed = seed ^ u64::from(frame_index).wrapping_mul(1_664_525) ^ 0xDEAD_BEEF;
    let mut rng = Pcg32::new(combined_seed);
    let maximum_rotation = if allow_rotation { 90.0 } else { 0.0 };
    transforms.clear();
    transforms.extend((0..shape_count).map(|_| TransformData {
        dx: rng.next_float(-50.0, 50.0),
        dy: rng.next_float(-50.0, 50.0),
        rotation_degrees: rng.next_float(-maximum_rotation, maximum_rotation),
        scale: rng.next_float(0.9, 1.1),
    }));
}

#[derive(Clone, Debug)]
pub struct BenchmarkScene {
    pub rects: Vec<RectData>,
    pub circles: Vec<CircleData>,
    pub transforms: Vec<TransformData>,
    pub workload: Workload,
    pub scene_mode: SceneMode,
    seed: u64,
}

impl BenchmarkScene {
    pub fn new_with_size(opts: &CliOptions, width: u32, height: u32) -> Self {
        let (rects, circles) = if opts.benchmark.uses_circles() {
            (Vec::new(), generate_circles(opts.seed, 0, width, height))
        } else {
            (generate_rects(opts.seed, 0, width, height), Vec::new())
        };
        Self {
            rects,
            circles,
            transforms: Vec::with_capacity(DEFAULT_OBJECT_COUNT as usize),
            workload: opts.benchmark,
            scene_mode: opts.scene_mode,
            seed: opts.seed,
        }
    }

    pub fn update(&mut self, frame_index: u32) {
        let allow_rotation =
            self.scene_mode == SceneMode::Rotation && !self.workload.uses_circles();
        fill_transforms(
            &mut self.transforms,
            self.seed,
            frame_index,
            DEFAULT_OBJECT_COUNT,
            allow_rotation,
        );
    }
}

#[derive(Clone, Debug, Serialize)]
pub struct BenchmarkStats {
    pub avg_ms: f64,
    pub median_ms: f64,
    pub p95_ms: f64,
    pub p99_ms: f64,
    pub min_ms: f64,
    pub max_ms: f64,
    pub stddev_ms: f64,
    pub fps: f64,
}

pub fn compute_stats(samples: &[f64]) -> BenchmarkStats {
    let mut sorted = samples.to_vec();
    sorted.sort_by(f64::total_cmp);
    let average = sorted.iter().sum::<f64>() / sorted.len() as f64;
    let variance = sorted
        .iter()
        .map(|sample| (sample - average).powi(2))
        .sum::<f64>()
        / sorted.len() as f64;
    BenchmarkStats {
        avg_ms: average,
        median_ms: percentile(&sorted, 50.0),
        p95_ms: percentile(&sorted, 95.0),
        p99_ms: percentile(&sorted, 99.0),
        min_ms: sorted[0],
        max_ms: sorted[sorted.len() - 1],
        stddev_ms: variance.sqrt(),
        fps: if average > 0.0 {
            1_000.0 / average
        } else {
            0.0
        },
    }
}

fn percentile(sorted: &[f64], percentile: f64) -> f64 {
    if sorted.len() == 1 {
        return sorted[0];
    }
    let index = percentile / 100.0 * (sorted.len() - 1) as f64;
    let lower = index.floor() as usize;
    let upper = index.ceil() as usize;
    sorted[lower] * (1.0 - index.fract()) + sorted[upper] * index.fract()
}

#[derive(Clone, Debug)]
pub struct RuntimeMetadata {
    pub engine: String,
    pub engine_version: String,
    pub engine_revision: String,
    pub scene_model: String,
    pub graphics_api: String,
    pub gpu_device: String,
    pub gpu_vendor: String,
    pub gpu_driver: String,
    pub gpu_completion: String,
    pub actual_width: u32,
    pub actual_height: u32,
    pub pixel_format: String,
    pub vsync_actual: bool,
    pub present_mode: String,
    pub asset_hash: String,
}

#[derive(Clone, Debug, Serialize)]
pub struct BenchmarkMetadata {
    pub benchmark: String,
    pub engine: String,
    pub engine_version: String,
    pub engine_revision: String,
    pub scene_model: String,
    pub backend: String,
    pub graphics_api: String,
    pub gpu_device: String,
    pub gpu_vendor: String,
    pub gpu_driver: String,
    pub gpu_completion: String,
    pub timing_mode: String,
    pub seed: u64,
    pub object_count: u32,
    pub resolution: String,
    pub requested_resolution: String,
    pub pixel_format: String,
    pub vsync_requested: bool,
    pub vsync_actual: bool,
    pub present_mode: String,
    pub scene_mode: String,
    pub warmup_frames: u32,
    pub measured_frames: u32,
    pub build_type: String,
    pub asset_hash: String,
}

#[derive(Clone, Debug, Serialize)]
pub struct BenchmarkResults {
    pub schema_version: u32,
    pub status: String,
    pub comparable: bool,
    pub stats: BenchmarkStats,
    pub frame_times_ms: Vec<f64>,
    pub metadata: BenchmarkMetadata,
}

pub trait BenchmarkDriver {
    /// Pump events before starting the frame timer. `false` means the run was interrupted.
    fn pump_events(&mut self) -> Result<bool, String>;
    fn update(&mut self, frame_index: u32) -> Result<(), String>;
    fn render(&mut self) -> Result<(), String>;
    fn present(&mut self) -> Result<(), String>;
    fn finish_gpu(&mut self) -> Result<(), String>;
    fn capture(&mut self, path: &Path) -> Result<(), String>;
    fn runtime_metadata(&self) -> &RuntimeMetadata;
}

pub fn run_benchmark<D: BenchmarkDriver>(
    opts: &CliOptions,
    driver: &mut D,
) -> Result<PathBuf, String> {
    let actual_size = {
        let runtime = driver.runtime_metadata();
        (runtime.actual_width, runtime.actual_height)
    };
    if actual_size != (opts.width, opts.height) {
        return Err(format!(
            "requested drawable {}x{}, but created {}x{}; exact drawable size is required",
            opts.width, opts.height, actual_size.0, actual_size.1
        ));
    }
    let total_frames = opts
        .warmup
        .checked_add(opts.frames)
        .ok_or_else(|| "warmup + measured frame count overflowed u32".to_string())?;
    let mut frame_times = Vec::with_capacity(opts.frames as usize);

    if let Some(path) = &opts.capture_path {
        if !driver.pump_events()? {
            return Err("window closed before correctness capture".into());
        }
        driver.update(0)?;
        driver.render()?;
        driver.capture(path)?;
        // Release/present any acquired on-screen surface only after capturing
        // the exact frame-0 render target.
        driver.present()?;
    }

    println!(
        "Running {} warmup frames + {} measured frames...",
        opts.warmup, opts.frames
    );
    for frame_index in 0..total_frames {
        if !driver.pump_events()? {
            return Err(format!(
                "window closed after {frame_index}/{total_frames} frames; partial result rejected"
            ));
        }
        let started = Instant::now();
        driver.update(frame_index)?;
        driver.render()?;
        driver.present()?;
        if opts.gpu_sync {
            driver.finish_gpu()?;
        }
        if frame_index >= opts.warmup {
            frame_times.push(started.elapsed().as_secs_f64() * 1_000.0);
        }
    }
    let runtime = driver.runtime_metadata();
    let stats = compute_stats(&frame_times);
    let comparable = opts.gpu_sync
        && !opts.vsync
        && !runtime.vsync_actual
        && !cfg!(debug_assertions)
        && !is_software_gpu(runtime);
    let metadata = metadata(opts, runtime, frame_times.len() as u32);
    let results = BenchmarkResults {
        schema_version: 2,
        status: if runtime.vsync_actual {
            "vsync-limited"
        } else {
            "passed"
        }
        .into(),
        comparable,
        stats,
        frame_times_ms: frame_times,
        metadata,
    };
    print_stats(&results);
    write_results(opts, &results)
}

fn is_software_gpu(runtime: &RuntimeMetadata) -> bool {
    let description = format!(
        "{} {} {}",
        runtime.gpu_device, runtime.gpu_vendor, runtime.gpu_driver
    )
    .to_lowercase();
    [
        "llvmpipe",
        "lavapipe",
        "softpipe",
        "swrast",
        "swiftshader",
        "software rasterizer",
        "microsoft basic render driver",
        "direct3d warp",
        "device_type=cpu",
        "[cpu]",
    ]
    .iter()
    .any(|marker| description.contains(marker))
}

fn metadata(
    opts: &CliOptions,
    runtime: &RuntimeMetadata,
    measured_frames: u32,
) -> BenchmarkMetadata {
    BenchmarkMetadata {
        benchmark: opts.benchmark.as_str().into(),
        engine: runtime.engine.clone(),
        engine_version: runtime.engine_version.clone(),
        engine_revision: runtime.engine_revision.clone(),
        scene_model: runtime.scene_model.clone(),
        backend: opts.backend.as_str().into(),
        graphics_api: runtime.graphics_api.clone(),
        gpu_device: runtime.gpu_device.clone(),
        gpu_vendor: runtime.gpu_vendor.clone(),
        gpu_driver: runtime.gpu_driver.clone(),
        gpu_completion: runtime.gpu_completion.clone(),
        timing_mode: if opts.gpu_sync {
            "onscreen_gpu_complete"
        } else {
            "diagnostic_onscreen_gpu_async"
        }
        .into(),
        seed: opts.seed,
        object_count: DEFAULT_OBJECT_COUNT,
        resolution: format!("{}x{}", runtime.actual_width, runtime.actual_height),
        requested_resolution: format!("{}x{}", opts.width, opts.height),
        pixel_format: runtime.pixel_format.clone(),
        vsync_requested: opts.vsync,
        vsync_actual: runtime.vsync_actual,
        present_mode: runtime.present_mode.clone(),
        scene_mode: opts.scene_mode.as_str().into(),
        warmup_frames: opts.warmup,
        measured_frames,
        build_type: if cfg!(debug_assertions) {
            "debug"
        } else {
            "release"
        }
        .into(),
        asset_hash: runtime.asset_hash.clone(),
    }
}

fn print_stats(results: &BenchmarkResults) {
    let stats = &results.stats;
    println!("Frames: {}", results.frame_times_ms.len());
    println!("Avg:    {:.6} ms", stats.avg_ms);
    println!("Median: {:.6} ms", stats.median_ms);
    println!("P95:    {:.6} ms", stats.p95_ms);
    println!("P99:    {:.6} ms", stats.p99_ms);
    println!("Min:    {:.6} ms", stats.min_ms);
    println!("Max:    {:.6} ms", stats.max_ms);
    println!("StdDev: {:.6} ms", stats.stddev_ms);
    println!("FPS:    {:.3}", stats.fps);
    println!("Comparable: {}", results.comparable);
}

fn write_results(opts: &CliOptions, results: &BenchmarkResults) -> Result<PathBuf, String> {
    let output_path = opts.output_path.clone().unwrap_or_else(|| {
        let seconds = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs();
        format!(
            "results_{}_{}_{}_{}.json",
            results.metadata.engine, results.metadata.backend, results.metadata.benchmark, seconds
        )
        .into()
    });
    if let Some(parent) = output_path
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
    {
        fs::create_dir_all(parent)
            .map_err(|error| format!("failed to create {}: {error}", parent.display()))?;
    }
    let file = File::create(&output_path)
        .map_err(|error| format!("failed to create {}: {error}", output_path.display()))?;
    let mut writer = BufWriter::new(file);
    serde_json::to_writer_pretty(&mut writer, results)
        .map_err(|error| format!("failed to encode results: {error}"))?;
    writer
        .write_all(b"\n")
        .map_err(|error| format!("failed to finish {}: {error}", output_path.display()))?;
    writer
        .flush()
        .map_err(|error| format!("failed to flush {}: {error}", output_path.display()))?;
    println!("Results written to: {}", output_path.display());
    Ok(output_path)
}

pub fn locate_image(extension: &str) -> Result<PathBuf, String> {
    let filename = format!("test.{extension}");
    let candidates = [
        PathBuf::from("resource/image").join(&filename),
        PathBuf::from("../resource/image").join(&filename),
        PathBuf::from("../../resource/image").join(&filename),
    ];
    candidates
        .into_iter()
        .find(|path| path.is_file())
        .ok_or_else(|| format!("could not locate resource/image/{filename}"))
}

pub fn sha256_file(path: &Path) -> Result<String, String> {
    let bytes =
        fs::read(path).map_err(|error| format!("failed to read {}: {error}", path.display()))?;
    Ok(format!("sha256:{:x}", Sha256::digest(bytes)))
}

pub fn write_ppm(path: &Path, rgba: &[u8], width: u32, height: u32) -> Result<(), String> {
    let expected_len = width as usize * height as usize * 4;
    if rgba.len() != expected_len {
        return Err(format!(
            "RGBA capture has {} bytes, expected {expected_len}",
            rgba.len()
        ));
    }
    if let Some(parent) = path
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
    {
        fs::create_dir_all(parent)
            .map_err(|error| format!("failed to create {}: {error}", parent.display()))?;
    }
    let file = File::create(path)
        .map_err(|error| format!("failed to create {}: {error}", path.display()))?;
    let mut writer = BufWriter::new(file);
    write!(writer, "P6\n{width} {height}\n255\n")
        .map_err(|error| format!("failed to write {} header: {error}", path.display()))?;
    for pixel in rgba.chunks_exact(4) {
        writer
            .write_all(&pixel[..3])
            .map_err(|error| format!("failed to write {} pixels: {error}", path.display()))?;
    }
    writer
        .flush()
        .map_err(|error| format!("failed to flush {}: {error}", path.display()))
}

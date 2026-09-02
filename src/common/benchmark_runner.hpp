#pragma once

#include "benchmark_window.hpp"
#include "cli_parser.hpp"
#include "json_writer.hpp"
#include "rect_generator.hpp"
#include "stats.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace bench {

inline bool is_software_gpu(const BenchmarkMetadata &meta) {
  std::string description =
      meta.gpu_device + " " + meta.gpu_vendor + " " + meta.gpu_driver;
  std::transform(description.begin(), description.end(), description.begin(),
                 [](unsigned char value) {
                   return static_cast<char>(std::tolower(value));
                 });
  for (const char *marker :
       {"llvmpipe", "lavapipe", "softpipe", "swrast", "swiftshader",
        "software rasterizer", "microsoft basic render driver",
        "direct3d warp", "device_type=cpu", "[cpu]"}) {
    if (description.find(marker) != std::string::npos) return true;
  }
  return false;
}

#ifndef VGBENCH_BUILD_TYPE
#define VGBENCH_BUILD_TYPE "unknown"
#endif

inline int run_benchmark(const bench::CliOptions &opts,
                         bench::BenchmarkWindow &window) {
  if (opts.frames == 0) {
    std::cerr << "Measured frame count must be greater than zero.\n";
    return 1;
  }
  if (window.surface_width() != opts.width ||
      window.surface_height() != opts.height) {
    std::cerr << "Requested drawable " << opts.width << "x" << opts.height
              << " but adapter created " << window.surface_width() << "x"
              << window.surface_height() << ".\n";
    return 1;
  }
  if (!window.ready()) {
    std::cerr << "Failed to prepare initial " << window.engine_title()
              << " scene.\n";
    return 1;
  }

  // ready() prepares a frame for optional correctness capture. Drain that
  // untimed submission so a zero-warmup run cannot charge it to sample 0.
  if (opts.gpu_sync && !window.finish_gpu()) {
    std::cerr << window.engine_title()
              << " initial GPU completion failed before timing.\n";
    return 1;
  }

  if (!opts.capture_path.empty()) {
    if (!window.capture(opts.capture_path)) {
      std::cerr << "Failed to capture prepared frame to: "
                << opts.capture_path << "\n";
      return 1;
    }
  }

  std::vector<double> frame_times;
  frame_times.reserve(opts.frames);

  if (opts.warmup > std::numeric_limits<uint32_t>::max() - opts.frames) {
    std::cerr << "Warmup + measured frame count exceeds uint32_t.\n";
    return 1;
  }
  const uint32_t total_frames = opts.warmup + opts.frames;

  std::cout << "Running " << opts.warmup << " warmup frames + " << opts.frames
            << " measured frames...\n";

  bool running = true;
  uint32_t frame_index = 0;

  while (running && frame_index < total_frames) {
    window.pump_events(running);

    auto start_time = std::chrono::steady_clock::now();

    if (!window.update(frame_index)) {
      std::cerr << window.engine_title() << " update failed.\n";
      return 1;
    }

    if (!window.draw()) {
      std::cerr << window.engine_title() << " draw/sync failed.\n";
      return 1;
    }

    window.refresh();

    if (!window.finish_gpu()) {
      std::cerr << window.engine_title() << " GPU completion failed.\n";
      return 1;
    }

    auto end_time = std::chrono::steady_clock::now();

    if (frame_index >= opts.warmup) {
      const double frame_ms =
          std::chrono::duration<double, std::milli>(end_time - start_time)
              .count();
      frame_times.push_back(frame_ms);
    }

    ++frame_index;
  }

  if (!running || frame_times.size() != opts.frames) {
    std::cerr << "Benchmark ended before all requested frames completed ("
              << frame_times.size() << "/" << opts.frames << ").\n";
    return 1;
  }

  bench::BenchmarkStats stats = bench::compute_stats(frame_times);

  std::cout << "\n=== " << window.engine_title() << " " << window.backend_title()
            << " Benchmark Results ===\n";
  std::cout << "Frames: " << frame_times.size() << "\n";
  std::cout << "Avg:    " << stats.avg_ms << " ms\n";
  std::cout << "Median: " << stats.median_ms << " ms\n";
  std::cout << "P95:    " << stats.p95_ms << " ms\n";
  std::cout << "P99:    " << stats.p99_ms << " ms\n";
  std::cout << "Min:    " << stats.min_ms << " ms\n";
  std::cout << "Max:    " << stats.max_ms << " ms\n";
  std::cout << "StdDev: " << stats.stddev_ms << " ms\n";
  std::cout << "FPS:    " << stats.fps << "\n";

  bench::BenchmarkMetadata meta;
  const bool gpu_backend = opts.backend != bench::Backend::CPU;
  meta.benchmark = opts.benchmark.empty() ? "unknown" : opts.benchmark;
  meta.engine = window.engine_id();
  meta.engine_version = window.engine_version();
  meta.engine_revision = window.engine_revision();
  meta.scene_model = window.scene_model();
  meta.backend = window.backend_id();
  meta.graphics_api = window.graphics_api();
  meta.gpu_device = window.gpu_device();
  meta.gpu_vendor = window.gpu_vendor();
  meta.gpu_driver = window.gpu_driver();
  meta.gpu_completion = window.gpu_completion();
  meta.timing_mode = !gpu_backend     ? "onscreen_cpu_complete"
                     : opts.gpu_sync ? "onscreen_gpu_complete"
                                     : "diagnostic_submit";
  meta.seed = opts.seed;
  meta.object_count = bench::RectGenConfig{}.rect_count;
  meta.width = window.surface_width();
  meta.height = window.surface_height();
  meta.requested_width = opts.width;
  meta.requested_height = opts.height;
  meta.pixel_format = window.pixel_format();
  meta.vsync_requested = opts.vsync;
  meta.vsync_actual = window.vsync_actual();
  meta.present_mode = window.present_mode();
  meta.scene_mode = bench::scene_mode_name(opts.scene_mode);
  meta.warmup_frames = opts.warmup;
  meta.measured_frames = static_cast<uint32_t>(frame_times.size());
  meta.build_type = VGBENCH_BUILD_TYPE;
  if (meta.build_type != "Release") meta.comparable = false;
  if (meta.benchmark == "image") {
    meta.asset_hash = window.asset_hash();
    if (meta.asset_hash.empty()) {
      std::cerr << "Image adapter did not report a hash of the loaded asset.\n";
      return 1;
    }
  }

  if (opts.vsync || window.vsync_actual() || !window.vsync_verified()) {
    meta.status = "vsync-limited";
    meta.comparable = false;
  }
  if (gpu_backend && (!opts.gpu_sync ||
                      std::string(window.gpu_completion()) == "none")) {
    meta.comparable = false;
  }
  if (gpu_backend && is_software_gpu(meta)) {
    meta.comparable = false;
  }

  const std::string output_path = opts.output_path.empty()
                                      ? bench::get_default_output_path(meta)
                                      : opts.output_path;

  if (bench::write_results(output_path, stats, meta, frame_times)) {
    std::cout << "Results written to: " << output_path << "\n";
  } else {
    std::cerr << "Failed to write results to: " << output_path << "\n";
    return 1;
  }

  return 0;
}

} // namespace bench

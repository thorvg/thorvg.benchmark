#pragma once

#include "benchmark_window.hpp"
#include "cli_parser.hpp"
#include "json_writer.hpp"
#include "rect_generator.hpp"
#include "stats.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace bench {

inline int run_benchmark(const bench::CliOptions &opts,
                         bench::BenchmarkWindow &window) {
  if (!window.ready()) {
    std::cerr << "Failed to prepare initial " << window.engine_title()
              << " scene.\n";
    return 1;
  }

  std::vector<double> frame_times;
  frame_times.reserve(opts.frames);

  const uint32_t total_frames = opts.warmup + opts.frames;

  std::cout << "Running " << opts.warmup << " warmup frames + " << opts.frames
            << " measured frames...\n";

  bool running = true;
  uint32_t frame_index = 0;

  while (running && frame_index < total_frames) {
    window.pump_events(running);

    auto start_time = std::chrono::high_resolution_clock::now();

    if (!window.update(frame_index)) {
      std::cerr << window.engine_title() << " update failed.\n";
      break;
    }

    if (!window.draw()) {
      std::cerr << window.engine_title() << " draw/sync failed.\n";
      break;
    }

    window.refresh();

    auto end_time = std::chrono::high_resolution_clock::now();

    if (frame_index >= opts.warmup) {
      const double frame_ms =
          std::chrono::duration<double, std::milli>(end_time - start_time)
              .count();
      frame_times.push_back(frame_ms);
    }

    ++frame_index;
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
  meta.engine = window.engine_id();
  meta.backend = window.backend_id();
  meta.seed = opts.seed;
  meta.rect_count = bench::RectGenConfig{}.rect_count;
  meta.width = window.surface_width();
  meta.height = window.surface_height();
  meta.vsync = opts.vsync;
  meta.scene_mode = bench::scene_mode_name(opts.scene_mode);
  meta.warmup_frames = opts.warmup;
  meta.measured_frames = static_cast<uint32_t>(frame_times.size());

  const std::string output_path = opts.output_path.empty()
                                      ? bench::get_default_output_path(meta)
                                      : opts.output_path;

  if (bench::write_results(output_path, stats, meta)) {
    std::cout << "Results written to: " << output_path << "\n";
  } else {
    std::cerr << "Failed to write results to: " << output_path << "\n";
  }

  return 0;
}

} // namespace bench


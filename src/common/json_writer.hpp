#pragma once

#include "stats.hpp"
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace bench {

/// Metadata for benchmark results
struct BenchmarkMetadata {
  std::string status = "passed";
  bool comparable = true;
  std::string benchmark;
  std::string engine;
  std::string engine_version = "unknown";
  std::string engine_revision = "unknown";
  std::string scene_model = "unknown";
  std::string backend;
  std::string graphics_api = "unknown";
  std::string gpu_device = "unknown";
  std::string gpu_vendor = "unknown";
  std::string gpu_driver = "unknown";
  std::string gpu_completion = "none";
  std::string timing_mode = "onscreen_gpu_complete";
  uint64_t seed = 12345;
  uint32_t object_count = 5000;
  uint32_t width = 2560;
  uint32_t height = 1440;
  uint32_t requested_width = 2560;
  uint32_t requested_height = 1440;
  std::string pixel_format = "unknown";
  bool vsync_requested = false;
  bool vsync_actual = false;
  std::string present_mode = "unknown";
  std::string scene_mode; // "default" or "rotation"
  uint32_t warmup_frames = 0;
  uint32_t measured_frames = 0;
  std::string build_type = "unknown";
  std::string asset_hash;
};

/// Generate timestamp string for filenames
inline std::string get_timestamp() {
  auto now = std::time(nullptr);
  auto *tm = std::localtime(&now);
  std::ostringstream oss;
  oss << std::put_time(tm, "%Y%m%d_%H%M%S");
  return oss.str();
}

/// Generate default output filename
inline std::string get_default_output_path(const BenchmarkMetadata &meta) {
  std::ostringstream oss;
  oss << "./results_" << meta.benchmark << "_" << meta.engine << "_"
      << meta.backend << "_" << meta.scene_mode << "_" << get_timestamp()
      << ".json";
  return oss.str();
}

/// Escape a string for JSON (minimal escaping)
inline std::string json_escape(const std::string &s) {
  std::string result;
  result.reserve(s.size());
  for (char c : s) {
    switch (c) {
    case '"':
      result += "\\\"";
      break;
    case '\\':
      result += "\\\\";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      result += c;
      break;
    }
  }
  return result;
}

/// Write benchmark results to JSON file
inline bool write_results(const std::string &path, const BenchmarkStats &stats,
                          const BenchmarkMetadata &meta,
                          const std::vector<double> &frame_times_ms = {}) {
  std::ofstream file(path);
  if (!file.is_open()) {
    return false;
  }

  file << std::fixed << std::setprecision(6);

  file << "{\n";
  file << "  \"schema_version\": 2,\n";
  file << "  \"status\": \"" << json_escape(meta.status) << "\",\n";
  file << "  \"comparable\": " << (meta.comparable ? "true" : "false")
       << ",\n";

  // Statistics
  file << "  \"stats\": {\n";
  file << "    \"avg_ms\": " << stats.avg_ms << ",\n";
  file << "    \"median_ms\": " << stats.median_ms << ",\n";
  file << "    \"p95_ms\": " << stats.p95_ms << ",\n";
  file << "    \"p99_ms\": " << stats.p99_ms << ",\n";
  file << "    \"min_ms\": " << stats.min_ms << ",\n";
  file << "    \"max_ms\": " << stats.max_ms << ",\n";
  file << "    \"stddev_ms\": " << stats.stddev_ms << ",\n";
  file << "    \"fps\": " << stats.fps << "\n";
  file << "  },\n";

  file << "  \"frame_times_ms\": [";
  for (size_t i = 0; i < frame_times_ms.size(); ++i) {
    if (i != 0) file << ", ";
    file << frame_times_ms[i];
  }
  file << "],\n";

  // Metadata
  file << "  \"metadata\": {\n";
  file << "    \"benchmark\": \"" << json_escape(meta.benchmark) << "\",\n";
  file << "    \"engine\": \"" << json_escape(meta.engine) << "\",\n";
  file << "    \"engine_version\": \"" << json_escape(meta.engine_version)
       << "\",\n";
  file << "    \"engine_revision\": \"" << json_escape(meta.engine_revision)
       << "\",\n";
  file << "    \"scene_model\": \"" << json_escape(meta.scene_model)
       << "\",\n";
  file << "    \"backend\": \"" << json_escape(meta.backend) << "\",\n";
  file << "    \"graphics_api\": \"" << json_escape(meta.graphics_api)
       << "\",\n";
  file << "    \"gpu_device\": \"" << json_escape(meta.gpu_device) << "\",\n";
  file << "    \"gpu_vendor\": \"" << json_escape(meta.gpu_vendor) << "\",\n";
  file << "    \"gpu_driver\": \"" << json_escape(meta.gpu_driver) << "\",\n";
  file << "    \"gpu_completion\": \"" << json_escape(meta.gpu_completion)
       << "\",\n";
  file << "    \"timing_mode\": \"" << json_escape(meta.timing_mode) << "\",\n";
  file << "    \"seed\": " << meta.seed << ",\n";
  file << "    \"object_count\": " << meta.object_count << ",\n";
  file << "    \"resolution\": \"" << meta.width << "x" << meta.height
       << "\",\n";
  file << "    \"requested_resolution\": \"" << meta.requested_width << "x"
       << meta.requested_height << "\",\n";
  file << "    \"pixel_format\": \"" << json_escape(meta.pixel_format)
       << "\",\n";
  file << "    \"vsync_requested\": "
       << (meta.vsync_requested ? "true" : "false") << ",\n";
  file << "    \"vsync_actual\": " << (meta.vsync_actual ? "true" : "false")
       << ",\n";
  file << "    \"present_mode\": \"" << json_escape(meta.present_mode)
       << "\",\n";
  file << "    \"scene_mode\": \"" << json_escape(meta.scene_mode) << "\",\n";
  file << "    \"warmup_frames\": " << meta.warmup_frames << ",\n";
  file << "    \"measured_frames\": " << meta.measured_frames << ",\n";
  file << "    \"build_type\": \"" << json_escape(meta.build_type) << "\",\n";
  file << "    \"asset_hash\": \"" << json_escape(meta.asset_hash) << "\"\n";
  file << "  }\n";
  file << "}\n";

  return file.good();
}

} // namespace bench

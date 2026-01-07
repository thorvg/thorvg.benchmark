#pragma once

#include "stats.hpp"
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace bench {

/// Metadata for benchmark results
struct BenchmarkMetadata {
  std::string engine;  // "skia" or "thorvg"
  std::string backend; // "cpu", "gl", "webgpu"
  uint64_t seed = 12345;
  uint32_t rect_count = 5000;
  uint32_t width = 2560;
  uint32_t height = 1440;
  bool vsync = false;
  std::string scene_mode; // "default" or "rotation"
  uint32_t warmup_frames = 0;
  uint32_t measured_frames = 0;
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
  oss << "./results_" << meta.engine << "_" << meta.backend << "_"
      << get_timestamp() << ".json";
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
                          const BenchmarkMetadata &meta) {
  std::ofstream file(path);
  if (!file.is_open()) {
    return false;
  }

  file << std::fixed << std::setprecision(6);

  file << "{\n";

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

  // Metadata
  file << "  \"metadata\": {\n";
  file << "    \"engine\": \"" << json_escape(meta.engine) << "\",\n";
  file << "    \"backend\": \"" << json_escape(meta.backend) << "\",\n";
  file << "    \"seed\": " << meta.seed << ",\n";
  file << "    \"rect_count\": " << meta.rect_count << ",\n";
  file << "    \"resolution\": \"" << meta.width << "x" << meta.height
       << "\",\n";
  file << "    \"vsync\": " << (meta.vsync ? "true" : "false") << ",\n";
  file << "    \"scene_mode\": \"" << json_escape(meta.scene_mode) << "\",\n";
  file << "    \"warmup_frames\": " << meta.warmup_frames << ",\n";
  file << "    \"measured_frames\": " << meta.measured_frames << "\n";
  file << "  }\n";
  file << "}\n";

  return file.good();
}

} // namespace bench

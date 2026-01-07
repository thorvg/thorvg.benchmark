#pragma once

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace bench {

/// Statistics results structure
struct BenchmarkStats {
  double avg_ms = 0.0;
  double median_ms = 0.0;
  double p95_ms = 0.0;
  double p99_ms = 0.0;
  double min_ms = 0.0;
  double max_ms = 0.0;
  double stddev_ms = 0.0;
  double fps = 0.0;
};

/// Compute percentile from sorted data
inline double percentile(const std::vector<double> &sorted_data, double p) {
  if (sorted_data.empty())
    return 0.0;
  if (sorted_data.size() == 1)
    return sorted_data[0];

  double index = (p / 100.0) * static_cast<double>(sorted_data.size() - 1);
  size_t lower = static_cast<size_t>(std::floor(index));
  size_t upper = static_cast<size_t>(std::ceil(index));

  if (lower == upper || upper >= sorted_data.size()) {
    return sorted_data[lower];
  }

  double fraction = index - static_cast<double>(lower);
  return sorted_data[lower] * (1.0 - fraction) + sorted_data[upper] * fraction;
}

/// Compute benchmark statistics from a vector of frame times in milliseconds
inline BenchmarkStats compute_stats(std::vector<double> times_ms) {
  BenchmarkStats stats;

  if (times_ms.empty()) {
    return stats;
  }

  // Sort for percentile calculations
  std::sort(times_ms.begin(), times_ms.end());

  // Min/Max
  stats.min_ms = times_ms.front();
  stats.max_ms = times_ms.back();

  // Average
  double sum = std::accumulate(times_ms.begin(), times_ms.end(), 0.0);
  stats.avg_ms = sum / static_cast<double>(times_ms.size());

  // Median (P50)
  stats.median_ms = percentile(times_ms, 50.0);

  // P95 and P99
  stats.p95_ms = percentile(times_ms, 95.0);
  stats.p99_ms = percentile(times_ms, 99.0);

  // Standard deviation
  double sq_sum = 0.0;
  for (double t : times_ms) {
    double diff = t - stats.avg_ms;
    sq_sum += diff * diff;
  }
  stats.stddev_ms = std::sqrt(sq_sum / static_cast<double>(times_ms.size()));

  // FPS (based on average frame time)
  if (stats.avg_ms > 0.0) {
    stats.fps = 1000.0 / stats.avg_ms;
  }

  return stats;
}

} // namespace bench

#pragma once

#include "rng.hpp"
#include "transform.hpp"
#include <cstdint>
#include <vector>

namespace bench {

/// Circle data structure with center, radius, and RGBA color
struct CircleData {
  float cx, cy, radius;
  uint8_t r, g, b, a;
};

/// Configuration for circle generation
struct CircleGenConfig {
  uint32_t canvas_width = 1280;
  uint32_t canvas_height = 720;
  uint32_t circle_count = 5000;
  float min_diameter = 10.0f;
  float max_diameter = 200.0f;
  uint8_t alpha_min = 240; // As specified: [240..255] inclusive
  uint8_t alpha_max = 255;
};

/**
 * Generate a list of random circles with deterministic RNG
 *
 * For reusable-circle scenes: call once with seed
 * For per-frame variation: vary frame_index
 */
inline std::vector<CircleData>
generate_circles(uint64_t seed, uint32_t frame_index,
                 const CircleGenConfig &config = CircleGenConfig{}) {
  // Combine seed and frame index for per-frame determinism
  uint64_t combined_seed =
      seed ^ (static_cast<uint64_t>(frame_index) * 2654435761ULL);
  PCG32 rng(combined_seed);

  std::vector<CircleData> circles;
  circles.reserve(config.circle_count);

  const float max_x = static_cast<float>(config.canvas_width);
  const float max_y = static_cast<float>(config.canvas_height);

  for (uint32_t i = 0; i < config.circle_count; ++i) {
    CircleData circle;

    // Random size (diameter)
    const float diameter =
        rng.next_float(config.min_diameter, config.max_diameter);
    circle.radius = diameter * 0.5f;

    // Random center position (can extend partially off-screen)
    circle.cx = rng.next_float(0.0f, max_x);
    circle.cy = rng.next_float(0.0f, max_y);

    // Random RGB color (full range)
    circle.r = rng.next_u8(0, 255);
    circle.g = rng.next_u8(0, 255);
    circle.b = rng.next_u8(0, 255);

    // Alpha in restricted range [240..255] as specified
    circle.a = rng.next_u8(config.alpha_min, config.alpha_max);

    circles.push_back(circle);
  }

  return circles;
}

/// Generate static circles (frame_index = 0)
inline std::vector<CircleData>
generate_static_circles(uint64_t seed,
                        const CircleGenConfig &config = CircleGenConfig{}) {
  return generate_circles(seed, 0, config);
}

} // namespace bench

#pragma once

#include "rng.hpp"
#include "transform.hpp"
#include <cstdint>
#include <vector>

namespace bench {

/// Rectangle data structure with position, size, and RGBA color
struct RectData {
  float x, y, w, h;
  uint8_t r, g, b, a;
};

/// Configuration for rectangle generation
struct RectGenConfig {
  uint32_t canvas_width = 1280;
  uint32_t canvas_height = 720;
  uint32_t rect_count = 5000;
  float min_rect_size = 10.0f;
  float max_rect_size = 200.0f;
  uint8_t alpha_min = 240; // As specified: [240..255] inclusive
  uint8_t alpha_max = 255;
};

/**
 * Generate a list of random rectangles with deterministic RNG
 *
 * For reusable-rect scenes: call once with seed
 * For per-frame variation: vary frame_index
 */
inline std::vector<RectData>
generate_rects(uint64_t seed, uint32_t frame_index,
               const RectGenConfig &config = RectGenConfig{}) {
  // Combine seed and frame index for per-frame determinism
  uint64_t combined_seed =
      seed ^ (static_cast<uint64_t>(frame_index) * 2654435761ULL);
  PCG32 rng(combined_seed);

  std::vector<RectData> rects;
  rects.reserve(config.rect_count);

  const float max_x = static_cast<float>(config.canvas_width);
  const float max_y = static_cast<float>(config.canvas_height);

  for (uint32_t i = 0; i < config.rect_count; ++i) {
    RectData rect;

    // Random size
    rect.w = rng.next_float(config.min_rect_size, config.max_rect_size);
    rect.h = rng.next_float(config.min_rect_size, config.max_rect_size);

    // Random position (can extend partially off-screen)
    rect.x = rng.next_float(-rect.w * 0.5f, max_x - rect.w * 0.5f);
    rect.y = rng.next_float(-rect.h * 0.5f, max_y - rect.h * 0.5f);

    // Random RGB color (full range)
    rect.r = rng.next_u8(0, 255);
    rect.g = rng.next_u8(0, 255);
    rect.b = rng.next_u8(0, 255);

    // Alpha in restricted range [253..255] as specified
    rect.a = rng.next_u8(config.alpha_min, config.alpha_max);

    rects.push_back(rect);
  }

  return rects;
}

/// Generate static rects (frame_index = 0)
inline std::vector<RectData>
generate_static_rects(uint64_t seed,
                      const RectGenConfig &config = RectGenConfig{}) {
  return generate_rects(seed, 0, config);
}

} // namespace bench

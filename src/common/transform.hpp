#pragma once

#include "rng.hpp"
#include <cmath>
#include <cstdint>
#include <vector>

namespace bench {

/// Degrees to radians conversion constant (pi/180)
constexpr float kDegToRad = 0.01745329251994329576923690768489f;

/// Transform data structure for transformed scenes (default/rotation)
struct TransformData {
  float dx, dy;       // Translation offset (bounded)
  float rotation_deg; // Rotation in degrees (bounded)
  float scale;        // Uniform scale factor (bounded)
};

/// Row-major components of a 2D affine transform.
struct Affine2D {
  float a, b, tx;
  float c, d, ty;
};

inline Affine2D linear_transform(const TransformData &transform,
                                 float extra_scale = 1.0f) {
  const float scale = transform.scale * extra_scale;
  if (transform.rotation_deg == 0.0f) {
    return {scale, 0.0f, 0.0f, 0.0f, scale, 0.0f};
  }

  const float radians = transform.rotation_deg * kDegToRad;
  const float cos_theta = std::cos(radians);
  const float sin_theta = std::sin(radians);
  return {cos_theta * scale, -sin_theta * scale, 0.0f,
          sin_theta * scale, cos_theta * scale,  0.0f};
}

inline Affine2D centered_transform(const TransformData &transform,
                                   float target_cx, float target_cy,
                                   float source_cx, float source_cy,
                                   float extra_scale = 1.0f) {
  auto affine = linear_transform(transform, extra_scale);
  affine.tx =
      transform.dx + target_cx - (affine.a * source_cx + affine.b * source_cy);
  affine.ty =
      transform.dy + target_cy - (affine.c * source_cx + affine.d * source_cy);
  return affine;
}

inline Affine2D centered_transform(const TransformData &transform, float cx,
                                   float cy) {
  return centered_transform(transform, cx, cy, cx, cy);
}

/// Configuration for transform generation
struct TransformGenConfig {
  float max_translation = 50.0f;  // Maximum translation in pixels per axis
  float max_rotation_deg = 90.0f; // Maximum rotation in degrees (per rect)
  float min_scale = 0.9f;         // Minimum uniform scale factor
  float max_scale = 1.1f;         // Maximum uniform scale factor
};

/**
 * Generate per-shape transforms for transformed scenes (default/rotation)
 *
 * These transforms are small, bounded per-shape translations/rotations/scales.
 * The transforms are deterministic based on seed and frame_index.
 */
inline std::vector<TransformData>
generate_transforms(uint64_t seed, uint32_t frame_index, uint32_t shape_count,
                    const TransformGenConfig &config = TransformGenConfig{}) {
  // Use a different multiplier than generate_rects to get different RNG stream
  uint64_t combined_seed =
      seed ^ (static_cast<uint64_t>(frame_index) * 1664525ULL) ^ 0xDEADBEEF;
  PCG32 rng(combined_seed);

  std::vector<TransformData> transforms;
  transforms.reserve(shape_count);

  for (uint32_t i = 0; i < shape_count; ++i) {
    TransformData t;
    // Generate small bounded translations / rotations / scales
    t.dx = rng.next_float(-config.max_translation, config.max_translation);
    t.dy = rng.next_float(-config.max_translation, config.max_translation);
    t.rotation_deg =
        rng.next_float(-config.max_rotation_deg, config.max_rotation_deg);
    t.scale = rng.next_float(config.min_scale, config.max_scale);
    transforms.push_back(t);
  }

  return transforms;
}

} // namespace bench

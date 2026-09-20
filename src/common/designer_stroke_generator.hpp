#pragma once

#include "rng.hpp"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

namespace bench {

enum class PathVerb { Move, Line, Cubic, Close };

// Coordinates are normalized to a shape's generated bounding box. Cubics use
// (x1, y1), (x2, y2), then (x3, y3); move and line use (x1, y1).
struct NormalizedPathCommand {
  PathVerb verb;
  float x1, y1;
  float x2, y2;
  float x3, y3;
};

enum class DesignerStrokeKind : uint8_t { Connector, Elbow, SpeechBubble };

struct NormalizedPathTemplate {
  const NormalizedPathCommand *commands;
  size_t command_count;
};

inline constexpr NormalizedPathCommand kConnectorCommands[] = {
    {PathVerb::Move, 0.08f, 0.80f, 0.0f, 0.0f, 0.0f, 0.0f},
    {PathVerb::Cubic, 0.28f, 0.80f, 0.32f, 0.68f, 0.50f, 0.50f},
    {PathVerb::Cubic, 0.68f, 0.32f, 0.72f, 0.20f, 0.92f, 0.20f},
};

inline constexpr NormalizedPathCommand kElbowCommands[] = {
    {PathVerb::Move, 0.08f, 0.20f, 0.0f, 0.0f, 0.0f, 0.0f},
    {PathVerb::Line, 0.38f, 0.20f, 0.0f, 0.0f, 0.0f, 0.0f},
    {PathVerb::Cubic, 0.446274f, 0.20f, 0.50f, 0.253726f, 0.50f, 0.32f},
    {PathVerb::Line, 0.50f, 0.68f, 0.0f, 0.0f, 0.0f, 0.0f},
    {PathVerb::Cubic, 0.50f, 0.746274f, 0.553726f, 0.80f, 0.62f, 0.80f},
    {PathVerb::Line, 0.92f, 0.80f, 0.0f, 0.0f, 0.0f, 0.0f},
};

inline constexpr NormalizedPathCommand kSpeechBubbleCommands[] = {
    {PathVerb::Move, 0.25f, 0.14f, 0.0f, 0.0f, 0.0f, 0.0f},
    {PathVerb::Line, 0.74f, 0.14f, 0.0f, 0.0f, 0.0f, 0.0f},
    {PathVerb::Cubic, 0.88f, 0.14f, 0.94f, 0.24f, 0.94f, 0.37f},
    {PathVerb::Line, 0.94f, 0.60f, 0.0f, 0.0f, 0.0f, 0.0f},
    {PathVerb::Cubic, 0.94f, 0.74f, 0.84f, 0.82f, 0.70f, 0.82f},
    {PathVerb::Line, 0.48f, 0.82f, 0.0f, 0.0f, 0.0f, 0.0f},
    {PathVerb::Line, 0.32f, 0.96f, 0.0f, 0.0f, 0.0f, 0.0f},
    {PathVerb::Line, 0.35f, 0.82f, 0.0f, 0.0f, 0.0f, 0.0f},
    {PathVerb::Line, 0.25f, 0.82f, 0.0f, 0.0f, 0.0f, 0.0f},
    {PathVerb::Cubic, 0.12f, 0.82f, 0.06f, 0.74f, 0.06f, 0.60f},
    {PathVerb::Line, 0.06f, 0.37f, 0.0f, 0.0f, 0.0f, 0.0f},
    {PathVerb::Cubic, 0.06f, 0.24f, 0.12f, 0.14f, 0.25f, 0.14f},
    {PathVerb::Close, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
};

inline constexpr NormalizedPathTemplate kPathTemplates[] = {
    {kConnectorCommands, std::size(kConnectorCommands)},
    {kElbowCommands, std::size(kElbowCommands)},
    {kSpeechBubbleCommands, std::size(kSpeechBubbleCommands)},
};

inline NormalizedPathTemplate path_template(DesignerStrokeKind kind) {
  return kPathTemplates[static_cast<size_t>(kind)];
}

struct DesignerStrokeData {
  float x, y, w, h;
  float stroke_width;
  uint8_t r, g, b;
  DesignerStrokeKind kind;
};

struct DesignerStrokeGenConfig {
  uint32_t canvas_width = 1280;
  uint32_t canvas_height = 720;
  uint32_t shape_count = 5000;
  float min_size = 48.0f;
  float max_size = 200.0f;
  float min_stroke_width = 2.0f;
  float max_stroke_width = 8.0f;
};

inline std::vector<DesignerStrokeData>
generate_designer_strokes(uint64_t seed,
                          const DesignerStrokeGenConfig &config = {}) {
  PCG32 rng(seed);
  std::vector<DesignerStrokeData> shapes;
  shapes.reserve(config.shape_count);

  for (uint32_t i = 0; i < config.shape_count; ++i) {
    DesignerStrokeData shape;
    shape.w = rng.next_float(config.min_size, config.max_size);
    shape.h = rng.next_float(config.min_size, config.max_size);
    shape.x = rng.next_float(-shape.w * 0.5f,
                             static_cast<float>(config.canvas_width) -
                                 shape.w * 0.5f);
    shape.y = rng.next_float(-shape.h * 0.5f,
                             static_cast<float>(config.canvas_height) -
                                 shape.h * 0.5f);
    shape.r = rng.next_u8(0, 255);
    shape.g = rng.next_u8(0, 255);
    shape.b = rng.next_u8(0, 255);
    shape.stroke_width =
        rng.next_float(config.min_stroke_width, config.max_stroke_width);
    shape.kind = static_cast<DesignerStrokeKind>(i % 3);
    shapes.push_back(shape);
  }

  return shapes;
}

inline float path_x(const DesignerStrokeData &shape, float normalized_x) {
  return shape.x + shape.w * normalized_x;
}

inline float path_y(const DesignerStrokeData &shape, float normalized_y) {
  return shape.y + shape.h * normalized_y;
}

} // namespace bench

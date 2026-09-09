#pragma once

#include "rect_generator.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace bench {

// One representative of each of 25 byte-distinct assets in the Kenney pack.
// Keep this order stable: it is part of the cross-engine benchmark input.
inline constexpr std::array<const char *, 25> kMultiImageAssets = {
    "kenney-animal-pack/PNG/Round/elephant.png",
    "kenney-animal-pack/PNG/Round/giraffe.png",
    "kenney-animal-pack/PNG/Round/hippo.png",
    "kenney-animal-pack/PNG/Round/monkey.png",
    "kenney-animal-pack/PNG/Round/panda.png",
    "kenney-animal-pack/PNG/Round/parrot.png",
    "kenney-animal-pack/PNG/Round/penguin.png",
    "kenney-animal-pack/PNG/Round/pig.png",
    "kenney-animal-pack/PNG/Round/rabbit.png",
    "kenney-animal-pack/PNG/Round/snake.png",
    "kenney-animal-pack/PNG/Round (outline)/elephant.png",
    "kenney-animal-pack/PNG/Round (outline)/giraffe.png",
    "kenney-animal-pack/PNG/Round (outline)/hippo.png",
    "kenney-animal-pack/PNG/Round (outline)/monkey.png",
    "kenney-animal-pack/PNG/Round (outline)/panda.png",
    "kenney-animal-pack/PNG/Round (outline)/parrot.png",
    "kenney-animal-pack/PNG/Round (outline)/penguin.png",
    "kenney-animal-pack/PNG/Round (outline)/pig.png",
    "kenney-animal-pack/PNG/Round (outline)/rabbit.png",
    "kenney-animal-pack/PNG/Round (outline)/snake.png",
    "kenney-animal-pack/PNG/Round without details/elephant.png",
    "kenney-animal-pack/PNG/Round without details/giraffe.png",
    "kenney-animal-pack/PNG/Round without details/hippo.png",
    "kenney-animal-pack/PNG/Round without details/monkey.png",
    "kenney-animal-pack/PNG/Round without details/panda.png",
};

inline constexpr uint32_t kMultiImageCount = RectGenConfig{}.rect_count;

inline std::vector<uint32_t> generate_multi_image_indices(uint64_t seed,
                                                           uint32_t count) {
  PCG32 rng(seed, 7);
  std::vector<uint32_t> indices;
  indices.reserve(count);
  for (uint32_t i = 0; i < count; ++i)
    indices.push_back(rng.next_u32() % kMultiImageAssets.size());
  return indices;
}

inline std::vector<RectData> generate_multi_image_layout(uint64_t seed,
                                                         uint32_t width,
                                                         uint32_t height) {
  RectGenConfig config;
  config.canvas_width = width;
  config.canvas_height = height;
  config.alpha_min = 253;
  config.alpha_max = 255;
  return generate_static_rects(seed, config);
}

} // namespace bench

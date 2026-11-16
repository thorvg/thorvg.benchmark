#pragma once

#include "rect_generator.hpp"
#include <array>
#include <string_view>

// Shared semantic mapping; enum integer values differ between engines.
#define BENCH_BLEND_MODES(X) \
  X(normal, Normal, SrcOver) \
  X(multiply, Multiply, Multiply) \
  X(screen, Screen, Screen) \
  X(overlay, Overlay, Overlay) \
  X(darken, Darken, Darken) \
  X(lighten, Lighten, Lighten) \
  X(color_dodge, ColorDodge, ColorDodge) \
  X(color_burn, ColorBurn, ColorBurn) \
  X(hard_light, HardLight, HardLight) \
  X(soft_light, SoftLight, SoftLight) \
  X(difference, Difference, Difference) \
  X(exclusion, Exclusion, Exclusion) \
  X(hue, Hue, Hue) \
  X(saturation, Saturation, Saturation) \
  X(color, Color, Color) \
  X(luminosity, Luminosity, Luminosity) \
  X(add, Add, Plus)

namespace bench {
inline constexpr uint32_t kBlendSquaresPerMode = 250;
inline constexpr std::array<std::string_view, 17> kBlendNames = {
#define BLEND_NAME(name, tvg, skia) #name,
    BENCH_BLEND_MODES(BLEND_NAME)
#undef BLEND_NAME
};

inline constexpr uint32_t kBlendSquareCount =
    kBlendSquaresPerMode * kBlendNames.size();

// Interleave modes so all 17 participate throughout the draw order.
inline constexpr size_t blend_mode_index(size_t square_index) {
  return square_index % kBlendNames.size();
}

inline std::vector<RectData> generate_blend_squares(uint64_t seed,
                                                  RectGenConfig config) {
  config.rect_count = kBlendSquareCount;
  config.alpha_min = 64;
  config.alpha_max = 255;
  auto squares = generate_static_rects(seed, config);
  for (auto &square : squares) square.h = square.w;
  return squares;
}

} // namespace bench

/**
 * Linear Gradient Bench: Skia SDL Benchmark
 *
 * Draws rectangles with linear gradients.
 */

#include "benchmark_runner.hpp"
#include "cli_parser.hpp"
#include "rect_generator.hpp"
#include "skia_sdl_example.hpp"

// Skia headers
#include "core/SkMatrix.h"
#include "core/SkPaint.h"
#include "core/SkRect.h"
#include "effects/SkGradient.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

namespace {

void draw_linear_gradient_rects_skia(
    SkCanvas *canvas, const std::vector<bench::RectData> &rects,
    const std::vector<bench::TransformData> *transforms = nullptr) {
  
  const bool apply_transforms =
      transforms && transforms->size() >= rects.size();

  constexpr float kDegToRad = 0.01745329251994329576923690768489f;

  for (size_t i = 0; i < rects.size(); ++i) {
    const auto &rect = rects[i];

    // Create linear gradient for each rect
    SkPoint pts[2] = {
      {rect.x, rect.y},
      {rect.x + rect.w, rect.y + rect.h}
    };

    SkColor4f colors[2] = {
      SkColor4f::FromColor(SkColorSetARGB((U8CPU)rect.a, (U8CPU)rect.r, (U8CPU)rect.g, (U8CPU)rect.b)),
      SkColor4f::FromColor(SkColorSetARGB((U8CPU)rect.a, 255 - (U8CPU)rect.r, 255 - (U8CPU)rect.g, 255 - (U8CPU)rect.b))
    };

    SkPaint paint;
    paint.setAntiAlias(true);
    SkGradient gradient(SkGradient::Colors(SkSpan(colors, 2), SkTileMode::kClamp), {});
    paint.setShader(SkShaders::LinearGradient(pts, gradient));

    if (!apply_transforms) {
      canvas->drawRect(SkRect::MakeXYWH(rect.x, rect.y, rect.w, rect.h), paint);
      continue;
    }

    const auto &t = (*transforms)[i];

    const float cx = rect.x + rect.w * 0.5f;
    const float cy = rect.y + rect.h * 0.5f;

    float a, b, c, d;
    if (t.rotation_deg == 0.0f) {
      a = t.scale;
      b = 0.0f;
      c = 0.0f;
      d = t.scale;
    } else {
      const float rad = t.rotation_deg * kDegToRad;
      const float cos_theta = std::cos(rad);
      const float sin_theta = std::sin(rad);

      a = cos_theta * t.scale;
      b = -sin_theta * t.scale;
      c = sin_theta * t.scale;
      d = cos_theta * t.scale;
    }

    const float tx = t.dx + cx - (a * cx + b * cy);
    const float ty = t.dy + cy - (c * cx + d * cy);

    SkMatrix m;
    m.setAll(a, b, tx, c, d, ty, 0, 0, 1);

    canvas->save();
    canvas->concat(m);
    canvas->drawRect(SkRect::MakeXYWH(rect.x, rect.y, rect.w, rect.h), paint);
    canvas->restore();
  }
}

class LinearGradientbenchExample final : public bench::skiaexam::Example {
public:
  LinearGradientbenchExample(uint64_t seed, bench::SceneMode scene_mode)
      : seed_(seed), scene_mode_(scene_mode) {}

  bool content(SkCanvas *canvas, uint32_t w, uint32_t h) override {
    (void)canvas;
    rect_config_.canvas_width = w;
    rect_config_.canvas_height = h;

    static_rects_ = bench::generate_static_rects(seed_, rect_config_);

    return true;
  }

  bool update(SkCanvas *canvas, uint32_t elapsed) override {
    (void)canvas;
    const uint32_t frame_index = elapsed;

    bench::TransformGenConfig transform_config;
    if (scene_mode_ == bench::SceneMode::Default) {
      transform_config.max_rotation_deg = 0.0f;
    }

    if (scene_mode_ == bench::SceneMode::Default ||
        scene_mode_ == bench::SceneMode::Rotation) {
      transforms_ = bench::generate_transforms(seed_, frame_index,
                                               rect_config_.rect_count,
                                               transform_config);
      return true;
    }
    return false;
  }

  bool draw(SkCanvas *canvas) override {
    if (!canvas) {
      return false;
    }

    canvas->clear(SK_ColorBLACK);

    const std::vector<bench::TransformData> *transforms_ptr = nullptr;

    if (scene_mode_ == bench::SceneMode::Default ||
        scene_mode_ == bench::SceneMode::Rotation) {
      transforms_ptr = &transforms_;
    }

    draw_linear_gradient_rects_skia(canvas, static_rects_, transforms_ptr);
    return true;
  }

private:
  uint64_t seed_ = 0;
  bench::SceneMode scene_mode_ = bench::SceneMode::Default;
  bench::RectGenConfig rect_config_{};
  std::vector<bench::RectData> static_rects_;
  std::vector<bench::TransformData> transforms_;
};

std::unique_ptr<bench::skiaexam::Window>
make_window_with_example(const bench::CliOptions &opts) {
  auto example = std::make_unique<LinearGradientbenchExample>(opts.seed, opts.scene_mode);
  switch (opts.backend) {
  case bench::Backend::CPU:
    return std::make_unique<bench::skiaexam::SwWindow>(
        example.release(), opts.width, opts.height, "LinearGradientbench");
  case bench::Backend::GL:
    return std::make_unique<bench::skiaexam::GlWindow>(
        example.release(), opts.width, opts.height, opts.vsync, opts.gpu_sync,
        "LinearGradientbench");
  case bench::Backend::WebGPU:
    std::cerr << "Error: Skia WebGPU backend not implemented\n";
    return nullptr;
  }

  return nullptr;
}

int run_benchmark(const bench::CliOptions &opts) {
  auto window = make_window_with_example(opts);
  if (!window || !window->initialized || !window->window || !window->example) {
    return 1;
  }
  return bench::run_benchmark(opts, *window);
}

} // namespace

int main(int argc, char *argv[]) {
  bench::CliOptions opts = bench::parse_cli(argc, argv);

  if (!opts.valid) {
    std::cerr << "Error: " << opts.error_message << "\n";
    bench::print_usage(argv[0]);
    return 1;
  }

  std::cout << "Linear Gradient Bench Skia SDL\n";
  std::cout << "Backend: " << bench::backend_name(opts.backend) << "\n";
  std::cout << "Scene:   " << bench::scene_mode_name(opts.scene_mode) << "\n";
  std::cout << "Seed:    " << opts.seed << "\n";
  std::cout << "VSync:   " << (opts.vsync ? "ON" : "OFF") << "\n";

  return run_benchmark(opts);
}

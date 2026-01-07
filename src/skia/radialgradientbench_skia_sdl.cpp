/**
 * Radial Gradient Bench: Skia SDL Benchmark
 *
 * Draws circles with radial gradients.
 */

#include "benchmark_runner.hpp"
#include "circle_generator.hpp"
#include "cli_parser.hpp"
#include "skia_sdl_example.hpp"

// Skia headers
#include "core/SkMatrix.h"
#include "core/SkPaint.h"
#include "effects/SkGradientShader.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

namespace {

void draw_radial_gradient_circles_skia(
    SkCanvas *canvas, const std::vector<bench::CircleData> &circles,
    const std::vector<bench::TransformData> *transforms = nullptr) {
  
  const bool apply_transforms =
      transforms && transforms->size() >= circles.size();

  constexpr float kDegToRad = 0.01745329251994329576923690768489f;

  for (size_t i = 0; i < circles.size(); ++i) {
    const auto &circle = circles[i];

    // Create radial gradient for each circle
    SkPoint center = {circle.cx, circle.cy};
    
    SkColor colors[2] = {
      SkColorSetARGB(circle.a, circle.r, circle.g, circle.b),
      SkColorSetARGB(circle.a, 255 - circle.r, 255 - circle.g, 255 - circle.b)
    };

    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setShader(SkGradientShader::MakeRadial(center, circle.radius, colors, nullptr, 2, SkTileMode::kClamp));

    if (!apply_transforms) {
      canvas->drawCircle(circle.cx, circle.cy, circle.radius, paint);
      continue;
    }

    const auto &t = (*transforms)[i];

    const float cx = circle.cx;
    const float cy = circle.cy;

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
    canvas->drawCircle(circle.cx, circle.cy, circle.radius, paint);
    canvas->restore();
  }
}

class RadialGradientbenchExample final : public bench::skiaexam::Example {
public:
  RadialGradientbenchExample(uint64_t seed, bench::SceneMode scene_mode)
      : seed_(seed), scene_mode_(scene_mode) {}

  bool content(SkCanvas *canvas, uint32_t w, uint32_t h) override {
    (void)canvas;
    circle_config_.canvas_width = w;
    circle_config_.canvas_height = h;

    static_circles_ = bench::generate_static_circles(seed_, circle_config_);

    return true;
  }

  bool update(SkCanvas *canvas, uint32_t elapsed) override {
    (void)canvas;
    const uint32_t frame_index = elapsed;

    bench::TransformGenConfig transform_config;
    // Circles are rotation-invariant, no rotation needed
    transform_config.max_rotation_deg = 0.0f;

    if (scene_mode_ == bench::SceneMode::Default ||
        scene_mode_ == bench::SceneMode::Rotation) {
      transforms_ = bench::generate_transforms(seed_, frame_index,
                                               circle_config_.circle_count,
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

    draw_radial_gradient_circles_skia(canvas, static_circles_, transforms_ptr);
    return true;
  }

private:
  uint64_t seed_ = 0;
  bench::SceneMode scene_mode_ = bench::SceneMode::Default;
  bench::CircleGenConfig circle_config_{};
  std::vector<bench::CircleData> static_circles_;
  std::vector<bench::TransformData> transforms_;
};

std::unique_ptr<bench::skiaexam::Window>
make_window_with_example(const bench::CliOptions &opts) {
  auto example = std::make_unique<RadialGradientbenchExample>(opts.seed, opts.scene_mode);
  switch (opts.backend) {
  case bench::Backend::CPU:
    return std::make_unique<bench::skiaexam::SwWindow>(
        example.release(), opts.width, opts.height, "RadialGradientbench");
  case bench::Backend::GL:
    return std::make_unique<bench::skiaexam::GlWindow>(
        example.release(), opts.width, opts.height, opts.vsync, opts.gpu_sync,
        "RadialGradientbench");
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

  std::cout << "Radial Gradient Bench Skia SDL\n";
  std::cout << "Backend: " << bench::backend_name(opts.backend) << "\n";
  std::cout << "Scene:   " << bench::scene_mode_name(opts.scene_mode) << "\n";
  std::cout << "Seed:    " << opts.seed << "\n";
  std::cout << "VSync:   " << (opts.vsync ? "ON" : "OFF") << "\n";

  return run_benchmark(opts);
}

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
#include "effects/SkGradient.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

namespace {

void draw_radial_gradient_circles_skia(
    SkCanvas *canvas, const std::vector<bench::CircleData> &circles,
    const std::vector<bench::TransformData> &transforms) {
  for (size_t i = 0; i < circles.size(); ++i) {
    const auto &circle = circles[i];

    // Create radial gradient for each circle
    SkPoint center = {circle.cx, circle.cy};

    SkColor4f colors[2] = {
      SkColor4f::FromColor(SkColorSetARGB((U8CPU)circle.a, (U8CPU)circle.r, (U8CPU)circle.g, (U8CPU)circle.b)),
      SkColor4f::FromColor(SkColorSetARGB((U8CPU)circle.a, 255 - (U8CPU)circle.r, 255 - (U8CPU)circle.g, 255 - (U8CPU)circle.b))
    };

    SkPaint paint;
    paint.setAntiAlias(true);
    SkGradient gradient(SkGradient::Colors(SkSpan(colors, 2), SkTileMode::kClamp), {});
    paint.setShader(SkShaders::RadialGradient(center, circle.radius, gradient));

    const auto affine =
        bench::centered_transform(transforms[i], circle.cx, circle.cy);

    SkMatrix m;
    m.setAll(affine.a, affine.b, affine.tx, affine.c, affine.d, affine.ty, 0, 0,
             1);

    canvas->save();
    canvas->concat(m);
    canvas->drawCircle(circle.cx, circle.cy, circle.radius, paint);
    canvas->restore();
  }
}

class RadialGradientbenchExample final : public bench::skiaexam::Example {
public:
  explicit RadialGradientbenchExample(uint64_t seed) : seed_(seed) {}

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

    transforms_ = bench::generate_transforms(
        seed_, frame_index, circle_config_.circle_count, transform_config);
    return true;
  }

  bool draw(SkCanvas *canvas) override {
    if (!canvas) {
      return false;
    }

    canvas->clear(SK_ColorBLACK);

    draw_radial_gradient_circles_skia(canvas, static_circles_, transforms_);
    return true;
  }

private:
  uint64_t seed_ = 0;
  bench::CircleGenConfig circle_config_{};
  std::vector<bench::CircleData> static_circles_;
  std::vector<bench::TransformData> transforms_;
};

std::unique_ptr<bench::skiaexam::Window>
make_window_with_example(const bench::CliOptions &opts) {
  auto example = std::make_unique<RadialGradientbenchExample>(opts.seed);
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

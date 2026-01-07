/**
 * Circlebench: Skia SDL Benchmark
 *
 * Uses the shared rectbench benchmark runner so Skia and ThorVG execute under
 * the same harness (event pumping, warmup/measured frames, timing, JSON output).
 *
 * Backends:
 * - CPU: raster into SDL window surface + SDL_UpdateWindowSurface()
 * - GL:  Ganesh OpenGL, rendering into the window system framebuffer
 */

#include "benchmark_runner.hpp"
#include "circle_generator.hpp"
#include "cli_parser.hpp"
#include "rect_generator.hpp"
#include "skia_sdl_example.hpp"

// Skia headers (drawing)
#include "core/SkMatrix.h"
#include "core/SkPaint.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

namespace {

void draw_circles_skia(
    SkCanvas *canvas, const std::vector<bench::CircleData> &circles,
    const std::vector<bench::TransformData> *transforms = nullptr) {
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setStyle(SkPaint::kFill_Style);

  const bool apply_transforms =
      transforms && transforms->size() >= circles.size();

  constexpr float kDegToRad =
      0.01745329251994329576923690768489f; // pi/180

  for (size_t i = 0; i < circles.size(); ++i) {
    const auto &circle = circles[i];

    paint.setColor(SkColorSetARGB(circle.a, circle.r, circle.g, circle.b));

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

    // Rotate/scale around circle center, then translate by (dx,dy).
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

class CirclebenchExample final : public bench::skiaexam::Example {
public:
  CirclebenchExample(uint64_t seed, bench::SceneMode scene_mode)
      : seed_(seed), scene_mode_(scene_mode) {}

  bool content(SkCanvas *canvas, uint32_t w, uint32_t h) override {
    (void)canvas;
    circle_config_.canvas_width = w;
    circle_config_.canvas_height = h;

    static_circles_ =
        bench::generate_static_circles(seed_, circle_config_);

    return true;
  }

  bool update(SkCanvas *canvas, uint32_t elapsed) override {
    (void)canvas;
    const uint32_t frame_index = elapsed; // deterministic: use frame index

    bench::TransformGenConfig transform_config;
    transform_config.max_rotation_deg = 0.0f; // Circles are rotation-invariant.

    if (scene_mode_ == bench::SceneMode::Default ||
        scene_mode_ == bench::SceneMode::Rotation) {
      transforms_ =
          bench::generate_transforms(seed_, frame_index,
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

    const std::vector<bench::CircleData> *circles_ptr = nullptr;
    const std::vector<bench::TransformData> *transforms_ptr = nullptr;

    circles_ptr = &static_circles_;
    if (scene_mode_ == bench::SceneMode::Default ||
        scene_mode_ == bench::SceneMode::Rotation) {
      transforms_ptr = &transforms_;
    }

    draw_circles_skia(canvas, *circles_ptr, transforms_ptr);
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
  auto example =
      std::make_unique<CirclebenchExample>(opts.seed, opts.scene_mode);
  switch (opts.backend) {
  case bench::Backend::CPU:
    return std::make_unique<bench::skiaexam::SwWindow>(
        example.release(), opts.width, opts.height, "Circlebench");
  case bench::Backend::GL:
    return std::make_unique<bench::skiaexam::GlWindow>(
        example.release(), opts.width, opts.height, opts.vsync, opts.gpu_sync,
        "Circlebench");
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

  std::cout << "Circlebench Skia SDL\n";
  std::cout << "Backend: " << bench::backend_name(opts.backend) << "\n";
  std::cout << "Scene:   " << bench::scene_mode_name(opts.scene_mode)
            << "\n";
  std::cout << "Seed:    " << opts.seed << "\n";
  std::cout << "VSync:   " << (opts.vsync ? "ON" : "OFF") << "\n";

  return run_benchmark(opts);
}

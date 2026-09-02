/**
 * StrokeRectbench: Skia SDL Benchmark
 *
 * Draws filled rectangles with a white stroke (width=3).
 * Uses the shared benchmark runner so Skia and ThorVG execute under
 * the same harness (event pumping, warmup/measured frames, timing, JSON output).
 *
 * Backends:
 * - CPU: raster into SDL window surface + SDL_UpdateWindowSurface()
 * - GL:  Ganesh OpenGL, rendering into the window system framebuffer
 */

#include "benchmark_runner.hpp"
#include "cli_parser.hpp"
#include "rect_generator.hpp"
#include "skia_sdl_example.hpp"

// Skia headers (drawing)
#include "core/SkMatrix.h"
#include "core/SkPaint.h"
#include "core/SkRect.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

namespace {

void draw_rects_skia(
    SkCanvas *canvas, const std::vector<bench::RectData> &rects,
    const std::vector<bench::TransformData> &transforms) {
  SkPaint fill_paint;
  fill_paint.setAntiAlias(true);
  fill_paint.setStyle(SkPaint::kFill_Style);

  SkPaint stroke_paint;
  stroke_paint.setAntiAlias(true);
  stroke_paint.setStyle(SkPaint::kStroke_Style);
  stroke_paint.setColor(SkColorSetARGB(255, 255, 255, 255));
  stroke_paint.setStrokeWidth(3.0f);
  stroke_paint.setStrokeCap(SkPaint::kButt_Cap);
  stroke_paint.setStrokeJoin(SkPaint::kMiter_Join);
  stroke_paint.setStrokeMiter(4.0f);

  for (size_t i = 0; i < rects.size(); ++i) {
    const auto &rect = rects[i];

    fill_paint.setColor(SkColorSetARGB(rect.a, rect.r, rect.g, rect.b));
    SkRect sk_rect = SkRect::MakeXYWH(rect.x, rect.y, rect.w, rect.h);

    const float cx = rect.x + rect.w * 0.5f;
    const float cy = rect.y + rect.h * 0.5f;
    const auto affine = bench::centered_transform(transforms[i], cx, cy);

    SkMatrix m;
    m.setAll(affine.a, affine.b, affine.tx, affine.c, affine.d, affine.ty, 0, 0,
             1);

    canvas->save();
    canvas->concat(m);
    canvas->drawRect(sk_rect, fill_paint);
    canvas->drawRect(sk_rect, stroke_paint);
    canvas->restore();
  }
}

class StrokeRectbenchExample final : public bench::skiaexam::Example {
public:
  StrokeRectbenchExample(uint64_t seed, bench::SceneMode scene_mode)
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
    const uint32_t frame_index = elapsed; // deterministic: use frame index

    bench::TransformGenConfig transform_config;
    if (scene_mode_ == bench::SceneMode::Default) {
      transform_config.max_rotation_deg = 0.0f;
    }

    transforms_ = bench::generate_transforms(
        seed_, frame_index, rect_config_.rect_count, transform_config);
    return true;
  }

  bool draw(SkCanvas *canvas) override {
    if (!canvas) {
      return false;
    }

    canvas->clear(SK_ColorBLACK);

    draw_rects_skia(canvas, static_rects_, transforms_);
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
  auto example = std::make_unique<StrokeRectbenchExample>(opts.seed, opts.scene_mode);
  switch (opts.backend) {
  case bench::Backend::CPU:
    return std::make_unique<bench::skiaexam::SwWindow>(
        example.release(), opts.width, opts.height, "StrokeRectbench");
  case bench::Backend::GL:
    return std::make_unique<bench::skiaexam::GlWindow>(
        example.release(), opts.width, opts.height, opts.vsync, opts.gpu_sync,
        "StrokeRectbench");
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

  std::cout << "StrokeRectbench Skia SDL\n";
  std::cout << "Backend: " << bench::backend_name(opts.backend) << "\n";
  std::cout << "Scene:   " << bench::scene_mode_name(opts.scene_mode)
            << "\n";
  std::cout << "Seed:    " << opts.seed << "\n";
  std::cout << "VSync:   " << (opts.vsync ? "ON" : "OFF") << "\n";

  return run_benchmark(opts);
}

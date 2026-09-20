/**
 * Strokebench: Skia SDL Benchmark
 *
 * Draws cached designer paths using SkPaint::kStroke_Style.
 */

#include "benchmark_runner.hpp"
#include "cli_parser.hpp"
#include "designer_stroke_generator.hpp"
#include "skia_sdl_example.hpp"

#include "core/SkMatrix.h"
#include "core/SkPaint.h"
#include "core/SkPath.h"
#include "core/SkPathBuilder.h"
#include "core/SkRRect.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

namespace {

SkPath make_path(const bench::DesignerStrokeData &shape) {
  SkPathBuilder builder;
  const auto path_template = bench::path_template(shape.kind);
  for (size_t i = 0; i < path_template.command_count; ++i) {
    const auto &command = path_template.commands[i];
    switch (command.verb) {
    case bench::PathVerb::Move:
      builder.moveTo(bench::path_x(shape, command.x1),
                     bench::path_y(shape, command.y1));
      break;
    case bench::PathVerb::Line:
      builder.lineTo(bench::path_x(shape, command.x1),
                     bench::path_y(shape, command.y1));
      break;
    case bench::PathVerb::Cubic:
      builder.cubicTo(bench::path_x(shape, command.x1),
                      bench::path_y(shape, command.y1),
                      bench::path_x(shape, command.x2),
                      bench::path_y(shape, command.y2),
                      bench::path_x(shape, command.x3),
                      bench::path_y(shape, command.y3));
      break;
    case bench::PathVerb::Close:
      builder.close();
      break;
    }
  }

  SkPath path = builder.detach();
#ifndef NDEBUG
  SkPoint line[2];
  SkRect rect;
  SkRRect rrect;
  SkASSERT(!path.isLine(line));
  SkASSERT(!path.isRect(&rect));
  SkASSERT(!path.isOval(&rect));
  SkASSERT(!path.isRRect(&rrect));
#endif
  return path;
}

void draw_designer_strokes_skia(
    SkCanvas *canvas, const std::vector<bench::DesignerStrokeData> &shapes,
    const std::vector<SkPath> &paths, uint8_t alpha,
    const std::vector<bench::TransformData> &transforms) {
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeCap(SkPaint::kRound_Cap);
  paint.setStrokeJoin(SkPaint::kRound_Join);

  for (size_t i = 0; i < shapes.size(); ++i) {
    const auto &shape = shapes[i];
    const auto &t = transforms[i];
    paint.setColor(SkColorSetARGB(alpha, shape.r, shape.g, shape.b));
    paint.setStrokeWidth(shape.stroke_width);

    const float cx = shape.x + shape.w * 0.5f;
    const float cy = shape.y + shape.h * 0.5f;
    float a, b, c, d;
    if (t.rotation_deg == 0.0f) {
      a = t.scale;
      b = 0.0f;
      c = 0.0f;
      d = t.scale;
    } else {
      const float rad = t.rotation_deg * bench::kDegToRad;
      const float cos_theta = std::cos(rad);
      const float sin_theta = std::sin(rad);
      a = cos_theta * t.scale;
      b = -sin_theta * t.scale;
      c = sin_theta * t.scale;
      d = cos_theta * t.scale;
    }
    const float tx = t.dx + cx - (a * cx + b * cy);
    const float ty = t.dy + cy - (c * cx + d * cy);
    SkMatrix matrix;
    matrix.setAll(a, b, tx, c, d, ty, 0, 0, 1);

    canvas->save();
    canvas->concat(matrix);
    canvas->drawPath(paths[i], paint);
    canvas->restore();
  }
}

class StrokebenchExample final : public bench::skiaexam::Example {
public:
  explicit StrokebenchExample(const bench::CliOptions &opts)
      : seed_(opts.seed), scene_mode_(opts.scene_mode),
        alpha_(static_cast<uint8_t>(std::lround(opts.opacity * 255.0f))) {}

  bool content(SkCanvas *canvas, uint32_t w, uint32_t h) override {
    (void)canvas;
    shape_config_.canvas_width = w;
    shape_config_.canvas_height = h;
    static_shapes_ = bench::generate_designer_strokes(seed_, shape_config_);
    paths_.reserve(static_shapes_.size());
    for (const auto &shape : static_shapes_) {
      paths_.push_back(make_path(shape));
    }
    transforms_.resize(static_shapes_.size(), {0.0f, 0.0f, 0.0f, 1.0f});
    return true;
  }

  bool update(SkCanvas *canvas, uint32_t elapsed) override {
    (void)canvas;
    bench::TransformGenConfig transform_config;
    if (scene_mode_ == bench::SceneMode::Default) {
      transform_config.max_rotation_deg = 0.0f;
    }
    transforms_ = bench::generate_transforms(seed_, elapsed,
                                             shape_config_.shape_count,
                                             transform_config);
    return true;
  }

  bool draw(SkCanvas *canvas) override {
    if (!canvas) {
      return false;
    }
    canvas->clear(SK_ColorBLACK);
    draw_designer_strokes_skia(canvas, static_shapes_, paths_, alpha_,
                               transforms_);
    return true;
  }

private:
  uint64_t seed_ = 0;
  bench::SceneMode scene_mode_ = bench::SceneMode::Default;
  uint8_t alpha_ = 255;
  bench::DesignerStrokeGenConfig shape_config_{};
  std::vector<bench::DesignerStrokeData> static_shapes_;
  std::vector<SkPath> paths_;
  std::vector<bench::TransformData> transforms_;
};

std::unique_ptr<bench::skiaexam::Window>
make_window_with_example(const bench::CliOptions &opts) {
  auto example = std::make_unique<StrokebenchExample>(opts);
  switch (opts.backend) {
  case bench::Backend::CPU:
    return std::make_unique<bench::skiaexam::SwWindow>(
        example.release(), opts.width, opts.height, "Strokebench");
  case bench::Backend::GL:
    return std::make_unique<bench::skiaexam::GlWindow>(
        example.release(), opts.width, opts.height, opts.vsync, opts.gpu_sync,
        "Strokebench");
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
  return bench::run_benchmark(opts, *window, "designer-strokes-v1");
}

} // namespace

int main(int argc, char *argv[]) {
  bench::CliOptions opts = bench::parse_cli(argc, argv);
  if (!opts.valid) {
    std::cerr << "Error: " << opts.error_message << "\n";
    bench::print_usage(argv[0]);
    return 1;
  }

  std::cout << "Strokebench Skia SDL\n";
  std::cout << "Workload: designer-strokes-v1\n";
  std::cout << "Backend: " << bench::backend_name(opts.backend) << "\n";
  std::cout << "Scene:   " << bench::scene_mode_name(opts.scene_mode) << "\n";
  std::cout << "Seed:    " << opts.seed << "\n";
  std::cout << "Opacity: " << opts.opacity << " (alpha "
            << std::lround(opts.opacity * 255.0f) << ")\n";
  std::cout << "VSync:   " << (opts.vsync ? "ON" : "OFF") << "\n";
  return run_benchmark(opts);
}

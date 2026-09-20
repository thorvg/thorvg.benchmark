/**
 * Strokebench: ThorVG SDL Benchmark
 *
 * Draws cached designer paths using ThorVG stroke shapes.
 */

#include "benchmark_runner.hpp"
#include "cli_parser.hpp"
#include "designer_stroke_generator.hpp"
#include "tvg_sdl_example.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

namespace {

void append_path(tvg::Shape *path, const bench::DesignerStrokeData &shape) {
  const auto path_template = bench::path_template(shape.kind);
  for (size_t i = 0; i < path_template.command_count; ++i) {
    const auto &command = path_template.commands[i];
    switch (command.verb) {
    case bench::PathVerb::Move:
      path->moveTo(bench::path_x(shape, command.x1),
                   bench::path_y(shape, command.y1));
      break;
    case bench::PathVerb::Line:
      path->lineTo(bench::path_x(shape, command.x1),
                   bench::path_y(shape, command.y1));
      break;
    case bench::PathVerb::Cubic:
      path->cubicTo(bench::path_x(shape, command.x1),
                    bench::path_y(shape, command.y1),
                    bench::path_x(shape, command.x2),
                    bench::path_y(shape, command.y2),
                    bench::path_x(shape, command.x3),
                    bench::path_y(shape, command.y3));
      break;
    case bench::PathVerb::Close:
      path->close();
      break;
    }
  }
}

class StrokebenchExample final : public bench::tvgexam::Example {
public:
  explicit StrokebenchExample(bench::CliOptions opts) : opts_(std::move(opts)) {
    alpha_ = static_cast<uint8_t>(std::lround(opts_.opacity * 255.0f));
  }

  bool content(tvg::Canvas *canvas, uint32_t w, uint32_t h) override {
    shape_config_.canvas_width = w;
    shape_config_.canvas_height = h;
    static_shapes_ =
        bench::generate_designer_strokes(opts_.seed, shape_config_);

    static_paths_.reserve(static_shapes_.size());
    for (const auto &shape_data : static_shapes_) {
      auto path = tvg::Shape::gen();
      append_path(path, shape_data);
      path->fill(0, 0, 0, 0);
      path->strokeFill(shape_data.r, shape_data.g, shape_data.b, alpha_);
      path->strokeWidth(shape_data.stroke_width);
      path->strokeCap(tvg::StrokeCap::Round);
      path->strokeJoin(tvg::StrokeJoin::Round);
      static_paths_.push_back(path);
      canvas->add(path);
    }
    return true;
  }

  bool update(tvg::Canvas *canvas, uint32_t elapsed) override {
    bench::TransformGenConfig transform_config;
    if (opts_.scene_mode == bench::SceneMode::Default) {
      transform_config.max_rotation_deg = 0.0f;
    }
    const auto transforms = bench::generate_transforms(
        opts_.seed, elapsed, shape_config_.shape_count, transform_config);

    for (size_t i = 0; i < static_paths_.size(); ++i) {
      const auto &shape = static_shapes_[i];
      const auto &t = transforms[i];
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
      static_paths_[i]->transform({a, b, tx, c, d, ty, 0, 0, 1});
    }
    canvas->update();
    return true;
  }

private:
  bench::CliOptions opts_;
  uint8_t alpha_ = 255;
  bench::DesignerStrokeGenConfig shape_config_{};
  std::vector<bench::DesignerStrokeData> static_shapes_;
  std::vector<tvg::Shape *> static_paths_;
};

std::unique_ptr<bench::tvgexam::Window>
make_window_with_example(const bench::CliOptions &opts) {
  auto example = std::make_unique<StrokebenchExample>(opts);
  switch (opts.backend) {
  case bench::Backend::CPU:
    return std::make_unique<bench::tvgexam::SwWindow>(
        example.release(), opts.width, opts.height, opts.threads, opts.vsync,
        "Strokebench");
  case bench::Backend::GL:
    return std::make_unique<bench::tvgexam::GlWindow>(
        example.release(), opts.width, opts.height, opts.threads, opts.vsync,
        "Strokebench");
  case bench::Backend::WebGPU:
    return std::make_unique<bench::tvgexam::WgWindow>(
        example.release(), opts.width, opts.height, opts.threads,
        opts.wgpu_external_device, "Strokebench");
  }
  return nullptr;
}

int run_benchmark(const bench::CliOptions &opts) {
  auto window = make_window_with_example(opts);
  if (!window || !window->initialized || !window->canvas || !window->example) {
    return 1;
  }
  window->clearBuffer = true;
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

  std::cout << "Strokebench ThorVG SDL\n";
  std::cout << "Workload: designer-strokes-v1\n";
  std::cout << "Backend: " << bench::backend_name(opts.backend) << "\n";
  std::cout << "Scene:   " << bench::scene_mode_name(opts.scene_mode) << "\n";
  std::cout << "Seed:    " << opts.seed << "\n";
  std::cout << "Opacity: " << opts.opacity << " (alpha "
            << std::lround(opts.opacity * 255.0f) << ")\n";
  std::cout << "VSync:   " << (opts.vsync ? "ON" : "OFF") << "\n";
  return run_benchmark(opts);
}

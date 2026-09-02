/**
 * StrokeRectbench: ThorVG SDL Benchmark
 *
 * Draws filled rectangles with a white stroke (width=3).
 * The drawing logic is kept intentionally simple. The SDL + ThorVG setup is
 * organized to mirror the official thorvg.example "Example.h" structure.
 */

#include "benchmark_runner.hpp"
#include "cli_parser.hpp"
#include "rect_generator.hpp"

#include "tvg_sdl_example.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

namespace {

class StrokeRectbenchExample final : public bench::tvgexam::Example {
public:
  explicit StrokeRectbenchExample(bench::CliOptions opts)
      : opts_(std::move(opts)) {}

  bool content(tvg::Canvas *canvas, uint32_t w, uint32_t h) override {
    rect_config_.canvas_width = w;
    rect_config_.canvas_height = h;

    static_rects_ = bench::generate_static_rects(opts_.seed, rect_config_);

    static_shapes_.reserve(static_rects_.size());
    for (const auto &rect : static_rects_) {
      auto shape = tvg::Shape::gen();
      shape->appendRect(rect.x, rect.y, rect.w, rect.h);
      shape->fill(rect.r, rect.g, rect.b, rect.a);
      // Add white stroke with width 3
      shape->strokeFill(255, 255, 255, 255);
      shape->strokeWidth(3.0f);
      shape->strokeCap(tvg::StrokeCap::Butt);
      shape->strokeJoin(tvg::StrokeJoin::Miter);
      shape->strokeMiterlimit(4.0f);
      static_shapes_.push_back(shape);
      canvas->add(shape);
    }

    return true;
  }

  bool update(tvg::Canvas *canvas, uint32_t elapsed) override {
    const uint32_t frame_index = elapsed; // deterministic: use frame index as "elapsed"

    bench::TransformGenConfig transform_config;
    if (opts_.scene_mode == bench::SceneMode::Default) {
      transform_config.max_rotation_deg = 0.0f;
    }

    auto transforms = bench::generate_transforms(
        opts_.seed, frame_index, rect_config_.rect_count, transform_config);

    for (size_t i = 0; i < static_shapes_.size(); ++i) {
      const auto &rect = static_rects_[i];

      const float cx = rect.x + rect.w * 0.5f;
      const float cy = rect.y + rect.h * 0.5f;
      const auto affine = bench::centered_transform(transforms[i], cx, cy);
      tvg::Matrix m = {affine.a, affine.b, affine.tx, affine.c, affine.d,
                       affine.ty, 0,        0,        1};
      static_shapes_[i]->transform(m);
    }

    canvas->update();
    return true;
  }

private:
  bench::CliOptions opts_;
  bench::RectGenConfig rect_config_{};
  std::vector<bench::RectData> static_rects_;
  std::vector<tvg::Shape *> static_shapes_;
};

std::unique_ptr<bench::tvgexam::Window>
make_window_with_example(const bench::CliOptions &opts) {
  auto example = std::make_unique<StrokeRectbenchExample>(opts);

  switch (opts.backend) {
  case bench::Backend::CPU:
    return std::make_unique<bench::tvgexam::SwWindow>(
        example.release(), opts.width, opts.height, opts.vsync,
        "StrokeRectbench");

  case bench::Backend::GL:
    return std::make_unique<bench::tvgexam::GlWindow>(
        example.release(), opts.width, opts.height, opts.vsync,
        opts.gpu_sync, "StrokeRectbench");

  case bench::Backend::WebGPU:
    return std::make_unique<bench::tvgexam::WgWindow>(
        example.release(), opts.width, opts.height, opts.vsync,
        opts.gpu_sync, "StrokeRectbench");
  }

  return nullptr;
}

int run_benchmark(const bench::CliOptions &opts) {
  auto window = make_window_with_example(opts);
  if (!window || !window->initialized || !window->canvas || !window->example) {
    return 1;
  }

  window->clearBuffer = true;

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

  std::cout << "StrokeRectbench ThorVG SDL\n";
  std::cout << "Backend: " << bench::backend_name(opts.backend) << "\n";
  std::cout << "Scene:   " << bench::scene_mode_name(opts.scene_mode) << "\n";
  std::cout << "Seed:    " << opts.seed << "\n";
  std::cout << "VSync:   " << (opts.vsync ? "ON" : "OFF") << "\n";

  return run_benchmark(opts);
}

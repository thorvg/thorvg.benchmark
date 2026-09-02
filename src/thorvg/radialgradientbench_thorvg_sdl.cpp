#include "benchmark_runner.hpp"
#include "circle_generator.hpp"
#include "cli_parser.hpp"

#include "tvg_sdl_example.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

namespace {

class RadialGradientbenchExample final : public bench::tvgexam::Example {
public:
  explicit RadialGradientbenchExample(bench::CliOptions opts)
      : opts_(std::move(opts)) {}

  bool content(tvg::Canvas *canvas, uint32_t w, uint32_t h) override {
    circle_config_.canvas_width = w;
    circle_config_.canvas_height = h;

    static_circles_ = bench::generate_static_circles(opts_.seed, circle_config_);

    static_shapes_.reserve(static_circles_.size());
    for (const auto &circle : static_circles_) {
      auto shape = tvg::Shape::gen();
      shape->appendCircle(circle.cx, circle.cy, circle.radius, circle.radius);
      
      // Create radial gradient (cx, cy, r, fx, fy, fr)
      auto grad = tvg::RadialGradient::gen();
      grad->radial(circle.cx, circle.cy, circle.radius, circle.cx, circle.cy, 0.0f);
      
      tvg::Fill::ColorStop colorStops[2];
      colorStops[0] = {0.0f, circle.r, circle.g, circle.b, circle.a};
      colorStops[1] = {1.0f, static_cast<uint8_t>(255 - circle.r), 
                       static_cast<uint8_t>(255 - circle.g), 
                       static_cast<uint8_t>(255 - circle.b), circle.a};
      grad->colorStops(colorStops, 2);
      
      shape->fill(std::move(grad));
      static_shapes_.push_back(shape);
      canvas->add(shape);
    }

    return true;
  }

  bool update(tvg::Canvas *canvas, uint32_t elapsed) override {
    const uint32_t frame_index = elapsed;

    bench::TransformGenConfig transform_config;
    // Circles are rotation-invariant
    transform_config.max_rotation_deg = 0.0f;

    auto transforms = bench::generate_transforms(
        opts_.seed, frame_index, circle_config_.circle_count, transform_config);

    for (size_t i = 0; i < static_shapes_.size(); ++i) {
      const auto &circle = static_circles_[i];
      const auto affine =
          bench::centered_transform(transforms[i], circle.cx, circle.cy);
      tvg::Matrix m = {affine.a, affine.b, affine.tx, affine.c, affine.d,
                       affine.ty, 0,        0,        1};
      static_shapes_[i]->transform(m);
    }

    canvas->update();
    return true;
  }

private:
  bench::CliOptions opts_;
  bench::CircleGenConfig circle_config_{};
  std::vector<bench::CircleData> static_circles_;
  std::vector<tvg::Shape *> static_shapes_;
};

std::unique_ptr<bench::tvgexam::Window>
make_window_with_example(const bench::CliOptions &opts) {
  auto example = std::make_unique<RadialGradientbenchExample>(opts);

  switch (opts.backend) {
  case bench::Backend::CPU:
    return std::make_unique<bench::tvgexam::SwWindow>(
        example.release(), opts.width, opts.height, opts.vsync,
        "RadialGradientbench");

  case bench::Backend::GL:
    return std::make_unique<bench::tvgexam::GlWindow>(
        example.release(), opts.width, opts.height, opts.vsync,
        opts.gpu_sync, "RadialGradientbench");

  case bench::Backend::WebGPU:
    return std::make_unique<bench::tvgexam::WgWindow>(
        example.release(), opts.width, opts.height, opts.vsync,
        opts.gpu_sync, "RadialGradientbench");
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

  std::cout << "Radial Gradient Bench ThorVG SDL\n";
  std::cout << "Backend: " << bench::backend_name(opts.backend) << "\n";
  std::cout << "Scene:   " << bench::scene_mode_name(opts.scene_mode) << "\n";
  std::cout << "Seed:    " << opts.seed << "\n";
  std::cout << "VSync:   " << (opts.vsync ? "ON" : "OFF") << "\n";

  return run_benchmark(opts);
}

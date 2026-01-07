#include "benchmark_runner.hpp"
#include "circle_generator.hpp"
#include "cli_parser.hpp"

#include "tvg_sdl_example.hpp"

#include <cmath>
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
      canvas->push(shape);
    }

    return true;
  }

  bool update(tvg::Canvas *canvas, uint32_t elapsed) override {
    const uint32_t frame_index = elapsed;

    bench::TransformGenConfig transform_config;
    // Circles are rotation-invariant
    transform_config.max_rotation_deg = 0.0f;

    if (opts_.scene_mode == bench::SceneMode::Default ||
        opts_.scene_mode == bench::SceneMode::Rotation) {
      auto transforms = bench::generate_transforms(
          opts_.seed, frame_index, circle_config_.circle_count, transform_config);

      constexpr float kDegToRad = 0.01745329251994329576923690768489f;

      for (size_t i = 0; i < static_shapes_.size() && i < transforms.size() &&
                         i < static_circles_.size();
           ++i) {
        const auto &t = transforms[i];
        const auto &circle = static_circles_[i];

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

        tvg::Matrix m = {a, b, tx, c, d, ty, 0, 0, 1};
        static_shapes_[i]->transform(m);
      }

      canvas->update();
      return true;
    }
    return false;
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
        example.release(), opts.width, opts.height, opts.threads, opts.vsync,
        "RadialGradientbench");

  case bench::Backend::GL:
    return std::make_unique<bench::tvgexam::GlWindow>(
        example.release(), opts.width, opts.height, opts.threads, opts.vsync,
        "RadialGradientbench");

  case bench::Backend::WebGPU:
    return std::make_unique<bench::tvgexam::WgWindow>(
        example.release(), opts.width, opts.height, opts.threads,
        opts.wgpu_external_device, "RadialGradientbench");
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

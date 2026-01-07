/**
 * Rectbench: ThorVG SDL Benchmark
 *
 * The drawing logic (rectangle generation + ThorVG paint creation) is kept
 * intentionally simple. The SDL + ThorVG setup is organized to mirror the
 * official thorvg.example "Example.h" structure, so ThorVG maintainers can
 * easily compare conditions (Canvas creation, target setup, draw/sync/present).
 */

#include "benchmark_runner.hpp"
#include "cli_parser.hpp"
#include "rect_generator.hpp"

#include "tvg_sdl_example.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

class RectbenchExample final : public bench::tvgexam::Example {
public:
  explicit RectbenchExample(bench::CliOptions opts)
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
      static_shapes_.push_back(shape);
      canvas->push(shape);
    }

    return true;
  }

  bool update(tvg::Canvas *canvas, uint32_t elapsed) override {
    const uint32_t frame_index = elapsed; // deterministic: use frame index as "elapsed"

    bench::TransformGenConfig transform_config;
    if (opts_.scene_mode == bench::SceneMode::Default) {
      transform_config.max_rotation_deg = 0.0f;
    }

    if (opts_.scene_mode == bench::SceneMode::Default ||
        opts_.scene_mode == bench::SceneMode::Rotation) {
      auto transforms = bench::generate_transforms(
          opts_.seed, frame_index, rect_config_.rect_count, transform_config);

      constexpr float kDegToRad =
          0.01745329251994329576923690768489f; // pi/180

      for (size_t i = 0; i < static_shapes_.size() && i < transforms.size() &&
                         i < static_rects_.size();
           ++i) {
        const auto &t = transforms[i];
        const auto &rect = static_rects_[i];

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

        // Rotate/scale around rect center, then translate by (dx,dy).
        const float tx = t.dx + cx - (a * cx + b * cy);
        const float ty = t.dy + cy - (c * cx + d * cy);

        // ThorVG Matrix: e11,e12,e13,e21,e22,e23,e31,e32,e33
        tvg::Matrix m = {a, b, tx, c, d, ty, 0, 0, 1};
        static_shapes_[i]->transform(m);
      }

      canvas->update();
      return true;
    }
    return false;
  }

  const bench::RectGenConfig &rect_config() const { return rect_config_; }

private:
  bench::CliOptions opts_;
  bench::RectGenConfig rect_config_{};
  std::vector<bench::RectData> static_rects_;
  std::vector<tvg::Shape *> static_shapes_;
};

std::unique_ptr<bench::tvgexam::Window>
make_window_with_example(const bench::CliOptions &opts) {
  auto example = std::make_unique<RectbenchExample>(opts);

  switch (opts.backend) {
  case bench::Backend::CPU:
    return std::make_unique<bench::tvgexam::SwWindow>(
        example.release(), opts.width, opts.height, opts.threads, opts.vsync,
        "Rectbench");

  case bench::Backend::GL:
    return std::make_unique<bench::tvgexam::GlWindow>(
        example.release(), opts.width, opts.height, opts.threads, opts.vsync,
        "Rectbench");

  case bench::Backend::WebGPU:
    return std::make_unique<bench::tvgexam::WgWindow>(
        example.release(), opts.width, opts.height, opts.threads,
        opts.wgpu_external_device, "Rectbench");
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

  std::cout << "Rectbench ThorVG SDL\n";
  std::cout << "Backend: " << bench::backend_name(opts.backend) << "\n";
  std::cout << "Scene:   " << bench::scene_mode_name(opts.scene_mode) << "\n";
  std::cout << "Seed:    " << opts.seed << "\n";
  std::cout << "VSync:   " << (opts.vsync ? "ON" : "OFF") << "\n";

  return run_benchmark(opts);
}

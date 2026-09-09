/**
 * Multi-image benchmark: ThorVG SDL benchmark
 *
 * Draws 5000 instances selected from 25 independently loaded images.
 */

#include "benchmark_runner.hpp"
#include "cli_parser.hpp"
#include "multi_image_layout.hpp"

#include "tvg_sdl_example.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

std::string find_image_path(const char *asset) {
  const std::string paths[] = {
      "../resource/image/" + std::string(asset),
      "resource/image/" + std::string(asset),
      "./resource/image/" + std::string(asset),
  };
  for (const auto &path : paths) {
    FILE *file = fopen(path.c_str(), "r");
    if (file) {
      fclose(file);
      return path;
    }
  }
  return paths[0];
}

class MultiImagebenchExample final : public bench::tvgexam::Example {
public:
  explicit MultiImagebenchExample(bench::CliOptions opts)
      : opts_(std::move(opts)) {}

  bool content(tvg::Canvas *canvas, uint32_t width, uint32_t height) override {
    positions_ = bench::generate_multi_image_layout(opts_.seed, width, height);
    image_indices_ = bench::generate_multi_image_indices(
        opts_.seed, static_cast<uint32_t>(positions_.size()));

    for (size_t i = 0; i < positions_.size(); ++i) {
      const std::string path = find_image_path(
          bench::kMultiImageAssets[image_indices_[i]]);
      auto picture = tvg::Picture::gen();
      if (picture->load(path.c_str()) != tvg::Result::Success) {
        std::cerr << "Failed to load image: " << path << "\n";
        return false;
      }

      float image_width = 0.0f;
      float image_height = 0.0f;
      picture->size(&image_width, &image_height);

      const auto &position = positions_[i];
      const float scale =
          std::min(position.w / image_width, position.h / image_height);
      const float scaled_width = image_width * scale;
      const float scaled_height = image_height * scale;
      picture->size(scaled_width, scaled_height);
      picture->opacity(position.a);

      const float x = position.x + (position.w - scaled_width) * 0.5f;
      const float y = position.y + (position.h - scaled_height) * 0.5f;
      const tvg::Matrix matrix = {1, 0, x, 0, 1, y, 0, 0, 1};
      picture->transform(matrix);

      sizes_.push_back({scaled_width, scaled_height});
      pictures_.push_back(picture);
      canvas->add(picture);
    }

    return true;
  }

  bool update(tvg::Canvas *canvas, uint32_t elapsed) override {
    bench::TransformGenConfig config;
    if (opts_.scene_mode == bench::SceneMode::Default) {
      config.max_rotation_deg = 0.0f;
    }

    const auto transforms = bench::generate_transforms(
        opts_.seed, elapsed, static_cast<uint32_t>(pictures_.size()), config);
    constexpr float kDegToRad = 0.01745329251994329576923690768489f;

    for (size_t i = 0; i < pictures_.size(); ++i) {
      const auto &position = positions_[i];
      const auto &size = sizes_[i];
      const auto &transform = transforms[i];
      const float radians = transform.rotation_deg * kDegToRad;
      const float cosine = std::cos(radians) * transform.scale;
      const float sine = std::sin(radians) * transform.scale;
      const float center_x = position.x + position.w * 0.5f;
      const float center_y = position.y + position.h * 0.5f;
      const float tx =
          transform.dx + center_x - (cosine * size.w - sine * size.h) * 0.5f;
      const float ty =
          transform.dy + center_y - (sine * size.w + cosine * size.h) * 0.5f;

      const tvg::Matrix matrix = {cosine, -sine, tx, sine, cosine, ty, 0, 0, 1};
      pictures_[i]->transform(matrix);
    }

    canvas->update();
    return true;
  }

private:
  struct Size {
    float w;
    float h;
  };

  bench::CliOptions opts_;
  std::vector<bench::RectData> positions_;
  std::vector<uint32_t> image_indices_;
  std::vector<tvg::Picture *> pictures_;
  std::vector<Size> sizes_;
};

std::unique_ptr<bench::tvgexam::Window>
make_window_with_example(const bench::CliOptions &opts) {
  auto example = std::make_unique<MultiImagebenchExample>(opts);

  switch (opts.backend) {
  case bench::Backend::CPU:
    return std::make_unique<bench::tvgexam::SwWindow>(
        example.release(), opts.width, opts.height, opts.threads, opts.vsync,
        "MultiImagebench", tvg::EngineOption::None);
  case bench::Backend::GL:
    return std::make_unique<bench::tvgexam::GlWindow>(
        example.release(), opts.width, opts.height, opts.threads, opts.vsync,
        "MultiImagebench");
  case bench::Backend::WebGPU:
    return std::make_unique<bench::tvgexam::WgWindow>(
        example.release(), opts.width, opts.height, opts.threads,
        opts.wgpu_external_device, "MultiImagebench");
  }
  return nullptr;
}

} // namespace

int main(int argc, char *argv[]) {
  bench::CliOptions opts = bench::parse_cli(argc, argv);
  if (!opts.valid) {
    std::cerr << "Error: " << opts.error_message << "\n";
    bench::print_usage(argv[0]);
    return 1;
  }

  std::cout << "MultiImagebench ThorVG SDL\n";
  std::cout << "Backend: " << bench::backend_name(opts.backend) << "\n";
  std::cout << "Scene:   " << bench::scene_mode_name(opts.scene_mode) << "\n";
  std::cout << "Images:  " << bench::kMultiImageCount
            << " instances from 25 assets\n";
  std::cout << "Seed:    " << opts.seed << "\n";
  std::cout << "VSync:   " << (opts.vsync ? "ON" : "OFF") << "\n";

  auto window = make_window_with_example(opts);
  if (!window || !window->initialized || !window->canvas || !window->example) {
    return 1;
  }
  window->clearBuffer = true;
  return bench::run_benchmark(opts, *window);
}

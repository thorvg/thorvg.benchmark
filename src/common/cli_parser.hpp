#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace bench {

/// Supported backends
enum class Backend { CPU, GL, WebGPU };

/// Scene generation mode
enum class SceneMode {
  Default,  // Reuse shapes with per-frame translation + scale (no rotation)
  Rotation  // Reuse shapes with per-frame translation + scale + rotation
};

/// Command-line options
struct CliOptions {
  Backend backend = Backend::CPU;
  SceneMode scene_mode = SceneMode::Default;
  std::string benchmark;
  std::string image_ext = "png";
  uint64_t seed = 12345;
  uint32_t frames = 1000;
  uint32_t warmup = 120;
  uint32_t width = 2560;             // Window/render width
  uint32_t height = 1440;            // Window/render height
  bool vsync = false;
  bool gpu_sync = true;              // GPU: wait for completion before stopping timer
  std::string output_path;           // Empty means auto-generate
  std::string capture_path;          // Empty means no capture

  // Validation
  bool valid = true;
  std::string error_message;
};

/// Parse a "--key=value" style argument
inline bool parse_key_value(const char *arg, const char *key,
                            std::string &value) {
  size_t key_len = std::strlen(key);
  if (std::strncmp(arg, key, key_len) == 0 && arg[key_len] == '=') {
    value = &arg[key_len + 1];
    return true;
  }
  return false;
}

inline bool parse_key_value(const char *arg, const char *key, uint64_t &value) {
  std::string str_val;
  if (parse_key_value(arg, key, str_val)) {
    value = std::strtoull(str_val.c_str(), nullptr, 10);
    return true;
  }
  return false;
}

inline bool parse_key_value(const char *arg, const char *key, uint32_t &value) {
  std::string str_val;
  if (parse_key_value(arg, key, str_val)) {
    value = static_cast<uint32_t>(std::strtoul(str_val.c_str(), nullptr, 10));
    return true;
  }
  return false;
}

inline bool parse_key_value(const char *arg, const char *key, bool &value) {
  std::string str_val;
  if (parse_key_value(arg, key, str_val)) {
    value = (str_val == "1" || str_val == "true" || str_val == "yes");
    return true;
  }
  return false;
}

/// Print usage information
inline void print_usage(const char *program_name) {
  std::cout << "Usage: " << program_name << " [options]\n"
            << "\nOptions:\n"
            << "  --backend=cpu|gl|webgpu   Rendering backend (default: cpu)\n"
            << "  --scene=default|rotation\n"
            << "                            Scene mode (default: default)\n"
            << "                              default: reuse shapes with per-frame translation + scale\n"
            << "                              rotation: reuse shapes with per-frame translation + scale + rotation\n"
            << "  --image=png|jpg           Imagebench only: image type (default: png)\n"
            << "  --seed=INT                RNG seed (default: 12345)\n"
            << "  --frames=INT              Measured frames (default: 1000)\n"
            << "  --warmup=INT              Warmup frames (default: 120)\n"
            << "  --width=INT               Window/render width (default: 2560)\n"
            << "  --height=INT              Window/render height (default: 1440)\n"
            << "  --vsync=0|1               VSync (default: 0)\n"
            << "  --gpu_sync=0|1            Wait for GPU completion before "
               "stopping the timer (default: 1)\n"
            << "  --output=PATH             Output JSON path (default: "
               "auto-generated)\n"
            << "  --capture=PATH            Capture the prepared frame\n"
            << "  --help                    Show this help message\n";
}

/// Parse command-line arguments
inline CliOptions parse_cli(int argc, char *argv[]) {
  CliOptions opts;

  for (int i = 1; i < argc; ++i) {
    const char *arg = argv[i];

    if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
      print_usage(argv[0]);
      std::exit(0);
    }

    std::string str_val;
    uint64_t u64_val;
    uint32_t u32_val;
    bool bool_val;

    if (parse_key_value(arg, "--backend", str_val)) {
      if (str_val == "cpu") {
        opts.backend = Backend::CPU;
      } else if (str_val == "gl") {
        opts.backend = Backend::GL;
      } else if (str_val == "webgpu") {
        opts.backend = Backend::WebGPU;
      } else {
        opts.valid = false;
        opts.error_message = "Invalid backend: " + str_val;
      }
    } else if (parse_key_value(arg, "--scene", str_val)) {
      if (str_val == "default") {
        opts.scene_mode = SceneMode::Default;
      } else if (str_val == "rotation") {
        opts.scene_mode = SceneMode::Rotation;
      } else {
        opts.valid = false;
        opts.error_message = "Invalid scene mode: " + str_val;
      }
    } else if (parse_key_value(arg, "--image", str_val)) {
      if (str_val == "png") {
        opts.image_ext = "png";
      } else if (str_val == "jpg") {
        opts.image_ext = "jpg";
      } else {
        opts.valid = false;
        opts.error_message = "Invalid image type: " + str_val;
      }
    } else if (parse_key_value(arg, "--seed", u64_val)) {
      opts.seed = u64_val;
    } else if (parse_key_value(arg, "--frames", u32_val)) {
      opts.frames = u32_val;
    } else if (parse_key_value(arg, "--warmup", u32_val)) {
      opts.warmup = u32_val;
    } else if (parse_key_value(arg, "--width", u32_val)) {
      opts.width = u32_val;
    } else if (parse_key_value(arg, "--height", u32_val)) {
      opts.height = u32_val;
    } else if (parse_key_value(arg, "--vsync", bool_val)) {
      opts.vsync = bool_val;
    } else if (parse_key_value(arg, "--gpu_sync", bool_val)) {
      opts.gpu_sync = bool_val;
    } else if (parse_key_value(arg, "--output", str_val)) {
      opts.output_path = str_val;
    } else if (parse_key_value(arg, "--capture", str_val)) {
      opts.capture_path = str_val;
    } else {
      opts.valid = false;
      opts.error_message = std::string("Unknown option: ") + arg;
    }
  }

  if (opts.benchmark.empty()) {
    std::string program = argc > 0 && argv[0] ? argv[0] : "";
    const size_t slash = program.find_last_of("/\\");
    if (slash != std::string::npos) program.erase(0, slash + 1);
    const size_t marker = program.find("bench_");
    if (marker != std::string::npos) {
      opts.benchmark = program.substr(0, marker);
    }
  }

  return opts;
}

/// Get backend name as string
inline const char *backend_name(Backend b) {
  switch (b) {
  case Backend::CPU:
    return "cpu";
  case Backend::GL:
    return "gl";
  case Backend::WebGPU:
    return "webgpu";
  }
  return "unknown";
}

/// Get scene mode name as string
inline const char *scene_mode_name(SceneMode m) {
  switch (m) {
  case SceneMode::Default:
    return "default";
  case SceneMode::Rotation:
    return "rotation";
  }
  return "unknown";
}

} // namespace bench

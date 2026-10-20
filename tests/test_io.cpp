#include <chrono>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <regex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "test_support.hpp"
#include "cli_parser.hpp"
#include "json_writer.hpp"
namespace
{

bench::CliOptions parse(std::initializer_list<const char*> arguments)
{
    std::vector<std::string> storage{"test_io"};
    for (const char* argument : arguments)
        storage.emplace_back(argument);
    std::vector<char*> argv;
    for (auto& argument : storage)
        argv.push_back(argument.data());
    return bench::parse_cli(static_cast<int>(argv.size()), argv.data());
}

void test_key_value()
{
    std::string text = "unchanged";
    CHECK(bench::parse_key_value("--output=a b=c.json", "--output", text));
    CHECK(text == "a b=c.json");
    CHECK(!bench::parse_key_value("--output-dir=value", "--output", text));
    CHECK(!bench::parse_key_value("--output", "--output", text));
    CHECK(text == "a b=c.json");

    uint64_t wide = 0;
    CHECK(bench::parse_key_value("--seed=18446744073709551615", "--seed", wide));
    CHECK(wide == UINT64_MAX);
    CHECK(!bench::parse_key_value("--seeds=1", "--seed", wide));
    CHECK(wide == UINT64_MAX);

    uint32_t narrow = 0;
    CHECK(bench::parse_key_value("--frames=4294967295", "--frames", narrow));
    CHECK(narrow == UINT32_MAX);
    CHECK(!bench::parse_key_value("--warmup=1", "--frames", narrow));
    CHECK(narrow == UINT32_MAX);

    for (const char* token : {"1", "true", "yes"}) {
        bool value = false;
        const std::string argument = std::string("--vsync=") + token;
        CHECK(bench::parse_key_value(argument.c_str(), "--vsync", value));
        CHECK(value);
    }
    for (const char* token : {"0", "false", "no"}) {
        bool value = true;
        const std::string argument = std::string("--vsync=") + token;
        CHECK(bench::parse_key_value(argument.c_str(), "--vsync", value));
        CHECK(!value);
    }
    bool value = true;
    CHECK(!bench::parse_key_value("--vsync-extra=0", "--vsync", value));
    CHECK(value);
}

void test_cli_defaults_and_options()
{
    const auto defaults = parse({});
    CHECK(defaults.valid);
    CHECK(defaults.error_message.empty());
    CHECK(defaults.backend == bench::Backend::CPU);
    CHECK(defaults.scene_mode == bench::SceneMode::Default);
    CHECK(defaults.image_ext == "png");
    CHECK(defaults.seed == 12345);
    CHECK(defaults.frames == 1000);
    CHECK(defaults.warmup == 120);
    CHECK(defaults.threads == 4);
    CHECK(defaults.width == 2560);
    CHECK(defaults.height == 1440);
    CHECK(!defaults.vsync);
    CHECK(!defaults.wgpu_external_device);
    CHECK_NEAR(defaults.opacity, 1.0f, 0.000001f);
    CHECK(defaults.output_path.empty());

    const auto options = parse({"--backend=webgpu", "--scene=rotation", "--image=jpg",
                                "--seed=9876543210123", "--frames=23", "--warmup=7", "--threads=2",
                                "--width=320", "--height=180", "--vsync=1",
                                "--opacity=0.375", "--output=results dir/run=2.json",
                                "--wgpu_external_device=1"});
    CHECK(options.valid);
    CHECK(options.error_message.empty());
    CHECK(options.backend == bench::Backend::WebGPU);
    CHECK(options.scene_mode == bench::SceneMode::Rotation);
    CHECK(options.image_ext == "jpg");
    CHECK(options.seed == UINT64_C(9876543210123));
    CHECK(options.frames == 23);
    CHECK(options.warmup == 7);
    CHECK(options.threads == 2);
    CHECK(options.width == 320);
    CHECK(options.height == 180);
    CHECK(options.vsync);
    CHECK(options.wgpu_external_device);
    CHECK_NEAR(options.opacity, 0.375f, 0.000001f);
    CHECK(options.output_path == "results dir/run=2.json");

    CHECK(parse({"--backend=cpu"}).backend == bench::Backend::CPU);
    CHECK(parse({"--backend=gl"}).backend == bench::Backend::GL);
    const auto explicit_defaults = parse({"--scene=default", "--image=png",
                                          "--vsync=0", "--wgpu_external_device=0"});
    CHECK(explicit_defaults.valid);
    CHECK(explicit_defaults.scene_mode == bench::SceneMode::Default);
    CHECK(explicit_defaults.image_ext == "png");
    CHECK(!explicit_defaults.vsync);
    CHECK(!explicit_defaults.wgpu_external_device);

    CHECK(std::string(bench::backend_name(bench::Backend::CPU)) == "cpu");
    CHECK(std::string(bench::backend_name(bench::Backend::GL)) == "gl");
    CHECK(std::string(bench::backend_name(bench::Backend::WebGPU)) == "webgpu");
    CHECK(std::string(bench::backend_name(static_cast<bench::Backend>(99))) == "unknown");
    CHECK(std::string(bench::scene_mode_name(bench::SceneMode::Default)) == "default");
    CHECK(std::string(bench::scene_mode_name(bench::SceneMode::Rotation)) == "rotation");
    CHECK(std::string(bench::scene_mode_name(static_cast<bench::SceneMode>(99))) == "unknown");
}

void test_cli_validation()
{
    struct InvalidOption
    {
        const char* argument;
        const char* error;
    };
    for (const auto& invalid : {
             InvalidOption{"--backend=vulkan", "Invalid backend: vulkan"},
             InvalidOption{"--scene=spin", "Invalid scene mode: spin"},
             InvalidOption{"--image=gif", "Invalid image type: gif"},
             InvalidOption{"--unknown=1", "Unknown option: --unknown=1"},
             InvalidOption{"--frames", "Unknown option: --frames"}}) {
        const auto options = parse({invalid.argument});
        CHECK(!options.valid);
        CHECK(options.error_message == invalid.error);
    }

    const unsigned concurrency = std::thread::hardware_concurrency();
    const uint32_t expected_threads = concurrency == 0 ? 1u : concurrency;
    CHECK(bench::max_thread_count() == expected_threads);
    for (const char* token : {"max", "maX", "mAx", "mAX", "Max", "MaX", "MAx", "MAX"}) {
        CHECK(bench::is_max_token(token));
        const std::string argument = std::string("--threads=") + token;
        const auto options = parse({argument.c_str()});
        CHECK(options.valid);
        CHECK(options.threads == expected_threads);
    }
    for (const char* token : {"", "ma", "maximum", "max ", " max", "map"}) {
        CHECK(!bench::is_max_token(token));
    }

    struct ValidOpacity
    {
        const char* token;
        float expected;
    };
    for (const auto& opacity : {ValidOpacity{"0", 0.0f}, ValidOpacity{"1", 1.0f},
                                ValidOpacity{"0.25", 0.25f}, ValidOpacity{"5e-1", 0.5f}}) {
        const std::string argument = std::string("--opacity=") + opacity.token;
        const auto options = parse({argument.c_str()});
        CHECK(options.valid);
        CHECK_NEAR(options.opacity, opacity.expected, 0.000001f);
    }
    for (const char* token : {"", "-0.1", "1.1", "nan", "inf", "-inf", "text", "0.5tail"}) {
        const std::string argument = std::string("--opacity=") + token;
        const auto options = parse({argument.c_str()});
        CHECK(!options.valid);
        CHECK(options.error_message == std::string("Invalid opacity: ") + token);
    }
}

struct TemporaryDirectory
{
    std::filesystem::path path;

    TemporaryDirectory()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto base = std::filesystem::temp_directory_path();
        for (int attempt = 0; attempt < 100; ++attempt) {
            auto candidate = base / ("thorvg-benchmark-io-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(candidate)) {
                path = std::move(candidate);
                return;
            }
        }
    }

    ~TemporaryDirectory()
    {
        if (!path.empty()) {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    }
};

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream file(path);
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
}

void test_json_output()
{
    CHECK(bench::json_escape("").empty());
    CHECK(bench::json_escape("ordinary / text") == "ordinary / text");
    CHECK(bench::json_escape("\"\\\n\r\t") == "\\\"\\\\\\n\\r\\t");

    bench::BenchmarkMetadata metadata;
    metadata.engine = "thorvg";
    metadata.backend = "gl";
    CHECK(std::regex_match(bench::get_timestamp(), std::regex("[0-9]{8}_[0-9]{6}")));
    CHECK(std::regex_match(bench::get_default_output_path(metadata),
                           std::regex("\\./results_thorvg_gl_[0-9]{8}_[0-9]{6}\\.json")));
    metadata.engine = "skia";
    metadata.backend = "cpu";
    CHECK(std::regex_match(bench::get_default_output_path(metadata),
                           std::regex("\\./results_skia_cpu_[0-9]{8}_[0-9]{6}\\.json")));

    bench::BenchmarkStats stats;
    stats.avg_ms = 1.25;
    stats.median_ms = 1.125;
    stats.p95_ms = 2.5;
    stats.p99_ms = 3.75;
    stats.min_ms = 0.5;
    stats.max_ms = 4.875;
    stats.stddev_ms = 0.625;
    stats.fps = 800.0;
    metadata.engine = "engine\"name";
    metadata.backend = "back\\end";
    metadata.seed = UINT64_C(9876543210123);
    metadata.rect_count = 17;
    metadata.width = 320;
    metadata.height = 180;
    metadata.vsync = true;
    metadata.scene_mode = "scene\nmode";
    metadata.warmup_frames = 7;
    metadata.measured_frames = 23;
    metadata.workload = "stroke\r\twork";
    metadata.opacity = 0.375f;

    TemporaryDirectory directory;
    CHECK(!directory.path.empty());
    if (directory.path.empty()) return;
    const auto output = directory.path / "results.json";
    CHECK(bench::write_results(output.string(), stats, metadata));
    const std::string expected = R"({
  "stats": {
    "avg_ms": 1.250000,
    "median_ms": 1.125000,
    "p95_ms": 2.500000,
    "p99_ms": 3.750000,
    "min_ms": 0.500000,
    "max_ms": 4.875000,
    "stddev_ms": 0.625000,
    "fps": 800.000000
  },
  "metadata": {
    "engine": "engine\"name",
    "backend": "back\\end",
    "seed": 9876543210123,
    "rect_count": 17,
    "resolution": "320x180",
    "vsync": true,
    "scene_mode": "scene\nmode",
    "warmup_frames": 7,
    "measured_frames": 23,
    "workload": "stroke\r\twork",
    "opacity": 0.375000
  }
}
)";
    CHECK(read_file(output) == expected);

    metadata.workload.clear();
    metadata.vsync = false;
    CHECK(bench::write_results(output.string(), stats, metadata));
    const std::string without_workload = read_file(output);
    CHECK(without_workload.find("\"vsync\": false") != std::string::npos);
    CHECK(without_workload.find("\"workload\"") == std::string::npos);
    CHECK(without_workload.find("\"opacity\"") == std::string::npos);
    CHECK(without_workload.find("\"measured_frames\": 23\n  }\n}\n") != std::string::npos);

    const auto impossible = directory.path / "missing-parent" / "results.json";
    CHECK(!bench::write_results(impossible.string(), stats, metadata));
    CHECK(!std::filesystem::exists(impossible));
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc > 1) {
        bench::parse_cli(argc, argv);
        return 1;  // --help and -h must exit successfully inside parse_cli.
    }
    test_key_value();
    test_cli_defaults_and_options();
    test_cli_validation();
    test_json_output();
    return test_result();
}

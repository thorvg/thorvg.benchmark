#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "test_support.hpp"
#include "adapter_harness.hpp"
#include "benchmark_runner.hpp"
namespace
{

std::vector<std::string> normalized_calls()
{
    std::vector<std::string> result;
    for (const auto& call : adapter_test::calls) {
        if (call != "viewport" && call != "back_buffer") result.push_back(call);
    }
    return result;
}

void reset_calls()
{
    adapter_test::calls.clear();
    adapter_test::viewport.clear();
    adapter_test::elapsed = std::chrono::milliseconds::zero();
}

struct FakeClock
{
    using duration = std::chrono::milliseconds;
    using rep = duration::rep;
    using period = duration::period;
    using time_point = std::chrono::time_point<FakeClock>;
    static constexpr bool is_steady = true;
    inline static unsigned now_calls = 0;

    static time_point now()
    {
        adapter_test::record(now_calls++ % 2 == 0 ? "timer_start" : "timer_stop");
        return time_point(adapter_test::elapsed);
    }
};

struct TemporaryDirectory
{
    std::filesystem::path path;

    TemporaryDirectory()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            auto candidate = std::filesystem::current_path() / ("test-sync-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
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

double json_number(const std::string& json, const char* key)
{
    const auto marker = "\"" + std::string(key) + "\":";
    const auto position = json.find(marker);
    CHECK(position != std::string::npos);
    if (position == std::string::npos) return std::numeric_limits<double>::quiet_NaN();
    std::istringstream value(json.substr(position + marker.size()));
    double result = std::numeric_limits<double>::quiet_NaN();
    value >> result;
    CHECK(!value.fail());
    return result;
}

struct SkiaGlFixture
{
    bench::skiaexam::Example example;
    SDL_Window target;
    bench::skiaexam::GlWindow window;

    SkiaGlFixture()
    {
        window.example = &example;
        window.window = &target;
        window.surface = std::make_shared<SkSurface>();
    }
};

struct ThorvgGlFixture
{
    bench::tvgexam::Example example;
    tvg::Canvas canvas;
    SDL_Window target;
    bench::tvgexam::GlWindow window;

    ThorvgGlFixture()
    {
        window.example = &example;
        window.canvas = &canvas;
        window.window = &target;
    }
};

template<typename Window>
struct ThorvgFixture
{
    bench::tvgexam::Example example;
    tvg::Canvas canvas;
    SDL_Window target;
    Window window;

    ThorvgFixture()
    {
        window.example = &example;
        window.canvas = &canvas;
        window.window = &target;
    }
};

struct CaptureOutput
{
    std::ostringstream output;
    std::streambuf* previous = std::cout.rdbuf(output.rdbuf());
    ~CaptureOutput() { std::cout.rdbuf(previous); }
};

std::vector<std::string> run_timed_adapter(bench::BenchmarkWindow& window,
                                           const std::filesystem::path& output,
                                           bench::Backend backend = bench::Backend::GL)
{
    const bool waits = backend == bench::Backend::WebGPU;
    bench::CliOptions options;
    options.backend = backend;
    options.warmup = 1;
    options.frames = 2;
    options.width = 320;
    options.height = 180;
    options.output_path = output.string();
    reset_calls();
    FakeClock::now_calls = 0;
    {
        CaptureOutput capture;
        CHECK(bench::run_benchmark<FakeClock>(options, window, "timed-sync") == 0);
    }

    std::vector<std::string> expected = {"content", "update", "draw", "submit"};
    if (waits) expected.push_back("wait");
    for (int frame = 0; frame < 3; ++frame) {
        expected.insert(expected.end(), {"timer_start", "update", "draw", "submit", "present"});
        if (waits) expected.push_back("wait");
        expected.push_back("timer_stop");
    }
    CHECK(normalized_calls() == expected);
    CHECK(FakeClock::now_calls == 6);
    CHECK(adapter_test::elapsed.count() == (waits ? 55 : 15));

    std::ifstream file(output);
    CHECK(file.is_open());
    const std::string json{std::istreambuf_iterator<char>(file),
                           std::istreambuf_iterator<char>()};
    const double expected_ms = waits ? 14.0 : 4.0;
    CHECK_NEAR(json_number(json, "avg_ms"), expected_ms, 0.000001);
    CHECK_NEAR(json_number(json, "min_ms"), expected_ms, 0.000001);
    CHECK_NEAR(json_number(json, "max_ms"), expected_ms, 0.000001);
    CHECK(json_number(json, "warmup_frames") == 1);
    CHECK(json_number(json, "measured_frames") == 2);
    CHECK(json.find(waits ? "\"gpu_completion\": \"queue.onSubmittedWorkDone\""
                          : "\"gpu_completion\": \"none\"") != std::string::npos);
    CHECK(json.find(waits ? "\"timing_mode\": \"onscreen_gpu_complete\""
                          : "\"timing_mode\": \"diagnostic_submit\"") != std::string::npos);
    return normalized_calls();
}

void test_gl_timing_inside_runner()
{
    TemporaryDirectory directory;
    CHECK(!directory.path.empty());
    if (directory.path.empty()) return;
    SkiaGlFixture skia;
    const auto skia_trace = run_timed_adapter(skia.window, directory.path / "skia.json");
    CHECK(skia.example.updated_frames == std::vector<uint32_t>({0, 0, 1, 2}));

    ThorvgGlFixture thorvg;
    const auto thorvg_trace = run_timed_adapter(thorvg.window, directory.path / "thorvg.json");
    CHECK(thorvg.example.updated_frames == std::vector<uint32_t>({0, 0, 1, 2}));
    CHECK(thorvg.canvas.clear_values == std::vector<bool>({false, true, true, true}));
    CHECK(thorvg.window.clearBuffer);
    CHECK(skia_trace == thorvg_trace);

    ThorvgFixture<bench::tvgexam::WgWindow> webgpu;
    run_timed_adapter(webgpu.window, directory.path / "webgpu.json", bench::Backend::WebGPU);
    CHECK(webgpu.example.updated_frames == std::vector<uint32_t>({0, 0, 1, 2}));
}

void test_gl_policy_parity()
{
    SkiaGlFixture skia;
    ThorvgGlFixture thorvg;
    std::vector<std::string> initial = {"content", "update", "draw", "submit"};
    std::vector<std::string> frame = {"update", "draw", "submit", "present"};

    reset_calls();
    CHECK(skia.window.ready());
    CHECK(normalized_calls() == initial);
    CHECK(skia.example.last_canvas == skia.window.surface->getCanvas());
    CHECK(skia.example.last_width == 320 && skia.example.last_height == 180);
    CHECK(adapter_test::viewport == std::vector<int>({0, 0, 320, 180}));
    const auto skia_initial = normalized_calls();

    reset_calls();
    CHECK(thorvg.window.ready());
    CHECK(normalized_calls() == initial);
    CHECK(normalized_calls() == skia_initial);
    CHECK(thorvg.example.last_canvas == &thorvg.canvas);
    CHECK(thorvg.example.last_width == 320 && thorvg.example.last_height == 180);
    CHECK(thorvg.canvas.clear_values == std::vector<bool>({false}));
    CHECK(thorvg.window.clearBuffer);

    reset_calls();
    CHECK(skia.window.update(41));
    CHECK(skia.window.draw());
    skia.window.refresh();
    CHECK(skia.window.finish_gpu());
    CHECK(normalized_calls() == frame);
    CHECK(skia.example.elapsed == 41 && skia.example.last_frame == 41);
    CHECK(adapter_test::calls.size() >= 3);
    if (adapter_test::calls.size() >= 3) {
        CHECK(adapter_test::calls[1] == "viewport");
        CHECK(adapter_test::calls[2] == "back_buffer");
    }
    const auto skia_frame = normalized_calls();

    reset_calls();
    CHECK(thorvg.window.update(41));
    CHECK(thorvg.window.draw());
    thorvg.window.refresh();
    CHECK(thorvg.window.finish_gpu());
    CHECK(normalized_calls() == frame);
    CHECK(normalized_calls() == skia_frame);
    CHECK(thorvg.example.elapsed == 41 && thorvg.example.last_frame == 41);
    CHECK(thorvg.canvas.clear_values == std::vector<bool>({false, true}));

    thorvg.window.clearBuffer = false;
    CHECK(thorvg.window.draw());
    CHECK(!thorvg.canvas.clear_values.empty() && !thorvg.canvas.clear_values.back());
    CHECK(!thorvg.window.clearBuffer);

    skia.window.window = nullptr;
    thorvg.window.window = nullptr;
    reset_calls();
    skia.window.refresh();
    thorvg.window.refresh();
    CHECK(adapter_test::calls.empty());
}

void test_skia_failures()
{
    SkiaGlFixture fixture;
    auto& window = fixture.window;
    auto& example = fixture.example;

    window.surface.reset();
    reset_calls();
    CHECK(!window.ready());
    CHECK(!window.update(2));
    CHECK(!window.draw());
    CHECK(adapter_test::calls.empty());

    window.surface = std::make_shared<SkSurface>();
    window.example = nullptr;
    reset_calls();
    CHECK(!window.ready());
    CHECK(!window.update(3));
    CHECK(!window.draw());
    CHECK(normalized_calls().empty());

    window.example = &example;
    window.surface->canvas = nullptr;
    reset_calls();
    CHECK(!window.ready());
    CHECK(!window.update(4));
    CHECK(!window.draw());
    CHECK(normalized_calls().empty());
    window.surface->canvas = &window.surface->owned_canvas;

    example.content_result = false;
    reset_calls();
    CHECK(!window.ready());
    CHECK(normalized_calls() == std::vector<std::string>({"content"}));
    example.content_result = true;
    example.draw_result = false;
    reset_calls();
    CHECK(!window.ready());
    CHECK(normalized_calls() == std::vector<std::string>({"content", "update", "draw"}));
    reset_calls();
    CHECK(!window.draw());
    CHECK(normalized_calls() == std::vector<std::string>({"draw"}));

    example.update_result = false;
    reset_calls();
    CHECK(!window.ready());
    CHECK(normalized_calls() == std::vector<std::string>({"content", "update"}));
    reset_calls();
    CHECK(!window.update(77));
    CHECK(example.elapsed == 77 && example.last_frame == 77);
    CHECK(normalized_calls() == std::vector<std::string>({"update"}));
}

void test_thorvg_failures()
{
    ThorvgGlFixture fixture;
    auto& window = fixture.window;
    auto& canvas = fixture.canvas;
    auto& example = fixture.example;

    window.canvas = nullptr;
    reset_calls();
    CHECK(!window.ready());
    CHECK(!window.update(2));
    CHECK(!window.draw());
    CHECK(adapter_test::calls.empty());
    window.canvas = &canvas;
    window.example = nullptr;
    reset_calls();
    CHECK(!window.ready());
    CHECK(!window.update(3));
    CHECK(adapter_test::calls.empty());
    window.example = &example;

    example.content_result = false;
    reset_calls();
    CHECK(!window.ready());
    CHECK(adapter_test::calls == std::vector<std::string>({"content"}));
    CHECK(window.clearBuffer);
    example.content_result = true;

    canvas.draw_result = tvg::Result::Failed;
    reset_calls();
    CHECK(!window.draw());
    CHECK(adapter_test::calls == std::vector<std::string>({"draw"}));
    reset_calls();
    CHECK(!window.ready());
    CHECK(adapter_test::calls == std::vector<std::string>({"content", "update", "draw"}));
    CHECK(window.clearBuffer);
    CHECK(!canvas.clear_values.empty() && !canvas.clear_values.back());

    canvas.draw_result = tvg::Result::Success;
    canvas.sync_result = tvg::Result::Failed;
    reset_calls();
    CHECK(!window.draw());
    CHECK(adapter_test::calls == std::vector<std::string>({"draw", "submit"}));
    reset_calls();
    CHECK(!window.ready());
    CHECK(adapter_test::calls == std::vector<std::string>({"content", "update", "draw", "submit"}));
    CHECK(window.clearBuffer);
    CHECK(!canvas.clear_values.empty() && !canvas.clear_values.back());

    window.clearBuffer = false;
    reset_calls();
    CHECK(!window.ready());
    CHECK(!window.clearBuffer);
    canvas.sync_result = tvg::Result::Success;
    CHECK(window.ready());
    CHECK(!window.clearBuffer);

    example.update_result = false;
    reset_calls();
    CHECK(!window.ready());
    CHECK(normalized_calls() == std::vector<std::string>({"content", "update"}));
    reset_calls();
    CHECK(!window.update(77));
    CHECK(example.elapsed == 77 && example.last_frame == 77);
    CHECK(adapter_test::calls == std::vector<std::string>({"update"}));
}

void test_cpu_and_webgpu()
{
    SDL_Window target;
    bench::skiaexam::Example example;
    bench::skiaexam::SwWindow skia;
    skia.example = &example;
    skia.window = &target;
    skia.sk_surface = std::make_shared<SkSurface>();
    reset_calls();
    CHECK(skia.ready());
    CHECK(adapter_test::calls == std::vector<std::string>({"content", "update", "draw"}));
    reset_calls();
    CHECK(skia.update(5));
    CHECK(skia.draw());
    skia.refresh();
    CHECK(adapter_test::calls == std::vector<std::string>({"update", "draw", "present"}));
    CHECK(example.last_frame == 5 && example.elapsed == 5);
    skia.window = nullptr;
    reset_calls();
    skia.refresh();
    CHECK(adapter_test::calls.empty());
    skia.sk_surface.reset();
    CHECK(!skia.draw());
    CHECK(adapter_test::calls.empty());

    ThorvgFixture<bench::tvgexam::SwWindow> thorvg;
    reset_calls();
    CHECK(thorvg.window.ready());
    CHECK(adapter_test::calls == std::vector<std::string>({"content", "update", "draw", "submit"}));
    CHECK(thorvg.canvas.clear_values == std::vector<bool>({false}));
    reset_calls();
    CHECK(thorvg.window.update(5));
    CHECK(thorvg.window.draw());
    thorvg.window.refresh();
    CHECK(adapter_test::calls == std::vector<std::string>({"update", "draw", "submit", "present"}));
    CHECK(thorvg.canvas.clear_values == std::vector<bool>({false, true}));
    thorvg.window.window = nullptr;
    reset_calls();
    thorvg.window.refresh();
    CHECK(adapter_test::calls.empty());

    ThorvgFixture<bench::tvgexam::WgWindow> webgpu;
    reset_calls();
    CHECK(webgpu.window.ready());
    CHECK(adapter_test::calls == std::vector<std::string>({"content", "update", "draw", "submit"}));
    reset_calls();
    CHECK(webgpu.window.update(6));
    CHECK(webgpu.window.draw());
    webgpu.window.refresh();
    CHECK(webgpu.window.finish_gpu());
    CHECK(adapter_test::calls == std::vector<std::string>({"update", "draw", "submit", "present", "wait"}));
    CHECK(webgpu.example.last_frame == 6 && webgpu.example.elapsed == 6);
}

}  // namespace

int main()
{
    test_gl_timing_inside_runner();
    test_gl_policy_parity();
    test_skia_failures();
    test_thorvg_failures();
    test_cpu_and_webgpu();
    return test_result();
}

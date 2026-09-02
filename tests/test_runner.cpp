#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "test_support.hpp"
#include "benchmark_runner.hpp"
namespace
{

struct FakeClock
{
    using duration = std::chrono::nanoseconds;
    using rep = duration::rep;
    using period = duration::period;
    using time_point = std::chrono::time_point<FakeClock>;
    static constexpr bool is_steady = true;

    inline static duration elapsed{};
    inline static unsigned now_calls = 0;
    inline static std::vector<std::string>* trace = nullptr;

    static time_point now()
    {
        ++now_calls;
        if (trace) trace->push_back("clock");
        return time_point(elapsed);
    }

    static void advance(duration amount) { elapsed += amount; }

    static void reset(std::vector<std::string>& events)
    {
        elapsed = duration::zero();
        now_calls = 0;
        trace = &events;
    }
};

struct FrameCost
{
    FakeClock::duration update;
    FakeClock::duration draw;
    FakeClock::duration present;
};

FrameCost microseconds(int64_t update, int64_t draw, int64_t present)
{
    return {std::chrono::microseconds(update), std::chrono::microseconds(draw),
            std::chrono::microseconds(present)};
}

struct RecordingWindow : bench::BenchmarkWindow
{
    std::vector<FrameCost> costs{microseconds(1000, 1000, 2000)};
    std::vector<std::string> trace;
    std::vector<uint32_t> updated;
    std::vector<uint32_t> drawn;
    std::vector<uint32_t> presented;
    uint32_t current_frame = 0;
    unsigned ready_calls = 0;
    unsigned event_calls = 0;
    bool ready_ok = true;
    uint32_t fail_update = UINT32_MAX;
    uint32_t fail_draw = UINT32_MAX;
    unsigned cancel_event = UINT32_MAX;
    bool cancel_by_return = false;

    bool ready() override
    {
        ++ready_calls;
        trace.push_back("ready");
        FakeClock::advance(std::chrono::milliseconds(999));
        return ready_ok;
    }

    bool pump_events(bool& running) override
    {
        const auto event = event_calls++;
        trace.push_back("events:" + std::to_string(event));
        FakeClock::advance(std::chrono::milliseconds(500));
        if (event == cancel_event || event >= 64) {
            if (cancel_by_return) return false;
            running = false;
        }
        return true;
    }

    FrameCost cost() const
    {
        return current_frame < costs.size() ? costs[current_frame]
                                            : microseconds(1000, 1000, 2000);
    }

    bool update(uint32_t index) override
    {
        current_frame = index;
        updated.push_back(index);
        trace.push_back("update:" + std::to_string(index));
        FakeClock::advance(cost().update);
        return index != fail_update;
    }

    bool draw() override
    {
        drawn.push_back(current_frame);
        trace.push_back("draw:" + std::to_string(current_frame));
        FakeClock::advance(cost().draw);
        return current_frame != fail_draw;
    }

    void refresh() override
    {
        presented.push_back(current_frame);
        trace.push_back("present:" + std::to_string(current_frame));
        FakeClock::advance(cost().present);
    }

    uint32_t surface_width() const override { return 320; }
    uint32_t surface_height() const override { return 180; }
    const char* engine_id() const override { return "fixture-engine"; }
    const char* engine_title() const override { return "Fixture"; }
    const char* backend_id() const override { return "fixture-cpu"; }
    const char* backend_title() const override { return "CPU"; }
};

struct TemporaryDirectory
{
    std::filesystem::path path;

    TemporaryDirectory()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            auto candidate = std::filesystem::current_path() / ("test-runner-" + std::to_string(stamp) + "-" + std::to_string(attempt));
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

struct CaptureStreams
{
    std::ostringstream output;
    std::ostringstream errors;
    std::streambuf* old_output = std::cout.rdbuf(output.rdbuf());
    std::streambuf* old_errors = std::cerr.rdbuf(errors.rdbuf());

    ~CaptureStreams()
    {
        std::cout.rdbuf(old_output);
        std::cerr.rdbuf(old_errors);
    }
};

struct RunResult
{
    int status;
    std::string output;
    std::string errors;
};

RunResult run(RecordingWindow& window, const bench::CliOptions& options,
              const std::string& workload = {})
{
    FakeClock::reset(window.trace);
    CaptureStreams capture;
    const int status = bench::run_benchmark<FakeClock>(options, window, workload);
    return {status, capture.output.str(), capture.errors.str()};
}

bench::CliOptions options_for(const std::filesystem::path& output,
                              uint32_t warmup, uint32_t frames)
{
    bench::CliOptions options;
    options.warmup = warmup;
    options.frames = frames;
    options.output_path = output.string();
    options.seed = UINT64_C(9876543210123);
    options.scene_mode = bench::SceneMode::Rotation;
    options.opacity = 0.375f;
    options.vsync = true;
    options.width = 320;
    options.height = 180;
    return options;
}

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void write_sentinel(const std::filesystem::path& path)
{
    std::ofstream file(path);
    file << "previous complete result\n";
    CHECK(file.good());
}

double number(const std::string& json, const char* key)
{
    const auto marker = "\"" + std::string(key) + "\":";
    const auto position = json.find(marker);
    CHECK(position != std::string::npos);
    if (position == std::string::npos)
        return std::numeric_limits<double>::quiet_NaN();
    std::istringstream value(json.substr(position + marker.size()));
    double result = std::numeric_limits<double>::quiet_NaN();
    value >> result;
    CHECK(!value.fail());
    return result;
}

void test_complete_timing_and_metadata(const std::filesystem::path& directory)
{
    const auto output = directory / "complete.json";
    RecordingWindow window;
    // Warmup takes 10 and 20 ms; measured frames take 4, 8, 12 and 16 ms.
    // Every measured stage contributes, including the presentation call.
    window.costs = {microseconds(1000, 2000, 7000),
                    microseconds(2000, 4000, 14000),
                    microseconds(1000, 1000, 2000),
                    microseconds(2000, 2000, 4000),
                    microseconds(3000, 3000, 6000),
                    microseconds(4000, 4000, 8000)};
    const auto result = run(window, options_for(output, 2, 4), "designer-strokes-v1");
    CHECK(result.status == 0);
    CHECK(result.errors.empty());
    CHECK(window.ready_calls == 1);
    CHECK(window.event_calls == 6);
    const std::vector<uint32_t> expected_frames = {0, 1, 2, 3, 4, 5};
    CHECK(window.updated == expected_frames);
    CHECK(window.drawn == expected_frames);
    CHECK(window.presented == expected_frames);
    std::vector<std::string> expected_trace{"ready"};
    for (uint32_t frame = 0; frame < 6; ++frame) {
        const auto index = std::to_string(frame);
        for (const auto& event : {"events:" + index, std::string("clock"),
                                  "update:" + index, "draw:" + index,
                                  "present:" + index, std::string("clock")})
            expected_trace.push_back(event);
    }
    CHECK(window.trace == expected_trace);
    CHECK(std::filesystem::exists(output));
    const auto json = read_file(output);
    CHECK_NEAR(number(json, "avg_ms"), 10.0, 0.000001);
    CHECK_NEAR(number(json, "median_ms"), 10.0, 0.000001);
    CHECK_NEAR(number(json, "p95_ms"), 15.4, 0.000001);
    CHECK_NEAR(number(json, "p99_ms"), 15.88, 0.000001);
    CHECK_NEAR(number(json, "min_ms"), 4.0, 0.000001);
    CHECK_NEAR(number(json, "max_ms"), 16.0, 0.000001);
    CHECK_NEAR(number(json, "stddev_ms"), std::sqrt(20.0), 0.000001);
    CHECK_NEAR(number(json, "fps"), 100.0, 0.000001);
    CHECK(number(json, "seed") == 9876543210123.0);
    CHECK(number(json, "object_count") == 5000.0);
    CHECK(number(json, "warmup_frames") == 2.0);
    CHECK(number(json, "measured_frames") == 4.0);
    CHECK_NEAR(number(json, "opacity"), 0.375, 0.000001);
    CHECK(json.find("\"engine\": \"fixture-engine\"") != std::string::npos);
    CHECK(json.find("\"backend\": \"fixture-cpu\"") != std::string::npos);
    CHECK(json.find("\"resolution\": \"320x180\"") != std::string::npos);
    CHECK(json.find("\"requested_resolution\": \"320x180\"") != std::string::npos);
    CHECK(json.find("\"vsync_requested\": true") != std::string::npos);
    CHECK(json.find("\"scene_mode\": \"rotation\"") != std::string::npos);
    CHECK(json.find("\"workload\": \"designer-strokes-v1\"") != std::string::npos);
}

void test_no_warmup_single_frame(const std::filesystem::path& directory)
{
    const auto output = directory / "single.json";
    RecordingWindow window;
    window.costs = {microseconds(1250, 2500, 3750)};
    const auto result = run(window, options_for(output, 0, 1));
    CHECK(result.status == 0);
    CHECK(window.updated == std::vector<uint32_t>{0});
    CHECK(window.drawn == std::vector<uint32_t>{0});
    CHECK(window.presented == std::vector<uint32_t>{0});
    CHECK(window.event_calls == 1);
    const auto json = read_file(output);
    for (const auto* key : {"avg_ms", "median_ms", "p95_ms", "p99_ms",
                            "min_ms", "max_ms"})
        CHECK_NEAR(number(json, key), 7.5, 0.000001);
    CHECK_NEAR(number(json, "stddev_ms"), 0.0, 0.000001);
    CHECK_NEAR(number(json, "fps"), 1000.0 / 7.5, 0.000001);
    CHECK(number(json, "warmup_frames") == 0.0);
    CHECK(number(json, "measured_frames") == 1.0);
    CHECK(json.find("\"workload\"") == std::string::npos);
    CHECK(json.find("\"opacity\"") == std::string::npos);
}

void test_invalid_frame_counts(const std::filesystem::path& directory)
{
    const std::pair<uint32_t, uint32_t> invalid[] = {
        {0, 0}, {UINT32_MAX, 1}, {1, UINT32_MAX}};
    for (const auto& [warmup, frames] : invalid) {
        const auto output = directory / ("invalid-" + std::to_string(warmup) + "-" + std::to_string(frames) + ".json");
        RecordingWindow window;
        // Also prevents huge allocations if a mutant removes the early guard.
        window.ready_ok = false;
        const auto result = run(window, options_for(output, warmup, frames));
        CHECK(result.status != 0);
        CHECK(window.ready_calls == 0);
        CHECK(window.event_calls == 0);
        CHECK(window.updated.empty());
        CHECK(FakeClock::now_calls == 0);
        CHECK(!std::filesystem::exists(output));
    }
}

void test_ready_failure(const std::filesystem::path& directory)
{
    const auto output = directory / "ready-failure.json";
    write_sentinel(output);
    RecordingWindow window;
    window.ready_ok = false;
    const auto result = run(window, options_for(output, 1, 3));
    CHECK(result.status != 0);
    CHECK(!result.errors.empty());
    CHECK(window.ready_calls == 1);
    CHECK(window.event_calls == 0);
    CHECK(window.updated.empty());
    CHECK(FakeClock::now_calls == 0);
    CHECK(window.trace == std::vector<std::string>{"ready"});
    CHECK(read_file(output) == "previous complete result\n");
}

void test_drawable_mismatch(const std::filesystem::path& directory)
{
    const auto output = directory / "drawable-mismatch.json";
    write_sentinel(output);
    RecordingWindow window;
    auto options = options_for(output, 1, 3);
    options.width = 640;
    options.height = 480;
    const auto result = run(window, options);
    CHECK(result.status != 0);
    CHECK(result.errors.find("Requested drawable 640x480 but adapter created 320x180") != std::string::npos);
    CHECK(window.ready_calls == 0);
    CHECK(window.event_calls == 0);
    CHECK(window.updated.empty());
    CHECK(FakeClock::now_calls == 0);
    CHECK(read_file(output) == "previous complete result\n");
}

void test_cancellation(const std::filesystem::path& directory)
{
    for (const auto cancel_event : {0u, 2u}) {
        for (const bool by_return : {false, true}) {
            const auto output = directory / ("cancel-" + std::to_string(cancel_event) + "-" + std::to_string(by_return) + ".json");
            const bool existing = cancel_event == 2 && !by_return;
            if (existing) write_sentinel(output);
            RecordingWindow window;
            window.cancel_event = cancel_event;
            window.cancel_by_return = by_return;
            const auto result = run(window, options_for(output, 1, 3));
            CHECK(result.status != 0);
            CHECK(window.ready_calls == 1);
            CHECK(window.event_calls == cancel_event + 1);
            const std::vector<uint32_t> completed = cancel_event == 0
                ? std::vector<uint32_t>{}
                : std::vector<uint32_t>{0, 1};
            CHECK(window.updated == completed);
            CHECK(window.drawn == completed);
            CHECK(window.presented == completed);
            if (cancel_event == 0) CHECK(FakeClock::now_calls == 0);
            if (existing)
                CHECK(read_file(output) == "previous complete result\n");
            else
                CHECK(!std::filesystem::exists(output));
        }
    }
}

void test_frame_failures(const std::filesystem::path& directory)
{
    for (const auto failed_frame : {0u, 2u}) {
        for (const bool during_draw : {false, true}) {
            const auto output = directory / ("frame-failure-" + std::to_string(failed_frame) + "-" + std::to_string(during_draw) + ".json");
            const bool existing = failed_frame == 2;
            if (existing) write_sentinel(output);
            RecordingWindow window;
            if (during_draw)
                window.fail_draw = failed_frame;
            else
                window.fail_update = failed_frame;
            const auto result = run(window, options_for(output, 1, 3));
            CHECK(result.status != 0);
            CHECK(!result.errors.empty());
            CHECK(window.event_calls == failed_frame + 1);
            const std::vector<uint32_t> completed = failed_frame == 0
                ? std::vector<uint32_t>{}
                : std::vector<uint32_t>{0, 1};
            auto attempted = completed;
            attempted.push_back(failed_frame);
            CHECK(window.updated == attempted);
            CHECK(window.drawn == (during_draw ? attempted : completed));
            CHECK(window.presented == completed);
            if (existing)
                CHECK(read_file(output) == "previous complete result\n");
            else
                CHECK(!std::filesystem::exists(output));
        }
    }
}

void test_output_open_failure(const std::filesystem::path& directory)
{
    const auto output = directory / "absent-parent" / "result.json";
    RecordingWindow window;
    const auto result = run(window, options_for(output, 0, 1));
    CHECK(result.status != 0);
    CHECK(!result.errors.empty());
    CHECK(window.updated == std::vector<uint32_t>{0});
    CHECK(window.presented == std::vector<uint32_t>{0});
    CHECK(!std::filesystem::exists(output));
}

}  // namespace

int main()
{
    CHECK(bench::BenchmarkClock::is_steady);
    TemporaryDirectory directory;
    CHECK(!directory.path.empty());
    if (!directory.path.empty()) {
        test_complete_timing_and_metadata(directory.path);
        test_no_warmup_single_frame(directory.path);
        test_invalid_frame_counts(directory.path);
        test_ready_failure(directory.path);
        test_drawable_mismatch(directory.path);
        test_cancellation(directory.path);
        test_frame_failures(directory.path);
        test_output_open_failure(directory.path);
    }
    return test_result();
}

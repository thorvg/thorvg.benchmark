#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <string>

#include "test_support.hpp"
#include "cli_parser.hpp"
// Constructors record the boundary between real factory policy and rendering.
// No SDL, engine initialization, image assets, or graphics context is involved.
namespace factory_test
{
struct Example
{
    template<class... Args>
    explicit Example(Args&&...)
    {
    }
    virtual ~Example() = default;
};

struct Window
{
    std::unique_ptr<Example> example;
    bench::Backend backend;
    uint32_t width, height, threads;
    bool vsync, gpu_sync, external_device;

    Window(Example* example, bench::Backend backend, uint32_t width,
           uint32_t height, uint32_t threads = 0, bool vsync = false,
           bool gpu_sync = false, bool external_device = false) :
        example(example), backend(backend), width(width), height(height),
        threads(threads), vsync(vsync), gpu_sync(gpu_sync),
        external_device(external_device) {}
    virtual ~Window() = default;
};

using MakeWindow = std::unique_ptr<Window> (*)(const bench::CliOptions&);
struct Factory
{
    const char* source;
    bool thorvg;
    MakeWindow make;
};
}  // namespace factory_test

namespace tvg
{
enum class EngineOption
{
    None
};
}

namespace bench::skiaexam
{
using Window = factory_test::Window;
using Example = factory_test::Example;
struct SwWindow : Window
{
    SwWindow(Example* example, uint32_t width, uint32_t height,
             const std::string& = "Benchmark") :
        Window(example, Backend::CPU, width, height) {}
};
struct GlWindow : Window
{
    GlWindow(Example* example, uint32_t width, uint32_t height,
             bool vsync, bool gpu_sync, const std::string& = "Benchmark") :
        Window(example, Backend::GL, width, height, 0, vsync, gpu_sync) {}
};
}  // namespace bench::skiaexam

namespace bench::tvgexam
{
using Window = factory_test::Window;
using Example = factory_test::Example;
struct SwWindow : Window
{
    SwWindow(Example* example, uint32_t width, uint32_t height,
             uint32_t threads, bool vsync, const std::string& = "Benchmark",
             tvg::EngineOption = tvg::EngineOption::None) :
        Window(example, Backend::CPU, width, height, threads, vsync) {}
};
struct GlWindow : Window
{
    GlWindow(Example* example, uint32_t width, uint32_t height,
             uint32_t threads, bool vsync, bool gpu_sync,
             const std::string& = "Benchmark") :
        Window(example, Backend::GL, width, height, threads, vsync, gpu_sync) {}
};
struct WgWindow : Window
{
    WgWindow(Example* example, uint32_t width, uint32_t height,
             uint32_t threads, bool external_device,
             const std::string& = "Benchmark") :
        Window(example, Backend::WebGPU, width, height, threads, false, false,
               external_device) {}
};
}  // namespace bench::tvgexam

#include "factory_harness.hpp"

namespace
{
void check_common(const factory_test::Window& window,
                  const bench::CliOptions& opts, bool thorvg)
{
    CHECK(window.example != nullptr);
    CHECK(window.backend == opts.backend);
    CHECK(window.width == opts.width);
    CHECK(window.height == opts.height);
    if (thorvg) CHECK(window.threads == opts.threads);
}

void test_factory(const factory_test::Factory& factory)
{
    const int previous_failures = test_failures;
    bench::CliOptions opts;
    opts.width = 321;
    opts.height = 197;
    for (uint32_t threads : {0u, 7u}) {
        opts.threads = threads;
        opts.backend = bench::Backend::GL;
        for (bool vsync : {false, true}) {
            opts.vsync = vsync;
            for (bool gpu_sync : {false, true}) {
                opts.gpu_sync = gpu_sync;
                const auto window = factory.make(opts);
                CHECK(window != nullptr);
                if (!window) continue;
                check_common(*window, opts, factory.thorvg);
                CHECK(window->vsync == vsync);
                CHECK(window->gpu_sync == gpu_sync);
            }
        }
        opts.backend = bench::Backend::CPU;
        const auto cpu = factory.make(opts);
        CHECK(cpu != nullptr);
        if (cpu) {
            check_common(*cpu, opts, factory.thorvg);
            if (factory.thorvg) CHECK(cpu->vsync == opts.vsync);
        }
        opts.backend = bench::Backend::WebGPU;
        for (bool external : {false, true}) {
            opts.wgpu_external_device = external;
            const auto webgpu = factory.make(opts);
            CHECK(static_cast<bool>(webgpu) == factory.thorvg);
            if (webgpu) {
                check_common(*webgpu, opts, factory.thorvg);
                CHECK(webgpu->external_device == external);
            }
        }
    }
    opts.backend = static_cast<bench::Backend>(99);
    CHECK(factory.make(opts) == nullptr);
    if (test_failures != previous_failures)
        std::cerr << "Factory forwarding failure: " << factory.source << '\n';
}
}  // namespace

int main()
{
    static_assert(std::size(factory_test::factories) == 16);
    for (const auto& factory : factory_test::factories)
        test_factory(factory);
    return test_result();
}

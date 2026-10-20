"""Compile selected production adapter methods against recording API fakes."""

from pathlib import Path
import re
import sys


def _masked(source: str) -> str:
    # Keep offsets/newlines while ignoring braces in comments and literals.
    tokens = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
    return tokens.sub(lambda match: re.sub(r"[^\n]", " ", match.group()), source)


def _closing_brace(masked: str, opening: int) -> int:
    depth = 1
    for index in range(opening + 1, len(masked)):
        if masked[index] == "{":
            depth += 1
        elif masked[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    raise ValueError("Unclosed C++ brace")


def _method(root: Path, relative: str, owner: str, name: str) -> str:
    path = root / relative
    source = path.read_text(encoding="utf-8")
    masked = _masked(source)
    classes = list(re.finditer(r"\bstruct\s+" + re.escape(owner) + r"\b[^;{]*\{", masked))
    if len(classes) != 1:
        raise ValueError(f"Expected one {owner} in {relative}")
    opening = classes[0].end() - 1
    closing = _closing_brace(masked, opening)
    pattern = re.compile(r"^[ \t]*(?:bool|void|SkCanvas\s*\*)\s*" + re.escape(name) +
                         r"\s*\([^;{}]*\)\s*(?:override\s*)?\{", re.M)
    methods = list(pattern.finditer(masked, opening + 1, closing))
    if len(methods) != 1:
        raise ValueError(f"Expected one {owner}::{name} in {relative}")
    start = methods[0].start()
    end = _closing_brace(masked, methods[0].end() - 1) + 1
    line = source.count("\n", 0, start) + 1
    filename = path.resolve().as_posix().replace('"', '\\"')
    return (f'#line {line} "{filename}"\n' + source[start:end] +
            '\n#line 1 "adapter_harness.hpp"\n')


_PREAMBLE = r'''// Generated from production method bodies; do not edit.
#pragma once
#include "benchmark_window.hpp"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace adapter_test {
inline std::vector<std::string> calls;
inline std::vector<int> viewport;
inline std::chrono::milliseconds elapsed{};
inline void record(const std::string &call) {
  calls.push_back(call);
  if (call == "update" || call == "draw" || call == "submit" || call == "present")
    elapsed += std::chrono::milliseconds(1);
}
struct AdapterWindow : bench::BenchmarkWindow {
  bool pump_events(bool &running) override { return running; }
  uint32_t surface_width() const override { return 320; }
  uint32_t surface_height() const override { return 180; }
  const char *engine_id() const override { return "fake"; }
  const char *engine_title() const override { return "Fake"; }
  const char *backend_id() const override { return "fake"; }
  const char *backend_title() const override { return "Fake"; }
  void refresh() override {}
};
template <typename Canvas> struct ExampleSpy {
  uint32_t elapsed = 0;
  bool content_result = true, update_result = true, draw_result = true;
  Canvas *last_canvas = nullptr;
  uint32_t last_width = 0, last_height = 0, last_frame = 0;
  std::vector<uint32_t> updated_frames;
  bool content(Canvas *canvas, uint32_t width, uint32_t height) {
    record("content"); last_canvas = canvas; last_width = width; last_height = height;
    return content_result;
  }
  bool update(Canvas *canvas, uint32_t frame) {
    record("update"); last_canvas = canvas; last_frame = frame;
    updated_frames.push_back(frame);
    return update_result;
  }
  bool draw(Canvas *canvas) { record("draw"); last_canvas = canvas; return draw_result; }
};
}

struct SDL_Window {};
inline void SDL_UpdateWindowSurface(SDL_Window *) { adapter_test::record("present"); }
inline void SDL_GL_SwapWindow(SDL_Window *) { adapter_test::record("present"); }
using GLsizei = int;
#define GL_BACK 0x0405
inline void glViewport(int x, int y, GLsizei width, GLsizei height) {
  adapter_test::record("viewport"); adapter_test::viewport = {x, y, width, height};
}
inline void glDrawBuffer(int value) {
  adapter_test::record(value == GL_BACK ? "back_buffer" : "wrong_buffer");
}
struct SkCanvas {};
struct SkSurface {
  SkCanvas owned_canvas;
  SkCanvas *canvas = &owned_canvas;
  SkCanvas *getCanvas() { return canvas; }
};
namespace skgpu::ganesh {
inline void FlushAndSubmit(SkSurface *) { adapter_test::record("submit"); }
}
namespace tvg {
enum class Result { Success, Failed };
struct Canvas {
  Result draw_result = Result::Success, sync_result = Result::Success;
  std::vector<bool> clear_values;
  Result draw(bool clear = false) {
    adapter_test::record("draw"); clear_values.push_back(clear); return draw_result;
  }
  Result sync() { adapter_test::record("submit"); return sync_result; }
};
}
'''


def generate(root: Path, output: Path) -> None:
    root = Path(root)
    skia = "src/skia/skia_sdl_example.hpp"
    thorvg = "src/thorvg/tvg_sdl_example.hpp"
    parts = [_PREAMBLE, r'''
namespace bench::skiaexam {
using Example = adapter_test::ExampleSpy<SkCanvas>;
struct Window : adapter_test::AdapterWindow {
  SDL_Window *window = nullptr;
  uint32_t width = 320, height = 180;
  Example *example = nullptr;
  virtual SkCanvas *sk_canvas() = 0;
''']
    parts.extend(_method(root, skia, "Window", method) for method in ("ready", "update", "draw"))
    parts.append(r'''};
struct SwWindow : Window {
  std::shared_ptr<SkSurface> sk_surface;
''')
    parts.extend(_method(root, skia, "SwWindow", method) for method in ("refresh", "sk_canvas"))
    parts.append(r'''};
struct GlWindow : Window {
  std::shared_ptr<SkSurface> surface;
''')
    parts.extend(_method(root, skia, "GlWindow", method) for method in ("draw", "refresh", "sk_canvas"))
    parts.append(r'''};
}
namespace bench::tvgexam {
using Example = adapter_test::ExampleSpy<tvg::Canvas>;
inline bool verify(tvg::Result result) { return result == tvg::Result::Success; }
struct Window : adapter_test::AdapterWindow {
  SDL_Window *window = nullptr;
  tvg::Canvas *canvas = nullptr;
  uint32_t width = 320, height = 180;
  Example *example = nullptr;
  bool clearBuffer = true;
''')
    parts.extend(_method(root, thorvg, "Window", method) for method in ("ready", "update", "draw"))
    parts.append(r'''};
struct SwWindow : Window {
''')
    parts.append(_method(root, thorvg, "SwWindow", "refresh"))
    parts.append(r'''};
struct GlWindow : Window {
''')
    parts.append(_method(root, thorvg, "GlWindow", "refresh"))
    parts.append(r'''};
struct WgWindow : Window {
  struct WgpuContext { void present() { adapter_test::record("present"); } } wgpu;
''')
    parts.append(_method(root, thorvg, "WgWindow", "refresh"))
    parts.append("};\n}\n")
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    generated = output.resolve().as_posix().replace('"', '\\"')
    source = "".join(parts).replace('#line 1 "adapter_harness.hpp"', f'#line 1 "{generated}"')
    output.write_text(source, encoding="utf-8")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("Usage: generate_adapter_harness.py SOURCE_ROOT OUTPUT_FILE")
    generate(Path(sys.argv[1]), Path(sys.argv[2]))

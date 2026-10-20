#!/usr/bin/env python3
"""Compile actual benchmark factories against recording window constructors.

Only function extraction is textual. Assertions execute each unchanged body;
line directives preserve its production source location for coverage and errors.
"""

import argparse
from pathlib import Path
import re


EXAMPLES = {
    "circlebench": "CirclebenchExample",
    "imagebench": "ImagebenchExample",
    "lineargradientbench": "LinearGradientbenchExample",
    "multiimagebench": "MultiImagebenchExample",
    "radialgradientbench": "RadialGradientbenchExample",
    "rectbench": "RectbenchExample",
    "strokebench": "StrokebenchExample",
    "strokerectbench": "StrokeRectbenchExample",
}


def extract_factory(source):
    text = source.read_text(encoding="utf-8")
    pattern = (r"std::unique_ptr<bench::(?:skiaexam|tvgexam)::Window>\s+"
               r"make_window_with_example\s*\(const\s+bench::CliOptions\s*&\s*opts\)\s*\{")
    matches = list(re.finditer(pattern, text))
    if len(matches) != 1:
        raise ValueError(f"{source}: expected exactly one factory definition")
    match = matches[0]
    start = match.end() - 1
    depth, quote, comment = 0, None, None
    i = start
    while i < len(text):
        ch, pair = text[i], text[i:i + 2]
        if comment == "//":
            if ch == "\n":
                comment = None
        elif comment == "/*":
            if pair == "*/":
                comment = None
                i += 1
        elif quote:
            if ch == "\\":
                i += 1
            elif ch == quote:
                quote = None
        elif pair in ("//", "/*"):
            comment = pair
            i += 1
        elif pair == 'R"':
            raise ValueError(f"{source}: raw strings in factory need explicit parser support")
        elif ch in ('"', "'"):
            quote = ch
        elif ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return text[match.start():i + 1], text.count("\n", 0, match.start()) + 1
        i += 1
    raise ValueError(f"{source}: unterminated factory body")


def generate(root: Path, output: Path):
    root = root.resolve()
    chunks = ["// Generated from production factories; do not edit.\n#pragma once\n"]
    entries = []
    for engine in ("skia", "thorvg"):
        sources = sorted((root / "src" / engine).glob(f"*bench_{engine}_sdl.cpp"))
        expected = {f"{name}_{engine}_sdl.cpp" for name in EXAMPLES}
        if {path.name for path in sources} != expected:
            raise ValueError(f"{engine}: benchmark factory set changed; update test coverage explicitly")
        for source in sources:
            workload = source.name.removesuffix(f"_{engine}_sdl.cpp")
            body, line = extract_factory(source)
            namespace = f"factory_copy_{engine}_{workload}"
            # A fresh namespace isolates each source's otherwise identical name.
            chunks.append(f"namespace {namespace} {{\n"
                          f"using {EXAMPLES[workload]} = ::factory_test::Example;\n"
                          f'#line {line} "{source.as_posix()}"\n{body}\n'
                          '#line 1 "factory_harness.hpp"\n}\n')
            entries.append(f'  {{"src/{engine}/{source.name}", '
                           f'{str(engine == "thorvg").lower()}, '
                           f'&{namespace}::make_window_with_example}}')
    chunks.append("namespace factory_test {\ninline const Factory factories[] = {\n" +
                  ",\n".join(entries) + "\n};\n}\n")
    output.parent.mkdir(parents=True, exist_ok=True)
    generated = output.resolve().as_posix().replace('"', '\\"')
    source = "\n".join(chunks).replace('#line 1 "factory_harness.hpp"', f'#line 1 "{generated}"')
    output.write_text(source, encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    generate(args.root, args.output)

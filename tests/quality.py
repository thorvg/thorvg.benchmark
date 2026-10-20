#!/usr/bin/env python3
"""Collect coverage/CRAP and check a selected, explicit mutation sample.

Requires a GCC build configured with RECTBENCH_TEST_COVERAGE=ON and the
packages in requirements-quality.txt. Engine APIs are replaced at the adapter
boundary; engine rendering implementation and hardware behavior are not measured.
"""

import argparse
from importlib.metadata import version
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import unittest

import coverage
import lizard


ROOT = Path(__file__).resolve().parents[1]


def mutation_spans(source, mutation):
    before = mutation["before"]
    if mutation["file"].endswith(".py"):
        return [(match.start(), match.end()) for match in re.finditer(re.escape(before), source)]
    # C++ formatting can move whitespace, but quoted strings remain exact.
    tokens = re.findall(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|\w+|[^\s]', before)
    pattern = r"\s*".join(re.escape(token) for token in tokens)
    return [(match.start(), match.end()) for match in re.finditer(pattern, source)]


def run(command, log, *, cwd=ROOT, timeout=120):
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True,
                            errors="replace", timeout=timeout)
    log.write_text(result.stdout + result.stderr, encoding="utf-8")
    return result.returncode


def collect(args, output):
    # Reset counters so repeated collection never mixes executions/revisions.
    for path in args.build_dir.rglob("*.gcda"):
        path.unlink()
    ctest = str(Path(args.cmake).with_name("ctest.exe" if os.name == "nt" else "ctest"))
    if not Path(ctest).is_file():
        ctest = "ctest"
    code = run([ctest, "--test-dir", str(args.build_dir), "--output-on-failure", "--no-tests=error",
                "-R", "^(workloads|io|runner|sync|factories|cli_help|cli_help_short)$"], output / "cpp-tests.log")
    if code:
        raise RuntimeError("C++ baseline failed; see cpp-tests.log")

    cov = coverage.Coverage(source=[str(ROOT / "tools")], branch=True,
                            data_file=str(output / ".coverage"))
    cov.start()
    suite = unittest.defaultTestLoader.discover(str(ROOT / "tests"), pattern="test_tools.py")
    with (output / "python-tests.log").open("w", encoding="utf-8") as log:
        result = unittest.TextTestRunner(stream=log, verbosity=2).run(suite)
    cov.stop()
    cov.save()
    if not result.wasSuccessful() or result.testsRun == 0:
        raise RuntimeError("Python baseline failed or empty; see python-tests.log")
    cov.json_report(outfile=str(output / "python-coverage.json"))
    cov.xml_report(outfile=str(output / "python-coverage.xml"))

    gcov = f'"{Path(args.gcov).as_posix()}"' if Path(args.gcov).is_file() else args.gcov
    code = run([sys.executable, "-m", "gcovr", "--root", str(ROOT),
                "--filter", "src/", "--gcov-executable", gcov,
                "--json", str(output / "cpp-coverage.json"),
                "--xml", str(output / "cpp-coverage.xml"),
                str(args.build_dir)], output / "gcovr.log")
    if code:
        raise RuntimeError("gcovr failed; see gcovr.log")


def crap_report(output):
    cpp = json.loads((output / "cpp-coverage.json").read_text(encoding="utf-8"))
    python = json.loads((output / "python-coverage.json").read_text(encoding="utf-8"))
    lines_by_file = {}
    for file in cpp["files"]:
        lines_by_file[file["file"].replace("\\", "/")] = {
            line["line_number"]: line["count"] > 0 for line in file["lines"]
            if not line.get("gcovr/noncode", False)}
    for name, data in python["files"].items():
        lines_by_file[Path(name).relative_to(ROOT).as_posix() if Path(name).is_absolute()
                      else name.replace("\\", "/")] = {
            **dict.fromkeys(data["missing_lines"], False),
            **dict.fromkeys(data["executed_lines"], True)}

    functions = []
    sources = sorted((ROOT / "src").rglob("*.hpp")) + sorted((ROOT / "src").rglob("*.cpp"))
    sources += sorted((ROOT / "tools").glob("*.py"))
    for source in sources:
        name = source.relative_to(ROOT).as_posix()
        hits = lines_by_file.get(name, {})
        for function in lizard.analyze_file(str(source)).function_list:
            measured = [hit for line, hit in hits.items()
                        if function.start_line <= line <= function.end_line]
            fraction = sum(measured) / len(measured) if measured else None
            initializer_only = function.name.endswith("::GlWindow::GlWindow") and bool(measured)
            if initializer_only:
                fraction = None  # Setup bodies are deliberately not compiled by the harness.
            cc = function.cyclomatic_complexity
            functions.append({"file": name, "function": function.long_name,
                              "line": function.start_line, "cc": cc,
                              "covered": sum(measured), "lines": len(measured),
                              "scope": "constructor initializer only" if initializer_only else "function",
                              "coverage": fraction,
                              "crap": cc * cc * (1 - fraction) ** 3 + cc
                              if fraction is not None else None})
    totals = {}
    for label, prefixes in (("common", ("src/common/",)), ("adapters", ("src/skia/", "src/thorvg/")),
                            ("tools", ("tools/",))):
        hits = [hit for name, lines in lines_by_file.items()
                if name.startswith(prefixes) for hit in lines.values()]
        if not hits:
            raise RuntimeError(f"No measured coverage for {label}")
        totals[label] = {"covered": sum(hits), "lines": len(hits),
                         "percent": 100 * sum(hits) / len(hits)}
    report = {"formula": "CC^2 * (1 - line_coverage)^3 + CC", "threshold": 30,
              "tools": {name: version(name) for name in ("lizard", "coverage", "gcovr")},
              "scope": "Shared logic, actual adapter/factory bodies and GL constructor initializers against fake APIs, and Python tools. Constructor setup bodies, hardware/engine internals, and uninstrumented functions have null scores.",
              "coverage": totals, "functions": functions}
    (output / "crap.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    text = ["# Test quality report", "", "CRAP = CC² × (1 − line coverage)³ + CC.",
            "Scores above 30 are reported, not used as a reason to refactor production code.", ""]
    for label, data in totals.items():
        text.append(f"- {label}: {data['covered']}/{data['lines']} lines ({data['percent']:.1f}%).")
    measured = [f for f in functions if f["crap"] is not None]
    text += [f"- Scored functions: {len(measured)}; unscored: {len(functions) - len(measured)}.",
             "- GL constructor initializers are tested; their graphics setup bodies are excluded from CRAP.",
             "", "| Function | CC | Line coverage | CRAP |", "|---|---:|---:|---:|"]
    for f in sorted(measured, key=lambda f: f["crap"], reverse=True):
        text.append(f"| {f['file']}:{f['line']} {f['function'].replace('|', '&#124;')} | "
                    f"{f['cc']} | {f['covered']}/{f['lines']} | {f['crap']:.2f} |")
    (output / "report.md").write_text("\n".join(text) + "\n", encoding="utf-8")
    return totals


def mutate(args, output):
    manifest = json.loads((ROOT / "tests" / "mutations.json").read_text(encoding="utf-8"))
    manifest += json.loads((ROOT / "tests" / "fairness_mutations.json").read_text(encoding="utf-8"))
    if args.mutation_prefix:
        manifest = [m for m in manifest if m["id"].startswith(args.mutation_prefix)]
    if not manifest:
        raise RuntimeError("No mutations selected")
    workspace = output / "mutants"
    # Overwrite only our copied inputs; never alter the working tree.
    for directory in ("src", "tools", "tests"):
        shutil.copytree(ROOT / directory, workspace / directory, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns("__pycache__"))
    compiler = next(line.split("=", 1)[1] for line in
                    (args.build_dir / "CMakeCache.txt").read_text(encoding="utf-8").splitlines()
                    if line.startswith("CMAKE_CXX_COMPILER:"))
    def cpp_command(suite, executable):
        return [compiler, "-std=c++20", "-O0", "-I", str(workspace / "src/common"),
                "-I", str(workspace / "generated"),
                str(workspace / "tests" / f"test_{suite}.cpp"), "-o", str(executable)]

    def build_cpp(suite, executable, log):
        if suite in ("sync", "factories"):
            generator, header = (("adapter", "adapter_harness.hpp") if suite == "sync"
                                 else ("factory", "factory_harness.hpp"))
            code = run([sys.executable, "-B", str(workspace / "tests" / f"generate_{generator}_harness.py"),
                        str(workspace), str(workspace / "generated" / header)], log, cwd=workspace)
            if code:
                return code
        return run(cpp_command(suite, executable), log, cwd=workspace)

    python_command = [sys.executable, "-B", "-m", "unittest", "discover",
                      "-s", "tests", "-p", "test_tools.py"]
    suites = sorted({m["suite"] for m in manifest if not m["file"].endswith(".py")})
    for suite in suites:
        executable = workspace / ("baseline.exe" if os.name == "nt" else "baseline")
        log = output / f"mutation-baseline-{suite}.log"
        if build_cpp(suite, executable, log) or run([str(executable)], log, cwd=workspace):
            raise RuntimeError(f"Copied {suite} baseline failed; see {log}")
    if any(m["file"].endswith(".py") for m in manifest) and run(
            python_command, output / "mutation-baseline-python.log", cwd=workspace):
        raise RuntimeError("Copied Python baseline failed")
    results = []
    for mutation in manifest:
        path = workspace / mutation["file"]
        original = path.read_text(encoding="utf-8")
        spans = mutation_spans(original, mutation)
        if len(spans) != 1:
            raise RuntimeError(f"Mutation {mutation['id']} no longer matches exactly once")
        start, end = spans[0]
        path.write_text(original[:start] + mutation["after"] + original[end:], encoding="utf-8")
        log = output / (mutation["id"] + ".log")
        status = "invalid"
        try:
            if mutation["file"].endswith(".py"):
                compile(path.read_text(encoding="utf-8"), str(path), "exec")
                command = python_command
            else:
                executable = workspace / ("mutant.exe" if os.name == "nt" else "mutant")
                code = build_cpp(mutation["suite"], executable, log)
                if code:
                    results.append({"id": mutation["id"], "status": "invalid"})
                    continue
                command = [str(executable)]
                if mutation.get("args"):
                    command = [sys.executable, str(workspace / "tests/check_help.py"),
                               str(executable), *mutation["args"]]
            code = run(command, log, cwd=workspace, timeout=30)
            status = "killed" if code else "survived"
        except SyntaxError:
            log.write_text("Invalid Python syntax\n", encoding="utf-8")
        except subprocess.TimeoutExpired:
            status = "timeout"
        finally:
            path.write_text(original, encoding="utf-8")
        results.append({"id": mutation["id"], "status": status})
        print(f"{mutation['id']}: {status}", flush=True)
    killed = sum(r["status"] == "killed" for r in results)
    report = {"kind": "selected manual mutation sample, not exhaustive",
              "selection_prefix": args.mutation_prefix,
              "killed": killed, "total": len(results),
              "counts": {status: sum(r["status"] == status for r in results)
                         for status in ("killed", "survived", "invalid", "timeout")},
              "results": results}
    (output / "mutations.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    report_path = output / "report.md"
    text = report_path.read_text(encoding="utf-8").split("\nSelected manual mutants:")[0]
    report_path.write_text(text + f"\nSelected manual mutants: {killed}/{len(results)} killed. "
                          "Compile errors and timeouts do not count as kills.\n", encoding="utf-8")
    return killed == len(results) and bool(results)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build-test-quality/report")
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--gcov", default="gcov")
    parser.add_argument("--mutation-prefix", help="Run only mutants with this ID prefix")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--mutations-only", action="store_true", help="Reuse existing coverage reports")
    mode.add_argument("--coverage-only", action="store_true", help="Collect coverage and CRAP only")
    args = parser.parse_args()
    args.build_dir = args.build_dir.resolve()
    output = args.output_dir.resolve()
    if output == ROOT or ROOT.is_relative_to(output):
        parser.error("output directory must not contain the repository")
    if any(output.is_relative_to(ROOT / name) for name in ("src", "tools", "tests")):
        parser.error("output directory must be outside source directories")
    output.mkdir(parents=True, exist_ok=True)
    os.chdir(ROOT)
    os.environ["PYTHONDONTWRITEBYTECODE"] = "1"
    if not args.mutations_only:
        collect(args, output)
        totals = crap_report(output)
        print(json.dumps(totals, indent=2), flush=True)
    return 0 if args.coverage_only or mutate(args, output) else 1


if __name__ == "__main__":
    raise SystemExit(main())

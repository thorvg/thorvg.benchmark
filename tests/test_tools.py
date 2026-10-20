"""Fast regression tests for benchmark orchestration and result reporting.

Benchmark processes are replaced at their subprocess boundary; fixtures use the
same JSON schema as benchmark output without requiring a renderer or GPU.
"""

import contextlib
import importlib.util
import io
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


TOOLS = Path(__file__).resolve().parents[1] / "tools"
EXE_SUFFIX = ".exe" if sys.platform == "win32" else ""


def load_tool(name):
    spec = importlib.util.spec_from_file_location(name, TOOLS / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


run_all = load_tool("run_all")
repeat_seed = load_tool("repeat_seed")
reconstruct_summary = load_tool("reconstruct_summary")
generate_report = load_tool("generate_report")
compare_stroke = load_tool("compare_stroke")


def invoke(module, args):
    """Capture a command's normal output while calling its actual entry point."""
    stdout, stderr = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
        if module in (run_all, repeat_seed):
            code = module.main(args)
        else:
            with mock.patch.object(sys, "argv", [str(TOOLS / f"{module.__name__}.py"), *args]):
                code = module.main()
    return code, stdout.getvalue(), stderr.getvalue()


def variant(bench, scene, engine, backend, fps, avg_ms=1.0):
    return {
        "benchmark": bench,
        "scene": scene,
        "engine": engine,
        "backend": backend,
        "summary": {"fps": {"mean": fps}, "avg_ms": {"mean": avg_ms}},
    }


def result_for_command(command, frame_ms=1.0):
    arguments = dict(argument[2:].split("=", 1) for argument in command[1:])
    engine = "skia" if "skia" in Path(command[0]).name else "thorvg"
    backend = arguments["backend"]
    gpu = backend != "cpu"
    gpu_sync = arguments.get("gpu_sync", "1" if backend == "webgpu" else "0") == "1"
    frames = int(arguments["frames"])
    resolution = f"{arguments['width']}x{arguments['height']}"
    return {
        "schema_version": 2,
        "status": "passed",
        "comparable": not gpu or gpu_sync,
        "frame_times_ms": [frame_ms] * frames,
        "stats": {
            **dict.fromkeys(("avg_ms", "median_ms", "p95_ms", "p99_ms", "min_ms", "max_ms"), frame_ms),
            "stddev_ms": 0.0,
            "fps": 1000.0 / frame_ms,
        },
        "metadata": {
            "benchmark": "rect", "engine": engine,
            "engine_version": "test", "engine_revision": "test",
            "scene_model": "immediate" if engine == "skia" else "retained",
            "backend": backend, "graphics_api": {"cpu": "CPU", "gl": "OpenGL", "webgpu": "WebGPU"}[backend],
            "gpu_device": "Test GPU" if gpu else "none",
            "gpu_vendor": "Test vendor" if gpu else "none",
            "gpu_driver": "Test driver" if gpu else "none",
            "gpu_completion": ("queue.onSubmittedWorkDone" if backend == "webgpu" else "glFinish")
                              if gpu and gpu_sync else "none",
            "timing_mode": "onscreen_cpu_complete" if not gpu else
                           "onscreen_gpu_complete" if gpu_sync else "diagnostic_submit",
            "seed": int(arguments["seed"]), "object_count": 5000,
            "resolution": resolution, "requested_resolution": resolution,
            "pixel_format": "RGBA8888", "vsync_requested": False,
            "vsync_actual": False, "present_mode": "immediate",
            "scene_mode": arguments["scene"], "warmup_frames": int(arguments["warmup"]),
            "measured_frames": frames, "build_type": "Release", "asset_hash": "",
        },
    }


class StatisticsTests(unittest.TestCase):
    def test_numeric_summaries_use_sample_standard_deviation(self):
        for module in (run_all, reconstruct_summary):
            with self.subTest(tool=module.__name__):
                if module is run_all:
                    with self.assertRaises(statistics.StatisticsError):
                        module._summarize([])
                else:
                    self.assertEqual(module._summarize([]), {})
                self.assertEqual(module._summarize([7.25]), {"mean": 7.25, "stdev": 0.0})
                result = module._summarize([1.0, 2.0, 6.0])
                self.assertEqual(result["mean"], 3.0)
                self.assertAlmostEqual(result["stdev"], math.sqrt(7.0))
        self.assertEqual(repeat_seed._summarize([]), "n/a")
        self.assertEqual(repeat_seed._summarize([7.25]), "7.250000")
        self.assertEqual(repeat_seed._summarize([1.0, 2.0, 6.0]), "3.000000 ± 2.645751")

    def test_seed_sequence_is_reproducible_and_fixed_seed_takes_precedence(self):
        # Published SplitMix64 reference values for successive input states.
        self.assertEqual(run_all._splitmix64(0), 0xE220A8397B1DCDAF)
        self.assertEqual(run_all._splitmix64(1), 0x910A2DEC89025CC1)
        self.assertEqual(run_all._pick_seeds(2, None, 0), [0xE220A8397B1DCDAF, 0x910A2DEC89025CC1])
        self.assertEqual(run_all._pick_seeds(2, 0, None), [0, 0])
        self.assertEqual(run_all._pick_seeds(2, None, None), run_all._pick_seeds(2, None, 12345))
        self.assertEqual(run_all._pick_seeds(1, None, 1 << 64), run_all._pick_seeds(1, None, 0))


class RunAllTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="test-tools-", dir=Path(__file__).resolve().parent)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.bin_dir = self.root / "bin"
        self.bin_dir.mkdir()
        for name in ("rect_skia_sdl", "rect_tvg_sdl"):
            (self.bin_dir / (name + EXE_SUFFIX)).touch()
        self.output = self.root / "results"
        self.args = ["--bin-dir", str(self.bin_dir), "--output-dir", str(self.output),
                     "--benchmarks", "rect", "--engines", "thorvg", "--backends", "cpu",
                     "--scenes", "default", "--runs", "1", "--seed", "42"]

    def test_supported_matrix_and_legacy_binary_names(self):
        manifest = run_all.load_manifest()
        variants = run_all.make_variants(
            manifest, self.bin_dir, ["rect"], ["skia", "thorvg"],
            ["cpu", "gl", "webgpu"], ["default", "rotation"], backends_all=True)
        self.assertEqual(len(variants), 10)
        self.assertEqual({(v.engine, v.backend) for v in variants},
                         {("skia", "cpu"), ("skia", "gl"), ("thorvg", "cpu"),
                          ("thorvg", "gl"), ("thorvg", "webgpu")})
        self.assertEqual({v.scene for v in variants}, {"default", "rotation"})
        for v in variants:
            self.assertEqual(v.benchmark, "rect")
            self.assertTrue(Path(v.binary).is_absolute())
            self.assertEqual(Path(v.binary).name, ("rect_tvg_sdl" if v.engine == "thorvg" else "rect_skia_sdl") + EXE_SUFFIX)
            self.assertEqual(v.name, f"rect_{v.engine}_{v.backend}_{v.scene}")
        explicit = run_all.make_variants(
            manifest, self.bin_dir, ["rect"], ["skia", "thorvg"],
            ["cpu", "gl", "webgpu"], ["default", "rotation"], backends_all=False)
        self.assertEqual(len(explicit), 12)
        self.assertEqual([(v.engine, v.backend, v.scene) for v in explicit if not v.supported],
                         [("skia", "webgpu", "default"), ("skia", "webgpu", "rotation")])
        code, stdout, _ = invoke(run_all, self.args + ["--dry-run", "--benchmarks", "RectBench",
            "--engines", "SKIA,THORVG", "--backends", "ALL", "--scenes", "DEFAULT,ROTATION"])
        self.assertEqual(code, 0)
        self.assertEqual(stdout.count("[1/1]"), 10)

    def test_windows_executable_discovery_preserves_explicit_suffix(self):
        raw = json.loads(run_all.DEFAULT_MANIFEST.read_text(encoding="utf-8"))
        raw["engines"]["skia"]["command"]["executable"] = "{workload}_skia_sdl.EXE"
        manifest_path = self.root / "windows-engines.json"
        manifest_path.write_text(json.dumps(raw), encoding="utf-8")
        for name in ("rect_skia_sdl.EXE", "rect_tvg_sdl.exe"):
            (self.bin_dir / name).touch()
        commands = []

        def run(cmd, **kwargs):
            commands.append(cmd)
            Path(next(a[9:] for a in cmd if a.startswith("--output="))).write_text(
                json.dumps(result_for_command(cmd)), encoding="utf-8")
            return subprocess.CompletedProcess(cmd, 0, "")

        with mock.patch.object(run_all.sys, "platform", "win32"), \
             mock.patch.object(run_all.subprocess, "run", side_effect=run):
            code, _, _ = invoke(run_all, self.args + ["--manifest", str(manifest_path),
                                                     "--engines", "skia,thorvg"])
        self.assertEqual(code, 0)
        self.assertEqual([Path(cmd[0]).name.lower() for cmd in commands],
                         ["rect_skia_sdl.exe", "rect_tvg_sdl.exe"])

    def test_mixed_backends_preserve_async_gl_and_sync_webgpu(self):
        commands = []

        def run(cmd, **kwargs):
            commands.append(cmd)
            self.assertFalse(any(a.startswith("--gpu_sync=") for a in cmd))
            Path(next(a[9:] for a in cmd if a.startswith("--output="))).write_text(
                json.dumps(result_for_command(cmd)), encoding="utf-8")
            return subprocess.CompletedProcess(cmd, 0, "")

        with mock.patch.object(run_all.subprocess, "run", side_effect=run):
            code, _, _ = invoke(run_all, self.args + ["--engines", "skia,thorvg", "--backends", "all"])
        self.assertEqual(code, 0)
        self.assertEqual(len(commands), 5)
        summary_path = self.output / "summary.json"
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
        self.assertEqual(summary["status"], "complete")
        for item in summary["variants"]:
            self.assertEqual(item["status"], "passed")
            self.assertEqual(item["comparable"], item["backend"] != "gl")
        _, report, _ = invoke(generate_report, [str(summary_path)])
        self.assertEqual(report.count("diagnostic (not ranked)"), 2)
        self.assertIn("## WebGPU", report)

    def test_gpu_sync_option_is_only_forwarded_to_rust_adapters(self):
        for engine, default in (("pathfinder", "0"), ("vello", "1")):
            for override in (None, "0", "1"):
                extra = [] if override is None else ["--", f"--gpu_sync={override}"]
                with self.subTest(engine=engine, override=override):
                    code, stdout, _ = invoke(run_all, self.args + ["--engines", engine,
                        "--backends", "all", "--dry-run", *extra])
                    self.assertEqual(code, 0)
                    self.assertEqual(stdout.count("--gpu_sync="), 1)
                    self.assertIn(f"--gpu_sync={default if override is None else override}", stdout)
        with mock.patch.object(run_all.subprocess, "run") as process:
            with self.assertRaises(SystemExit) as error:
                invoke(run_all, self.args + ["--build", "--", "--gpu_sync=1"])
        self.assertEqual(error.exception.code, 2)
        process.assert_not_called()

    def test_runs_share_inputs_and_write_summary_compatible_with_reports(self):
        frame_times = iter([1.0, 2.0, 6.0, 3.0])
        commands = []

        def run(cmd, **kwargs):
            commands.append(cmd)
            path = Path(next(a.removeprefix("--output=") for a in cmd if a.startswith("--output=")))
            self.assertTrue(path.is_absolute())
            path.write_text(json.dumps(result_for_command(cmd, next(frame_times))), encoding="utf-8")
            self.assertEqual(kwargs["timeout"], 1.5)
            return subprocess.CompletedProcess(cmd, 0, "benchmark log\n")

        args = self.args[:-2] + ["--seed-base", "0", "--runs", "2", "--engines", "skia,thorvg",
                                "--timeout-seconds", "1.5", "--", "--frames=10", "--seed=999", "--output=bad"]
        with mock.patch.object(run_all.subprocess, "run", side_effect=run):
            code, _, stderr = invoke(run_all, args)
        self.assertEqual(code, 0)
        self.assertIn("ignoring arguments controlled by the suite", stderr)
        summary_path = self.output / "summary.json"
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
        self.assertEqual(summary["suite_schema_version"], 2)
        self.assertEqual(summary["status"], "complete")
        self.assertEqual(summary["runs_per_variant"], 2)
        self.assertIsNone(summary["fixed_seed"])
        self.assertEqual(summary["seed_base"], 0)
        self.assertEqual(len(set(summary["seeds"])), 2)
        self.assertEqual([Path(c[0]).name for c in commands],
                         [name + EXE_SUFFIX for name in
                          ("rect_skia_sdl", "rect_tvg_sdl", "rect_tvg_sdl", "rect_skia_sdl")])
        self.assertEqual([c[3] for c in commands],
                         [f"--seed={seed}" for seed in summary["seeds"] for _ in range(2)])
        for cmd in commands:
            self.assertIn("--frames=10", cmd)
            self.assertEqual(sum(a.startswith("--seed=") for a in cmd), 1)
            self.assertEqual(sum(a.startswith("--output=") for a in cmd), 1)
        first, second = summary["variants"]
        self.assertEqual((first["engine"], second["engine"]), ("skia", "thorvg"))
        self.assertAlmostEqual(first["summary"]["fps"]["mean"], (1000.0 + 1000.0 / 3) / 2)
        self.assertAlmostEqual(first["summary"]["avg_ms"]["stdev"], math.sqrt(2))
        self.assertAlmostEqual(second["summary"]["fps"]["mean"], (500.0 + 1000.0 / 6) / 2)
        self.assertEqual(first["summary"]["median_ms"], {"mean": 2.0, "stdev": math.sqrt(2)})
        self.assertEqual(first["summary"]["p95_ms"], {"mean": 2.0, "stdev": math.sqrt(2)})
        for item in summary["variants"]:
            self.assertEqual(item["status"], "passed")
            self.assertTrue(item["comparable"])
            self.assertEqual(len(item["outputs"]), 2)
            self.assertEqual([run["frame_count"] for run in item["runs"]], [10, 10])
            for path in item["outputs"]:
                self.assertEqual(Path(path).with_suffix(".log").read_text(encoding="utf-8"), "benchmark log\n")
        self.assertEqual(invoke(generate_report, [str(summary_path)])[0], 0)

    def test_failed_runs_publish_incomplete_summary_without_ranking(self):
        for kind, message in (("exit", "exited with 7"), ("missing", "non-empty result"),
                              ("empty", "non-empty result"), ("invalid", "invalid result"),
                              ("numeric", "stats.median_ms must be numeric"),
                              ("metadata", "metadata.seed"), ("timeout", "timed out")):
            with self.subTest(kind=kind):
                output = self.root / kind
                args = self.args + ["--output-dir", str(output), "--timeout-seconds", "0.1", "--runs", "2"]

                def run(cmd, **kwargs):
                    if kind == "timeout":
                        raise subprocess.TimeoutExpired(cmd, 0.1, output="partial log")
                    path = Path(next(a[9:] for a in cmd if a.startswith("--output=")))
                    if kind in ("empty", "invalid"):
                        path.write_text("" if kind == "empty" else "{bad", encoding="utf-8")
                    if kind in ("numeric", "metadata"):
                        data = result_for_command(cmd)
                        if kind == "numeric":
                            data["stats"]["median_ms"] = "ignored"
                        else:
                            data["metadata"]["seed"] += 1
                        path.write_text(json.dumps(data), encoding="utf-8")
                    return subprocess.CompletedProcess(cmd, 7 if kind == "exit" else 0, "failure log")

                with mock.patch.object(run_all.subprocess, "run", side_effect=run) as process:
                    code, _, _ = invoke(run_all, args)
                self.assertEqual(code, 2)
                self.assertEqual(process.call_count, 1)
                summary = json.loads((output / "summary.json").read_text(encoding="utf-8"))
                self.assertEqual(summary["status"], "incomplete")
                item, = summary["variants"]
                self.assertEqual(item["status"], "run-failed")
                self.assertFalse(item["comparable"])
                self.assertIn(message, item["reason"])
                self.assertEqual(item["summary"], {})
                self.assertEqual(item["outputs"], [])
                self.assertEqual(len(list(output.rglob("*.log"))), 1)

    def test_legacy_results_remain_historical_and_noncomparable(self):
        def run(cmd, **kwargs):
            data = result_for_command(cmd)
            for key in ("schema_version", "status", "comparable", "frame_times_ms"):
                del data[key]
            Path(next(a[9:] for a in cmd if a.startswith("--output="))).write_text(
                json.dumps(data), encoding="utf-8")
            return subprocess.CompletedProcess(cmd, 0, "legacy log")

        with mock.patch.object(run_all.subprocess, "run", side_effect=run):
            self.assertEqual(invoke(run_all, self.args)[0], 0)
        path = self.output / "summary.json"
        item, = json.loads(path.read_text(encoding="utf-8"))["variants"]
        self.assertEqual(item["status"], "legacy-v1")
        self.assertFalse(item["comparable"])
        self.assertEqual(item["summary"]["fps"]["mean"], 1000.0)
        code, report, _ = invoke(generate_report, [str(path)])
        self.assertEqual(code, 0)
        self.assertIn("1000.00 ± 0.00†", report)
        self.assertIn("historical context only and are not ranked", report)

    def test_dry_run_expands_all_and_strips_conflicts_without_running(self):
        with mock.patch.object(run_all.subprocess, "run") as process:
            code, stdout, stderr = invoke(run_all, self.args + ["--dry-run", "--build", "--engines", "ALL",
                "--backends", "all", "--scenes", " all , ", "--", "--seed=1", "--output=bad", "--frames=5"])
        self.assertEqual(code, 0)
        process.assert_not_called()
        self.assertFalse(self.output.exists())
        self.assertEqual(stdout.count("[1/1]"), 16)
        self.assertIn("Building: cmake --build", stdout)
        self.assertIn("--frames=5", stdout)
        self.assertNotIn("--output=bad", stdout)
        self.assertIn("--seed=1", stderr)

    def test_invalid_selection_missing_binaries_and_build_failure(self):
        for extra in (["--runs", "0"], ["--build-jobs", "0"], ["--seed-base", "4"],
                      ["--engines", "other"], ["--backends", "other"], ["--scenes", "other"]):
            with self.subTest(extra=extra), self.assertRaises(SystemExit) as error:
                invoke(run_all, self.args + extra)
            self.assertEqual(error.exception.code, 2)
        code, _, stderr = invoke(run_all, self.args + ["--engines", "skia", "--backends", "webgpu"])
        self.assertEqual(code, 2)
        item, = json.loads((self.output / "summary.json").read_text(encoding="utf-8"))["variants"]
        self.assertEqual(item["status"], "unsupported")
        self.assertIn("does not support backend webgpu", item["reason"])
        code, _, stderr = invoke(run_all, self.args + ["--bin-dir", str(self.root / "absent")])
        self.assertEqual(code, 2)
        item, = json.loads((self.output / "summary.json").read_text(encoding="utf-8"))["variants"]
        self.assertEqual(item["status"], "build-missing")
        self.assertIn("benchmark executable not found", item["reason"])
        with mock.patch.object(run_all.subprocess, "run", return_value=subprocess.CompletedProcess([], 5)) as process:
            self.assertEqual(invoke(run_all, self.args + ["--build", "--build-jobs", "3"])[0], 5)
        self.assertEqual(process.call_args.args[0], ["cmake", "--build", "build", "-j", "3"])


class RepeatSeedTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="test-tools-", dir=Path(__file__).resolve().parent)
        self.addCleanup(self.temp.cleanup)
        self.output = Path(self.temp.name) / "results"

    def test_repeats_override_conflicts_and_summarize_only_numeric_stats(self):
        stats = iter([{"avg_ms": 1, "fps": "ignored"}, {"avg_ms": 3, "fps": 20}])

        def run(cmd, **kwargs):
            self.assertEqual(cmd[:-2], ["bench", "--frames=10"])
            self.assertEqual(cmd[-2], "--seed=0")
            self.assertEqual(kwargs["timeout"], 2.0)
            Path(cmd[-1][9:]).write_text(json.dumps({"stats": next(stats)}), encoding="utf-8")
            return subprocess.CompletedProcess(cmd, 0, "ok")

        with mock.patch.object(repeat_seed.subprocess, "run", side_effect=run) as process:
            code, stdout, stderr = invoke(repeat_seed, ["--runs", "2", "--seed", "0", "--timeout-seconds", "2",
                "--output-dir", str(self.output), "--", "bench", "--seed=99", "--output", "old.json",
                "--seed", "5", "--output=old2.json", "--frames=10"])
        self.assertEqual(code, 0)
        self.assertEqual(process.call_count, 2)
        self.assertIn("avg_ms: 2.000000 ± 1.414214", stdout)
        self.assertIn("fps: 20.000000", stdout)
        self.assertIn("ignoring command args", stderr)
        self.assertEqual({p.name for p in self.output.glob("*.json")},
                         {"bench_seed0_run01.json", "bench_seed0_run02.json"})

    def test_random_seeds_are_unique_and_dry_run_never_starts_process(self):
        with mock.patch.object(repeat_seed.secrets, "randbits", side_effect=[10, 10, 20]) as random_bits, \
             mock.patch.object(repeat_seed.subprocess, "run") as process:
            code, stdout, _ = invoke(repeat_seed, ["--runs", "2", "--dry-run", "--output-dir", str(self.output),
                                                  "--", "weird name!", "--output"])
        self.assertEqual(code, 0)
        process.assert_not_called()
        self.assertEqual(random_bits.call_count, 3)
        self.assertIn("weird_name_seed10_run01.json", stdout)
        self.assertIn("weird_name_seed20_run02.json", stdout)
        self.assertEqual(list(self.output.iterdir()), [])
        self.assertEqual(repeat_seed._safe_stem("!@"), "cmd")
        self.assertEqual(repeat_seed._safe_stem("a-b_c.d"), "a-b_c.d")

    def test_failure_modes_and_invalid_arguments(self):
        for kind, expected, message in (("exit", 6, "failed"), ("missing", 2, "missing output"),
                                         ("empty", 2, "empty output"), ("invalid", 2, "invalid JSON"),
                                         ("timeout", 124, "timed out")):
            with self.subTest(kind=kind):
                output = self.output / kind

                def run(cmd, **kwargs):
                    if kind == "timeout":
                        raise subprocess.TimeoutExpired(cmd, 1, output="partial")
                    if kind in ("empty", "invalid"):
                        Path(cmd[-1][9:]).write_text("" if kind == "empty" else "[", encoding="utf-8")
                    return subprocess.CompletedProcess(cmd, 6 if kind == "exit" else 0, "log")

                with mock.patch.object(repeat_seed.subprocess, "run", side_effect=run) as process:
                    code, _, stderr = invoke(repeat_seed, ["--runs", "2", "--seed", "1", "--output-dir", str(output), "--", "bench"])
                self.assertEqual(code, expected)
                self.assertIn(message, stderr)
                self.assertEqual(process.call_count, 1)
                self.assertEqual(len(list(output.glob("*.log"))), 1)
        for args in ([], ["--runs", "0", "--", "bench"]):
            with self.subTest(args=args), self.assertRaises(SystemExit) as error:
                invoke(repeat_seed, args)
            self.assertEqual(error.exception.code, 2)


class ResultToolsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="test-tools-", dir=Path(__file__).resolve().parent)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def write_json(self, path, value):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value), encoding="utf-8")
        return path

    def test_reconstruct_groups_runs_ignores_bad_files_and_retains_schema(self):
        suite = self.root / "suite"
        scene = suite / "stroke" / "thorvg" / "gl" / "default"
        first = self.write_json(scene / "run01.json", {"stats": {"fps": 10, "avg_ms": 2, "p95_ms": "skip"}})
        second = self.write_json(scene / "run02.json", {"stats": {"fps": 30, "avg_ms": 6}})
        self.write_json(scene / "no_stats.json", {"other": 1})
        self.write_json(scene / "summary.json", {"stats": {"fps": 9999}})
        (scene / "bad.json").write_text("{", encoding="utf-8")
        other = self.write_json(suite / "rect" / "skia" / "cpu" / "rotation" / "run.json", {"stats": {"fps": 50}})
        (suite / "empty" / "skia" / "cpu" / "default").mkdir(parents=True)
        # Files at intermediate hierarchy levels are not variants.
        for directory in (suite, suite / "stroke", suite / "stroke" / "thorvg", scene.parent):
            (directory / "README.txt").write_text("metadata", encoding="utf-8")
        code, _, stderr = invoke(reconstruct_summary, [str(suite)])
        self.assertEqual(code, 0)
        self.assertIn("bad.json", stderr)
        data = json.loads((suite / "summary.json").read_text(encoding="utf-8"))
        self.assertEqual({k: data[k] for k in ("runs_per_variant", "fixed_seed", "seed_base", "seeds")},
                         {"runs_per_variant": "unknown", "fixed_seed": "unknown", "seed_base": "unknown", "seeds": []})
        self.assertEqual(len(data["variants"]), 2)
        by_bench = {v["benchmark"]: v for v in data["variants"]}
        stroke = by_bench["stroke"]
        self.assertEqual((stroke["engine"], stroke["backend"], stroke["scene"], stroke["binary"]),
                         ("thorvg", "gl", "default", "unknown"))
        self.assertEqual(set(stroke["outputs"]), {str(first), str(second)})
        self.assertEqual(stroke["summary"]["fps"]["mean"], 20.0)
        self.assertAlmostEqual(stroke["summary"]["avg_ms"]["stdev"], math.sqrt(8))
        self.assertNotIn("p95_ms", stroke["summary"])
        self.assertEqual(by_bench["rect"]["outputs"], [str(other)])
        self.assertEqual(invoke(generate_report, [str(suite / "summary.json")])[0], 0)
        code, _, stderr = invoke(reconstruct_summary, [str(self.root / "absent")])
        self.assertEqual(code, 1)
        self.assertIn("Directory not found", stderr)

    def test_report_columns_grouping_order_missing_values_and_rounding(self):
        rows = [variant("stroke", "rotation", "thorvg", "webgpu", 99.126),
                variant("rect", "rotation", "thorvg", "gl", 4),
                variant("rect", "default", "thorvg", "cpu", 3),
                variant("rect", "default", "skia", "gl", 2),
                variant("rect", "default", "skia", "cpu", 1),
                {"benchmark": "rect", "scene": "default", "engine": "thorvg", "backend": "gl"}]
        path = self.write_json(self.root / "summary.json", {"variants": rows})
        code, stdout, stderr = invoke(generate_report, [str(path)])
        self.assertEqual((code, stderr), (0, ""))
        self.assertEqual([line for line in stdout.splitlines() if line.startswith("|")], [
            "| Workload (scene) | Skia — CPU (mean FPS) | ThorVG — CPU (mean FPS) |",
            "|---|---|---|",
            "| Rectangle (default) | 1.00† | 3.00† |",
            "| Workload (scene) | Skia — OpenGL (mean FPS) | ThorVG — OpenGL (mean FPS) |",
            "|---|---|---|",
            "| Rectangle (default) | 2.00† | legacy v1† |",
            "| Rectangle (rotation) | — | 4.00† |",
            "| Workload (scene) | ThorVG — WebGPU (mean FPS) |",
            "|---|---|",
            "| Stroke (rotation) | 99.13† |",
        ])
        self.assertLess(stdout.index("## CPU"), stdout.index("## OpenGL"))
        self.assertLess(stdout.index("## OpenGL"), stdout.index("## WebGPU"))
        self.assertIn("historical context only and are not ranked", stdout)

    def test_report_default_path_empty_input_and_error_exits(self):
        old_cwd = Path.cwd()
        self.addCleanup(os.chdir, old_cwd)
        os.chdir(self.root)
        with self.assertRaises(SystemExit) as error:
            invoke(generate_report, [])
        self.assertEqual(error.exception.code, 2)
        self.assertEqual(invoke(generate_report, [str(self.root / "missing.json")])[0], 1)
        default = self.write_json(self.root / "summary.json", {})
        code, stdout, stderr = invoke(generate_report, [])
        self.assertEqual((code, stdout, stderr), (0, "No variants found in summary.\n", ""))
        default.write_text("[", encoding="utf-8")
        code, _, stderr = invoke(generate_report, [str(default)])
        self.assertEqual(code, 1)
        self.assertIn("Error: Expecting", stderr)
        self.write_json(default, {"variants": [{}]})
        self.assertIn("| Unknown (Unknown) | legacy v1† |", invoke(generate_report, [str(default)])[1])

    def test_comparison_uses_engine_ratios_and_handles_missing_or_zero_baseline(self):
        rows = [variant("stroke", "rotation", "thorvg", "cpu", 60, 2),
                variant("rect", "default", "skia", "cpu", 10, 10),
                variant("rect", "default", "thorvg", "cpu", 25, 4),
                variant("rect", "default", "skia", "gl", 20, 5),
                variant("rect", "default", "thorvg", "gl", 30, 3),
                variant("stroke", "rotation", "skia", "cpu", 0, 0)]
        path = self.write_json(self.root / "summary.json", {"variants": rows})
        _, stdout, _ = invoke(compare_stroke, [str(path)])
        self.assertIn("| rect (default) | 10.00 | 25.00 | **2.50x** | 20.00 | 30.00 | **1.50x** |", stdout)
        self.assertIn("| stroke (rotation) | 0.00 | 60.00 | **0.00x** | 0.00 | 0.00 | **0.00x** |", stdout)
        self.assertIn("| rect (default) | 10.00 | 4.00 | 5.00 | 3.00 |", stdout)
        self.assertIn("| stroke (rotation) | 0.00 | 2.00 | 0.00 | 0.00 |", stdout)
        self.assertLess(stdout.index("| rect (default)"), stdout.index("| stroke (rotation)"))
        with self.assertRaises(SystemExit) as error:
            invoke(compare_stroke, [])
        self.assertEqual(error.exception.code, 1)


if __name__ == "__main__":
    unittest.main()

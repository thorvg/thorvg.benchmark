#!/usr/bin/env python3
"""Run benchmark variants described by ``config/engines.json``.

The manifest deliberately owns engine capabilities and command shapes. This
lets the suite mix the existing one-binary-per-workload C++ adapters with Rust
adapters that select a workload using ``--benchmark=<id>``.

A suite-level ``--capture=PATH`` extra argument is treated as a base filename;
the runner adds the variant, seed, and run number so captures cannot overwrite
one another within a suite.
"""

from __future__ import annotations

import argparse
import json
import math
import shlex
import statistics
import subprocess
import sys
import time
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = REPO_ROOT / "config" / "engines.json"
DEFAULT_SEED_BASE = 12345
RESULT_STAT_KEYS = (
    "avg_ms", "median_ms", "p95_ms", "p99_ms", "min_ms", "max_ms",
    "stddev_ms", "fps",
)
V2_RESULT_STATUSES = {
    "passed",
    "vsync-limited",
}
V2_METADATA_KEYS = {
    "benchmark", "engine", "engine_version", "engine_revision",
    "scene_model", "backend", "graphics_api", "gpu_device", "gpu_vendor",
    "gpu_driver", "gpu_completion", "timing_mode", "seed", "object_count",
    "resolution", "requested_resolution", "pixel_format", "vsync_requested",
    "vsync_actual", "present_mode", "scene_mode", "warmup_frames",
    "measured_frames", "build_type", "asset_hash",
}
COMPARISON_ENVIRONMENT_KEYS = (
    "graphics_api", "gpu_device", "gpu_vendor", "gpu_driver", "pixel_format",
)
GPU_COMPARISON_KEYS = ("gpu_device", "pixel_format")
UNKNOWN_COMPARISON_VALUES = {
    "", "none", "unknown", "unavailable", "unsupported", "n/a", "not available",
}


class ManifestError(ValueError):
    """Raised when the engine manifest is internally inconsistent."""


class ResultValidationError(ValueError):
    """Raised when a benchmark result does not match its requested variant."""


@dataclass(frozen=True)
class CommandSpec:
    executable: str
    args: tuple[str, ...]


@dataclass(frozen=True)
class BackendSpec:
    display_name: str
    api: str
    gpu: bool
    args: tuple[str, ...]
    workloads: tuple[str, ...] | None = None


@dataclass(frozen=True)
class EngineSpec:
    display_name: str
    scene_model: str
    command: CommandSpec
    workloads: tuple[str, ...]
    backends: Mapping[str, BackendSpec]
    engine_version: str = ""
    engine_revision: str = ""


@dataclass(frozen=True)
class CapabilityManifest:
    path: Path
    workloads: Mapping[str, str]
    engines: Mapping[str, EngineSpec]
    default_workloads: tuple[str, ...]
    default_engines: tuple[str, ...]
    default_scenes: tuple[str, ...]
    measured_frames: int
    warmup_frames: int
    object_count: int
    width: int
    height: int
    assets: Mapping[str, str]
    api_order: tuple[str, ...]

    @property
    def known_backends(self) -> tuple[str, ...]:
        return tuple(dict.fromkeys(
            backend_id
            for engine in self.engines.values()
            for backend_id in engine.backends
        ))


@dataclass(frozen=True)
class Variant:
    benchmark: str
    engine: str
    backend: str
    scene: str
    binary: str
    command_args: tuple[str, ...]
    engine_display_name: str
    backend_display_name: str
    api: str
    scene_model: str
    gpu: bool
    engine_version: str = ""
    engine_revision: str = ""
    supported: bool = True
    unsupported_reason: str = ""

    @property
    def name(self) -> str:
        return f"{self.benchmark}_{self.engine}_{self.backend}_{self.scene}"


@dataclass(frozen=True)
class RequestedRun:
    frames: int
    warmup: int
    width: int
    height: int
    object_count: int
    asset_hash: str
    vsync: bool
    gpu_sync: bool


@dataclass(frozen=True)
class ValidatedResult:
    schema_version: int
    status: str
    comparable: bool
    stats: Mapping[str, float]
    frame_count: int
    comparison_environment: Mapping[str, str] | None


def _load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as file:
        value = json.load(file)
    if not isinstance(value, dict):
        raise ValueError("top-level JSON value must be an object")
    return value


def _string_list(
    value: Any,
    context: str,
    *,
    allow_empty: bool = False,
    unique: bool = False,
) -> tuple[str, ...]:
    if not isinstance(value, list) or (not value and not allow_empty):
        qualifier = "an array" if allow_empty else "a non-empty array"
        raise ManifestError(f"{context} must be {qualifier} of strings")
    if not all(isinstance(item, str) and item for item in value):
        raise ManifestError(f"{context} must contain only non-empty strings")
    if unique and len(set(value)) != len(value):
        raise ManifestError(f"{context} must not contain duplicate values")
    return tuple(value)


def _optional_string_list(
    owner: Mapping[str, Any],
    key: str,
    context: str,
    *,
    default: tuple[str, ...] = (),
    allow_empty: bool = False,
    unique: bool = False,
) -> tuple[str, ...]:
    if key not in owner:
        return default
    return _string_list(
        owner[key], context, allow_empty=allow_empty, unique=unique
    )


def load_manifest(path: Path = DEFAULT_MANIFEST) -> CapabilityManifest:
    """Load and validate the capability manifest."""
    raw = _load_json(path)
    manifest_version = raw.get("manifest_version")
    if (
        isinstance(manifest_version, bool)
        or not isinstance(manifest_version, int)
        or manifest_version != 1
    ):
        raise ManifestError("manifest_version must be 1")

    raw_workloads = raw.get("workloads")
    if not isinstance(raw_workloads, dict) or not raw_workloads:
        raise ManifestError("workloads must be a non-empty object")
    workloads: dict[str, str] = {}
    for workload_id, workload in raw_workloads.items():
        if not isinstance(workload_id, str) or not workload_id:
            raise ManifestError("workload identifiers must be non-empty strings")
        if not isinstance(workload, dict):
            raise ManifestError(f"workloads.{workload_id} must be an object")
        display_name = workload.get("display_name")
        if not isinstance(display_name, str) or not display_name:
            raise ManifestError(f"workloads.{workload_id}.display_name is required")
        workloads[workload_id] = display_name

    raw_engines = raw.get("engines")
    if not isinstance(raw_engines, dict) or not raw_engines:
        raise ManifestError("engines must be a non-empty object")
    engines: dict[str, EngineSpec] = {}
    for engine_id, engine in raw_engines.items():
        context = f"engines.{engine_id}"
        if not isinstance(engine, dict):
            raise ManifestError(f"{context} must be an object")
        display_name = engine.get("display_name")
        scene_model = engine.get("scene_model")
        if not isinstance(display_name, str) or not display_name:
            raise ManifestError(f"{context}.display_name is required")
        if scene_model not in {"immediate", "retained"}:
            raise ManifestError(f"{context}.scene_model must be immediate or retained")

        raw_command = engine.get("command")
        if not isinstance(raw_command, dict):
            raise ManifestError(f"{context}.command must be an object")
        executable = raw_command.get("executable")
        if not isinstance(executable, str) or not executable:
            raise ManifestError(f"{context}.command.executable is required")
        command_args = _optional_string_list(
            raw_command, "args", f"{context}.command.args", allow_empty=True
        )
        engine_workloads = _optional_string_list(
            engine,
            "workloads",
            f"{context}.workloads",
            default=tuple(workloads),
            unique=True,
        )
        unknown_workloads = set(engine_workloads) - set(workloads)
        if unknown_workloads:
            raise ManifestError(
                f"{context}.workloads contains unknown IDs: {', '.join(sorted(unknown_workloads))}"
            )

        raw_backends = engine.get("backends")
        if not isinstance(raw_backends, dict) or not raw_backends:
            raise ManifestError(f"{context}.backends must be a non-empty object")
        backends: dict[str, BackendSpec] = {}
        for backend_id, backend in raw_backends.items():
            backend_context = f"{context}.backends.{backend_id}"
            if not isinstance(backend, dict):
                raise ManifestError(f"{backend_context} must be an object")
            backend_display_name = backend.get("display_name")
            api = backend.get("api")
            gpu = backend.get("gpu")
            if not isinstance(backend_display_name, str) or not backend_display_name:
                raise ManifestError(f"{backend_context}.display_name is required")
            if not isinstance(api, str) or not api:
                raise ManifestError(f"{backend_context}.api is required")
            if not isinstance(gpu, bool):
                raise ManifestError(f"{backend_context}.gpu must be a boolean")
            backend_args = _optional_string_list(
                backend, "args", f"{backend_context}.args", allow_empty=True
            )
            backend_workloads = None
            if "workloads" in backend:
                backend_workloads = _string_list(
                    backend["workloads"],
                    f"{backend_context}.workloads",
                    unique=True,
                )
                unknown = set(backend_workloads) - set(engine_workloads)
                if unknown:
                    raise ManifestError(
                        f"{backend_context}.workloads contains unsupported engine workloads: "
                        + ", ".join(sorted(unknown))
                    )
            backends[backend_id] = BackendSpec(
                display_name=backend_display_name,
                api=api,
                gpu=gpu,
                args=backend_args,
                workloads=backend_workloads,
            )

        version = engine.get("engine_version", "")
        revision = engine.get("engine_revision", "")
        if not isinstance(version, str) or not isinstance(revision, str):
            raise ManifestError(f"{context} version/revision values must be strings")
        engines[engine_id] = EngineSpec(
            display_name=display_name,
            scene_model=scene_model,
            command=CommandSpec(executable=executable, args=command_args),
            workloads=engine_workloads,
            backends=backends,
            engine_version=version,
            engine_revision=revision,
        )

    defaults = raw.get("defaults")
    if not isinstance(defaults, dict):
        raise ManifestError("defaults must be an object")
    default_workloads = _optional_string_list(
        defaults,
        "workloads",
        "defaults.workloads",
        default=tuple(workloads),
        unique=True,
    )
    default_engines = _optional_string_list(
        defaults,
        "engines",
        "defaults.engines",
        default=tuple(engines),
        unique=True,
    )
    default_scenes = _string_list(
        defaults.get("scenes"), "defaults.scenes", unique=True
    )
    if set(default_workloads) - set(workloads):
        raise ManifestError("defaults.workloads contains an unknown workload")
    if set(default_engines) - set(engines):
        raise ManifestError("defaults.engines contains an unknown engine")
    if any(scene not in {"default", "rotation"} for scene in default_scenes):
        raise ManifestError("defaults.scenes contains an unknown scene")

    numeric_defaults: dict[str, int] = {}
    for key in ("measured_frames", "warmup_frames", "object_count", "width", "height"):
        value = defaults.get(key)
        if not isinstance(value, int) or isinstance(value, bool) or value < 0:
            raise ManifestError(f"defaults.{key} must be a non-negative integer")
        numeric_defaults[key] = value
    if numeric_defaults["measured_frames"] == 0:
        raise ManifestError("defaults.measured_frames must be positive")
    if numeric_defaults["width"] == 0 or numeric_defaults["height"] == 0:
        raise ManifestError("default dimensions must be positive")
    if numeric_defaults["object_count"] == 0:
        raise ManifestError("defaults.object_count must be positive")

    raw_assets = raw.get("assets")
    if not isinstance(raw_assets, dict) or not raw_assets:
        raise ManifestError("assets must be a non-empty object")
    assets: dict[str, str] = {}
    for extension, digest in raw_assets.items():
        if extension not in {"png", "jpg"} or not isinstance(digest, str):
            raise ManifestError("assets must map png/jpg to SHA-256 strings")
        hex_digest = digest.removeprefix("sha256:")
        if (
            not digest.startswith("sha256:")
            or len(hex_digest) != 64
            or any(character not in "0123456789abcdefABCDEF" for character in hex_digest)
        ):
            raise ManifestError(f"assets.{extension} must be a sha256: digest")
        assets[extension] = digest
    if set(assets) != {"png", "jpg"}:
        raise ManifestError("assets must define both png and jpg")

    api_order = _string_list(raw.get("api_order"), "api_order", unique=True)
    return CapabilityManifest(
        path=path.resolve(),
        workloads=workloads,
        engines=engines,
        default_workloads=default_workloads,
        default_engines=default_engines,
        default_scenes=default_scenes,
        measured_frames=numeric_defaults["measured_frames"],
        warmup_frames=numeric_defaults["warmup_frames"],
        object_count=numeric_defaults["object_count"],
        width=numeric_defaults["width"],
        height=numeric_defaults["height"],
        assets=assets,
        api_order=api_order,
    )


def _summarize(values: Sequence[float]) -> dict[str, float]:
    if len(values) == 1:
        return {"mean": float(values[0]), "stdev": 0.0}
    return {"mean": float(statistics.fmean(values)), "stdev": float(statistics.stdev(values))}


def _csv(value: str) -> list[str]:
    return [part.strip().lower() for part in value.split(",") if part.strip()]


def _select(
    raw: str | None,
    *,
    known: Iterable[str],
    defaults: Sequence[str],
    kind: str,
    omitted_is_all: bool = False,
) -> tuple[list[str], bool]:
    known_values = list(known)
    if raw is None:
        return list(defaults), omitted_is_all
    values = _csv(raw)
    if not values:
        raise ValueError(f"at least one {kind} must be selected")
    if "all" in values:
        if len(values) != 1:
            raise ValueError(f"'all' cannot be combined with explicit {kind}")
        return known_values, True
    known_set = set(known_values)
    normalized = [
        value[:-5] if kind == "benchmark" and value.endswith("bench") else value
        for value in values
    ]
    unknown = [value for value in normalized if value not in known_set]
    if unknown:
        raise ValueError(f"unknown {kind}: {', '.join(unknown)}")
    return list(dict.fromkeys(normalized)), False


def _format_template(template: str, values: Mapping[str, str], context: str) -> str:
    try:
        return template.format_map(values)
    except (KeyError, ValueError) as error:
        raise ManifestError(f"invalid command template in {context}: {error}") from error


def make_variants(
    manifest: CapabilityManifest,
    bin_dir: Path,
    benchmarks: Sequence[str],
    engines: Sequence[str],
    backends: Sequence[str],
    scenes: Sequence[str],
    *,
    backends_all: bool,
) -> list[Variant]:
    """Expand selected capabilities, retaining explicit unsupported requests."""
    variants: list[Variant] = []
    for benchmark in benchmarks:
        for engine_id in engines:
            engine = manifest.engines[engine_id]
            selected_backends = list(engine.backends) if backends_all else list(backends)
            for backend_id in selected_backends:
                backend = engine.backends.get(backend_id)
                for scene in scenes:
                    if backend is None:
                        variants.append(Variant(
                            benchmark=benchmark, engine=engine_id, backend=backend_id,
                            scene=scene, binary="", command_args=(),
                            engine_display_name=engine.display_name,
                            backend_display_name=backend_id, api="Unknown",
                            scene_model=engine.scene_model, gpu=False,
                            engine_version=engine.engine_version,
                            engine_revision=engine.engine_revision,
                            supported=False,
                            unsupported_reason=f"{engine_id} does not support backend {backend_id}",
                        ))
                        continue

                    supported_workloads = backend.workloads or engine.workloads
                    supported = benchmark in supported_workloads
                    values = {
                        "workload": benchmark, "engine": engine_id,
                        "backend": backend_id, "scene": scene,
                    }
                    rendered_executable = _format_template(
                        engine.command.executable, values,
                        f"engines.{engine_id}.command.executable",
                    )
                    executable = Path(rendered_executable)
                    if not executable.is_absolute():
                        executable = bin_dir / executable
                    rendered_args = tuple(
                        _format_template(arg, values, f"engines.{engine_id}.command.args")
                        for arg in (*engine.command.args, *backend.args)
                    )
                    variants.append(Variant(
                        benchmark=benchmark, engine=engine_id, backend=backend_id,
                        scene=scene, binary=str(executable.resolve()),
                        command_args=rendered_args,
                        engine_display_name=engine.display_name,
                        backend_display_name=backend.display_name, api=backend.api,
                        scene_model=engine.scene_model, gpu=backend.gpu,
                        engine_version=engine.engine_version,
                        engine_revision=engine.engine_revision,
                        supported=supported,
                        unsupported_reason=(
                            "" if supported
                            else f"{engine_id}/{backend_id} does not support {benchmark}"
                        ),
                    ))
    return variants


def _splitmix64(value: int) -> int:
    value = (value + 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF
    mixed = value
    mixed = (mixed ^ (mixed >> 30)) * 0xBF58476D1CE4E5B9 & 0xFFFFFFFFFFFFFFFF
    mixed = (mixed ^ (mixed >> 27)) * 0x94D049BB133111EB & 0xFFFFFFFFFFFFFFFF
    return (mixed ^ (mixed >> 31)) & 0xFFFFFFFFFFFFFFFF


def _pick_seeds(runs: int, fixed_seed: int | None, seed_base: int | None) -> list[int]:
    if fixed_seed is not None:
        return [fixed_seed for _ in range(runs)]
    base = int(seed_base if seed_base is not None else DEFAULT_SEED_BASE) & 0xFFFFFFFFFFFFFFFF
    return [_splitmix64(base + index) for index in range(runs)]


def execution_schedule(
    variants: Sequence[Variant], seeds: Sequence[int]
) -> list[tuple[Variant, int, int]]:
    """Create matched-seed runs with cyclically counterbalanced engine order."""
    groups: dict[tuple[str, str, str], list[Variant]] = defaultdict(list)
    for variant in variants:
        if variant.supported:
            groups[(variant.benchmark, variant.backend, variant.scene)].append(variant)

    schedule: list[tuple[Variant, int, int]] = []
    ordered_group_keys = sorted(groups)
    for run_index, seed in enumerate(seeds):
        if ordered_group_keys:
            offset = run_index % len(ordered_group_keys)
            run_group_keys = ordered_group_keys[offset:] + ordered_group_keys[:offset]
        else:
            run_group_keys = []
        for group_key in run_group_keys:
            group = groups[group_key]
            offset = run_index % len(group)
            counterbalanced = group[offset:] + group[:offset]
            schedule.extend((variant, run_index, seed) for variant in counterbalanced)
    return schedule


def _kv_arg(args: Sequence[str], key: str) -> str | None:
    prefix = f"--{key}="
    found = None
    for arg in args:
        if arg.startswith(prefix):
            found = arg[len(prefix):]
    return found


def _bool_value(value: str, context: str) -> bool:
    lowered = value.lower()
    if lowered in {"1", "true", "yes"}:
        return True
    if lowered in {"0", "false", "no"}:
        return False
    raise ValueError(f"{context} must be 0 or 1")


def _int_arg(args: Sequence[str], key: str, default: int, *, positive: bool) -> int:
    raw = _kv_arg(args, key)
    if raw is None:
        return default
    try:
        value = int(raw)
    except ValueError as error:
        raise ValueError(f"--{key} must be an integer") from error
    if value < (1 if positive else 0):
        qualifier = "positive" if positive else "non-negative"
        raise ValueError(f"--{key} must be {qualifier}")
    return value


def requested_run(
    manifest: CapabilityManifest, extra_args: Sequence[str], *, gpu: bool
) -> RequestedRun:
    raw_vsync = _kv_arg(extra_args, "vsync")
    raw_gpu_sync = _kv_arg(extra_args, "gpu_sync")
    image_extension = _kv_arg(extra_args, "image") or "png"
    if image_extension not in manifest.assets:
        raise ValueError("--image must be png or jpg")
    capture_path = _kv_arg(extra_args, "capture")
    if capture_path is not None and (not capture_path or not Path(capture_path).name):
        raise ValueError("--capture must name a file")
    return RequestedRun(
        frames=_int_arg(extra_args, "frames", manifest.measured_frames, positive=True),
        warmup=_int_arg(extra_args, "warmup", manifest.warmup_frames, positive=False),
        width=_int_arg(extra_args, "width", manifest.width, positive=True),
        height=_int_arg(extra_args, "height", manifest.height, positive=True),
        object_count=manifest.object_count,
        asset_hash=manifest.assets[image_extension],
        vsync=False if raw_vsync is None else _bool_value(raw_vsync, "--vsync"),
        gpu_sync=(gpu if raw_gpu_sync is None else _bool_value(raw_gpu_sync, "--gpu_sync")),
    )


def _strip_conflicting_kv_args(
    args: Sequence[str], *, keys: set[str]
) -> tuple[list[str], list[str]]:
    kept: list[str] = []
    stripped: list[str] = []
    prefixes = tuple(f"--{key}=" for key in keys)
    for arg in args:
        (stripped if arg.startswith(prefixes) else kept).append(arg)
    return kept, stripped


def _derived_capture_path(
    requested_path: str, variant: Variant, seed: int, run_index: int
) -> Path:
    """Turn one capture base path into a unique path for this suite run."""
    if not requested_path:
        raise ValueError("--capture must name a file")
    capture_path = Path(requested_path)
    suffix = capture_path.suffix
    stem = capture_path.name[: -len(suffix)] if suffix else capture_path.name
    derived_name = (
        f"{stem}_{variant.name}_seed{seed}_run{run_index + 1:02d}{suffix}"
    )
    return capture_path.with_name(derived_name)


def build_command(
    variant: Variant,
    seed: int,
    output_path: Path,
    extra_args: Sequence[str],
    *,
    requested: RequestedRun,
    run_index: int,
) -> list[str]:
    command = [
        variant.binary, *variant.command_args, f"--backend={variant.backend}",
        f"--scene={variant.scene}", f"--seed={seed}", f"--output={output_path}",
    ]
    for key, value in (
        ("frames", requested.frames),
        ("warmup", requested.warmup),
        ("width", requested.width),
        ("height", requested.height),
    ):
        if _kv_arg(extra_args, key) is None:
            command.append(f"--{key}={value}")
    if _kv_arg(extra_args, "vsync") is None:
        command.append("--vsync=0")
    if variant.gpu and _kv_arg(extra_args, "gpu_sync") is None:
        command.append("--gpu_sync=1")
    capture_path = _kv_arg(extra_args, "capture")
    forwarded_args = [
        argument
        for argument in extra_args
        if not argument.startswith("--capture=")
    ]
    if capture_path is not None:
        derived_capture = _derived_capture_path(
            capture_path, variant, seed, run_index
        )
        command.append(f"--capture={derived_capture}")
    command.extend(forwarded_args)
    return command


def _numeric_stats(stats: Any, *, require_all: bool) -> dict[str, float]:
    if not isinstance(stats, dict):
        raise ResultValidationError("stats must be an object")
    converted: dict[str, float] = {}
    for key, value in stats.items():
        if (
            isinstance(value, bool)
            or not isinstance(value, (int, float))
            or not math.isfinite(value)
        ):
            raise ResultValidationError(f"stats.{key} must be numeric")
        converted[key] = float(value)
    if require_all:
        missing = [key for key in RESULT_STAT_KEYS if key not in converted]
        if missing:
            raise ResultValidationError(f"stats missing keys: {', '.join(missing)}")
    return converted


def _percentile(sorted_samples: Sequence[float], percentile: float) -> float:
    if len(sorted_samples) == 1:
        return sorted_samples[0]
    position = percentile / 100.0 * (len(sorted_samples) - 1)
    lower = math.floor(position)
    upper = math.ceil(position)
    fraction = position - lower
    return (
        sorted_samples[lower] * (1.0 - fraction)
        + sorted_samples[upper] * fraction
    )


def _stats_from_samples(samples: Sequence[float]) -> dict[str, float]:
    if not samples:
        return {key: 0.0 for key in RESULT_STAT_KEYS}
    sorted_samples = sorted(samples)
    average = math.fsum(sorted_samples) / len(sorted_samples)
    variance = math.fsum((sample - average) ** 2 for sample in sorted_samples) / len(
        sorted_samples
    )
    return {
        "avg_ms": average,
        "median_ms": _percentile(sorted_samples, 50.0),
        "p95_ms": _percentile(sorted_samples, 95.0),
        "p99_ms": _percentile(sorted_samples, 99.0),
        "min_ms": sorted_samples[0],
        "max_ms": sorted_samples[-1],
        "stddev_ms": math.sqrt(variance),
        "fps": 1000.0 / average if average > 0.0 else 0.0,
    }


def _expect_metadata(metadata: Mapping[str, Any], key: str, expected: Any) -> None:
    actual = metadata.get(key)
    if actual != expected:
        raise ResultValidationError(f"metadata.{key}={actual!r}; expected {expected!r}")


def _validate_v1(
    data: Mapping[str, Any], variant: Variant, seed: int, requested: RequestedRun
) -> ValidatedResult:
    metadata = data.get("metadata")
    if not isinstance(metadata, dict):
        raise ResultValidationError("metadata must be an object")
    _expect_metadata(metadata, "engine", variant.engine)
    _expect_metadata(metadata, "backend", variant.backend)
    _expect_metadata(metadata, "seed", seed)
    _expect_metadata(metadata, "scene_mode", variant.scene)
    _expect_metadata(metadata, "measured_frames", requested.frames)
    _expect_metadata(metadata, "warmup_frames", requested.warmup)
    _expect_metadata(metadata, "resolution", f"{requested.width}x{requested.height}")
    stats = _numeric_stats(data.get("stats"), require_all=True)
    return ValidatedResult(
        schema_version=1, status="legacy-v1", comparable=False, stats=stats,
        frame_count=0, comparison_environment=None,
    )


def _api_matches(actual: str, expected: str) -> bool:
    normalized_actual = actual.strip().lower()
    normalized_expected = expected.strip().lower()
    return normalized_actual == normalized_expected or normalized_actual.startswith(
        normalized_expected + " "
    )


def _is_software_gpu(metadata: Mapping[str, Any]) -> bool:
    description = " ".join(
        str(metadata.get(key, ""))
        for key in ("gpu_device", "gpu_vendor", "gpu_driver")
    ).lower()
    return any(
        marker in description
        for marker in (
            "llvmpipe",
            "lavapipe",
            "softpipe",
            "swrast",
            "swiftshader",
            "software rasterizer",
            "microsoft basic render driver",
            "direct3d warp",
            "device_type=cpu",
            "[cpu]",
        )
    )


def _normalize_comparison_value(value: str) -> str:
    return " ".join(value.split()).casefold()


def _invalid_comparison_environment_fields(environment: Any) -> tuple[str, ...]:
    if not isinstance(environment, Mapping):
        return GPU_COMPARISON_KEYS
    invalid = []
    for key in GPU_COMPARISON_KEYS:
        value = environment.get(key)
        if not isinstance(value, str):
            invalid.append(key)
            continue
        if _normalize_comparison_value(value) in UNKNOWN_COMPARISON_VALUES:
            invalid.append(key)
    return tuple(invalid)


def _comparison_environment_key(environment: Any) -> tuple[str, str] | None:
    if _invalid_comparison_environment_fields(environment):
        return None
    return tuple(
        _normalize_comparison_value(environment[key])
        for key in GPU_COMPARISON_KEYS
    )


def _different_comparison_fields(
    keys: Sequence[tuple[str, str]],
) -> tuple[str, ...]:
    return tuple(
        field
        for index, field in enumerate(GPU_COMPARISON_KEYS)
        if len({key[index] for key in keys}) > 1
    )


def _comparison_field_names(fields: Sequence[str]) -> str:
    labels = {
        "gpu_device": "GPU device",
        "pixel_format": "pixel format",
    }
    rendered = [labels[field] for field in fields]
    if len(rendered) == 1:
        return rendered[0]
    return " and ".join(rendered)


def _validate_v2(
    data: Mapping[str, Any], variant: Variant, seed: int, requested: RequestedRun
) -> ValidatedResult:
    status = data.get("status")
    if status not in V2_RESULT_STATUSES:
        raise ResultValidationError(
            "status must be one of " + ", ".join(sorted(V2_RESULT_STATUSES))
        )
    comparable = data.get("comparable")
    if not isinstance(comparable, bool):
        raise ResultValidationError("comparable must be a boolean")
    if status != "passed" and comparable:
        raise ResultValidationError(f"status {status!r} cannot be comparable")

    metadata = data.get("metadata")
    if not isinstance(metadata, dict):
        raise ResultValidationError("metadata must be an object")
    missing_metadata = sorted(V2_METADATA_KEYS - set(metadata))
    if missing_metadata:
        raise ResultValidationError("metadata missing keys: " + ", ".join(missing_metadata))

    _expect_metadata(metadata, "benchmark", variant.benchmark)
    _expect_metadata(metadata, "engine", variant.engine)
    _expect_metadata(metadata, "backend", variant.backend)
    _expect_metadata(metadata, "scene_model", variant.scene_model)
    _expect_metadata(metadata, "scene_mode", variant.scene)
    _expect_metadata(metadata, "seed", seed)
    _expect_metadata(metadata, "warmup_frames", requested.warmup)
    if variant.engine_version:
        _expect_metadata(metadata, "engine_version", variant.engine_version)
    if variant.engine_revision:
        _expect_metadata(metadata, "engine_revision", variant.engine_revision)

    expected_resolution = f"{requested.width}x{requested.height}"
    _expect_metadata(metadata, "requested_resolution", expected_resolution)
    _expect_metadata(metadata, "vsync_requested", requested.vsync)
    if not isinstance(metadata.get("vsync_actual"), bool):
        raise ResultValidationError("metadata.vsync_actual must be a boolean")
    _expect_metadata(metadata, "object_count", requested.object_count)
    for key in (
        "engine_version", "engine_revision", "graphics_api", "gpu_device",
        "gpu_vendor", "gpu_driver", "gpu_completion", "timing_mode",
        "resolution", "requested_resolution", "pixel_format", "present_mode",
        "build_type", "asset_hash",
    ):
        if not isinstance(metadata.get(key), str):
            raise ResultValidationError(f"metadata.{key} must be a string")
    expected_asset_hash = requested.asset_hash if variant.benchmark == "image" else ""
    _expect_metadata(metadata, "asset_hash", expected_asset_hash)
    if not _api_matches(metadata["graphics_api"], variant.api):
        raise ResultValidationError(
            f"metadata.graphics_api={metadata['graphics_api']!r}; expected {variant.api!r}"
        )

    measured_frames = metadata.get("measured_frames")
    if not isinstance(measured_frames, int) or isinstance(measured_frames, bool):
        raise ResultValidationError("metadata.measured_frames must be an integer")
    if measured_frames != requested.frames:
        raise ResultValidationError(
            f"metadata.measured_frames={measured_frames!r}; expected {requested.frames!r}"
        )
    if metadata["resolution"] != expected_resolution:
        raise ResultValidationError(
            f"metadata.resolution={metadata['resolution']!r}; expected {expected_resolution!r}"
        )

    frame_times = data.get("frame_times_ms")
    if not isinstance(frame_times, list) or any(
        isinstance(value, bool)
        or not isinstance(value, (int, float))
        or not math.isfinite(value)
        or value < 0.0
        for value in frame_times
    ):
        raise ResultValidationError("frame_times_ms must be an array of numbers")
    if len(frame_times) != measured_frames:
        raise ResultValidationError(
            f"frame_times_ms has {len(frame_times)} samples; metadata says {measured_frames}"
        )

    stats = _numeric_stats(data.get("stats"), require_all=True)
    calculated_stats = _stats_from_samples(frame_times)
    for key in RESULT_STAT_KEYS:
        if not math.isclose(
            stats[key], calculated_stats[key], rel_tol=1e-5, abs_tol=1e-6
        ):
            raise ResultValidationError(
                f"stats.{key}={stats[key]!r} does not match raw frame samples "
                f"({calculated_stats[key]!r})"
            )
    stats = calculated_stats
    if variant.gpu:
        if requested.gpu_sync:
            if metadata["timing_mode"] != "onscreen_gpu_complete":
                raise ResultValidationError(
                    "synchronized GPU results must use timing_mode='onscreen_gpu_complete'"
                )
            completion = metadata["gpu_completion"].strip().lower()
            if completion in {"", "none", "unknown", "unavailable", "unsupported", "n/a"}:
                raise ResultValidationError(
                    "synchronized GPU results must name a real gpu_completion mechanism"
                )
        else:
            if not (
                metadata["timing_mode"].startswith("diagnostic")
                or metadata["timing_mode"] == "onscreen_submit"
            ):
                raise ResultValidationError(
                    "asynchronous GPU runs must use a diagnostic timing mode"
                )
            if comparable:
                raise ResultValidationError("asynchronous GPU diagnostics cannot be comparable")
        if metadata["vsync_actual"] and status != "vsync-limited":
            raise ResultValidationError(
                "VSync-active GPU results must use status='vsync-limited'"
            )
    else:
        if metadata["timing_mode"] != "onscreen_cpu_complete":
            raise ResultValidationError(
                "CPU results must use timing_mode='onscreen_cpu_complete'"
            )
        if metadata["gpu_completion"] != "none":
            raise ResultValidationError(
                "CPU results must use gpu_completion='none'"
            )

    if comparable:
        if requested.vsync or metadata["vsync_actual"]:
            raise ResultValidationError("VSync-enabled results cannot be comparable")
        if metadata["resolution"] != metadata["requested_resolution"]:
            raise ResultValidationError(
                "comparable results require the exact requested resolution"
            )
    if metadata["build_type"].strip().lower() != "release":
        comparable = False
    if variant.gpu and _is_software_gpu(metadata):
        comparable = False
    comparison_environment = None
    if variant.gpu:
        comparison_environment = {
            key: metadata[key] for key in COMPARISON_ENVIRONMENT_KEYS
        }
        if _comparison_environment_key(comparison_environment) is None:
            comparable = False
    return ValidatedResult(
        schema_version=2, status=status, comparable=comparable, stats=stats,
        frame_count=len(frame_times),
        comparison_environment=comparison_environment,
    )


def validate_result(
    data: Mapping[str, Any], variant: Variant, seed: int, requested: RequestedRun
) -> ValidatedResult:
    """Validate a v2 result or read a legacy v1 result as non-comparable."""
    schema_version = data.get("schema_version", 1)
    if isinstance(schema_version, bool) or not isinstance(schema_version, int):
        raise ResultValidationError("schema_version must be an integer")
    if schema_version == 1:
        return _validate_v1(data, variant, seed, requested)
    if schema_version == 2:
        return _validate_v2(data, variant, seed, requested)
    raise ResultValidationError(f"unsupported result schema_version: {schema_version!r}")


def _default_output_dir() -> str:
    return time.strftime("results_suite_%Y%m%d_%H%M%S")


def _variant_entry(variant: Variant) -> dict[str, Any]:
    return {
        "benchmark": variant.benchmark,
        "engine": variant.engine,
        "engine_display_name": variant.engine_display_name,
        "backend": variant.backend,
        "backend_display_name": variant.backend_display_name,
        "graphics_api": variant.api,
        "scene_model": variant.scene_model,
        "scene": variant.scene,
        "gpu": variant.gpu,
        "binary": variant.binary,
        "command_args": list(variant.command_args),
        "status": "pending" if variant.supported else "unsupported",
        "comparable": False,
        "reason": "" if variant.supported else variant.unsupported_reason,
        "outputs": [],
        "runs": [],
        "comparison_environment": None,
        "summary": {},
    }


def _finalize_entry(entry: dict[str, Any], runs_per_variant: int) -> None:
    if entry["status"] in {"unsupported", "build-missing", "run-failed"} and not entry["runs"]:
        return
    runs = entry["runs"]
    if len(runs) != runs_per_variant:
        if entry["status"] == "pending":
            entry["status"] = "run-failed"
            entry["reason"] = f"completed {len(runs)} of {runs_per_variant} requested runs"
        return

    statuses = [run["status"] for run in runs]
    if "vsync-limited" in statuses:
        entry["status"] = "vsync-limited"
    elif all(status == "legacy-v1" for status in statuses):
        entry["status"] = "legacy-v1"
    elif all(status == "passed" for status in statuses):
        entry["status"] = "passed"
    else:
        entry["status"] = "run-failed"
        entry["reason"] = "mixed result schemas or statuses"

    entry["comparable"] = (
        entry["status"] == "passed"
        and all(run.get("schema_version") == 2 and run.get("comparable") for run in runs)
    )
    values: dict[str, list[float]] = defaultdict(list)
    for run in runs:
        for key, value in run.get("stats", {}).items():
            if isinstance(value, (int, float)) and not isinstance(value, bool):
                values[key].append(float(value))
    entry["summary"] = {
        key: _summarize(metric_values) for key, metric_values in values.items()
    }

    if not entry["gpu"]:
        return
    v2_runs = [run for run in runs if run.get("schema_version") == 2]
    if not v2_runs:
        return
    environments = [run.get("comparison_environment") for run in v2_runs]
    first_environment = environments[0]
    if isinstance(first_environment, Mapping):
        entry["comparison_environment"] = dict(first_environment)
    if entry["status"] != "passed":
        return

    invalid_fields = tuple(dict.fromkeys(
        field
        for environment in environments
        for field in _invalid_comparison_environment_fields(environment)
    ))
    if invalid_fields:
        entry["comparable"] = False
        entry["reason"] = (
            "missing or placeholder GPU comparison metadata: "
            + _comparison_field_names(invalid_fields)
        )
        return

    environment_keys = [
        key
        for environment in environments
        if (key := _comparison_environment_key(environment)) is not None
    ]
    changed_fields = _different_comparison_fields(environment_keys)
    if changed_fields:
        entry["comparable"] = False
        entry["reason"] = (
            "GPU environment changed between repeats: "
            + _comparison_field_names(changed_fields)
        )


def enforce_gpu_comparability(entries: Iterable[dict[str, Any]]) -> None:
    """Prevent rankings across incompatible GPU environments in one report row."""
    groups: dict[tuple[str, str, str], list[dict[str, Any]]] = defaultdict(list)
    for entry in entries:
        if (
            entry.get("gpu") is not True
            or entry.get("status") != "passed"
            or entry.get("comparable") is not True
        ):
            continue
        environment = entry.get("comparison_environment")
        invalid_fields = _invalid_comparison_environment_fields(environment)
        if invalid_fields:
            entry["comparable"] = False
            entry["reason"] = (
                "missing or placeholder GPU comparison metadata: "
                + _comparison_field_names(invalid_fields)
            )
            continue
        group = (
            str(entry.get("benchmark", "")),
            str(entry.get("scene", "")),
            str(entry.get("graphics_api", "")),
        )
        groups[group].append(entry)

    for (benchmark, scene, api), group in groups.items():
        environment_keys = [
            _comparison_environment_key(entry["comparison_environment"])
            for entry in group
        ]
        distinct_keys = set(environment_keys)
        if len(distinct_keys) <= 1:
            continue
        changed_fields = _different_comparison_fields([
            key for key in environment_keys if key is not None
        ])
        field_names = _comparison_field_names(changed_fields)
        verb = "differs" if len(changed_fields) == 1 else "differ"
        reason = (
            f"{field_names} {verb} across {api} engines for {benchmark}/{scene}"
        )
        for entry in group:
            entry["comparable"] = False
            entry["reason"] = reason


def _write_summary(path: Path, summary: Mapping[str, Any]) -> None:
    path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Run manifest-defined benchmark engines, backends, workloads, and scenes.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--manifest", default=str(DEFAULT_MANIFEST), help="Engine capability manifest")
    parser.add_argument("--build", action="store_true", help="Build configured CMake targets first")
    parser.add_argument("--build-dir", default="build", help="CMake build directory")
    parser.add_argument("--build-jobs", type=int, default=8, help="Parallel CMake build jobs")
    parser.add_argument("--runs", type=int, default=3, help="Repeats per supported variant")
    parser.add_argument("--seed", type=int, default=None, help="Use one fixed seed for every repeat")
    parser.add_argument("--seed-base", type=int, default=None, help="Base for deterministic per-run seeds")
    parser.add_argument(
        "--benchmarks",
        default=None,
        help="Comma-separated workload IDs or 'all' (omitted: manifest defaults)",
    )
    parser.add_argument(
        "--engines",
        default=None,
        help="Comma-separated engine IDs or 'all' (omitted: manifest defaults)",
    )
    parser.add_argument(
        "--backends",
        default=None,
        help="Comma-separated backend IDs or 'all' (omitted/all: every backend per engine)",
    )
    parser.add_argument(
        "--scenes",
        default=None,
        help="Comma-separated scenes or 'all' (omitted: manifest defaults)",
    )
    parser.add_argument("--bin-dir", default=None, help="Directory containing all benchmark binaries")
    parser.add_argument("--output-dir", default=None, help="Directory for logs, results, and summary")
    parser.add_argument("--timeout-seconds", type=float, default=None, help="Per-process timeout")
    parser.add_argument("--dry-run", action="store_true", help="Print the schedule without executing it")
    parser.add_argument(
        "extra_args", nargs=argparse.REMAINDER,
        help=(
            "Arguments passed to every benchmark (prefix with --); "
            "--capture=PATH is expanded to a unique file for each run"
        ),
    )
    args = parser.parse_args(argv)

    if args.runs <= 0:
        parser.error("--runs must be >= 1")
    if args.build_jobs <= 0:
        parser.error("--build-jobs must be >= 1")
    if args.timeout_seconds is not None and args.timeout_seconds <= 0:
        parser.error("--timeout-seconds must be positive")
    if args.seed is not None and args.seed_base is not None:
        parser.error("use only one of --seed or --seed-base")
    selected_seed = args.seed if args.seed is not None else args.seed_base
    if selected_seed is not None and not 0 <= selected_seed <= 0xFFFFFFFFFFFFFFFF:
        parser.error("--seed and --seed-base must fit in an unsigned 64-bit integer")

    try:
        manifest = load_manifest(Path(args.manifest))
        benchmarks, _ = _select(
            args.benchmarks, known=manifest.workloads,
            defaults=manifest.default_workloads, kind="benchmark",
        )
        engines, _ = _select(
            args.engines, known=manifest.engines,
            defaults=manifest.default_engines, kind="engine",
        )
        backends, backends_all = _select(
            args.backends, known=manifest.known_backends,
            defaults=manifest.known_backends, kind="backend", omitted_is_all=True,
        )
        scenes, _ = _select(
            args.scenes, known=("default", "rotation"),
            defaults=manifest.default_scenes, kind="scene",
        )
    except (OSError, json.JSONDecodeError, ManifestError, ValueError) as error:
        parser.error(str(error))

    extra_args = list(args.extra_args)
    if extra_args and extra_args[0] == "--":
        extra_args = extra_args[1:]
    extra_args, stripped = _strip_conflicting_kv_args(
        extra_args, keys={"seed", "output", "backend", "scene", "benchmark"}
    )
    if stripped:
        print(
            "Note: ignoring arguments controlled by the suite: " + " ".join(stripped),
            file=sys.stderr,
        )

    try:
        requested_runs = {
            gpu: requested_run(manifest, extra_args, gpu=gpu)
            for gpu in (False, True)
        }
    except ValueError as error:
        parser.error(str(error))

    if args.build:
        build_command_line = [
            "cmake", "--build", args.build_dir, "-j", str(args.build_jobs)
        ]
        print(f"Building: {shlex.join(build_command_line)}", flush=True)
        if not args.dry_run:
            build = subprocess.run(build_command_line)
            if build.returncode != 0:
                return build.returncode

    output_dir = Path(args.output_dir or _default_output_dir()).resolve()
    if not args.dry_run:
        output_dir.mkdir(parents=True, exist_ok=True)
    bin_dir = Path(args.bin_dir or args.build_dir)
    variants = make_variants(
        manifest, bin_dir, benchmarks, engines, backends, scenes,
        backends_all=backends_all,
    )

    seeds = _pick_seeds(args.runs, args.seed, args.seed_base)
    entries = {variant.name: _variant_entry(variant) for variant in variants}
    for variant in variants:
        if not variant.supported:
            print(
                f"[unsupported] {variant.name}: {variant.unsupported_reason}",
                flush=True,
            )
    if not args.dry_run:
        for variant in variants:
            if variant.supported and not Path(variant.binary).is_file():
                entry = entries[variant.name]
                entry["status"] = "build-missing"
                entry["reason"] = f"benchmark executable not found: {variant.binary}"

    runnable = [
        variant for variant in variants
        if variant.supported
        and (args.dry_run or entries[variant.name]["status"] != "build-missing")
    ]
    schedule = execution_schedule(runnable, seeds)
    for variant, run_index, seed in schedule:
        entry = entries[variant.name]
        if entry["status"] == "run-failed":
            continue
        variant_dir = (
            output_dir / variant.benchmark / variant.engine / variant.backend / variant.scene
        )
        if not args.dry_run:
            variant_dir.mkdir(parents=True, exist_ok=True)
        output_json = (
            variant_dir / f"{variant.name}_seed{seed}_run{run_index + 1:02d}.json"
        ).resolve()
        output_log = (
            variant_dir / f"{variant.name}_seed{seed}_run{run_index + 1:02d}.log"
        )
        requested = requested_runs[variant.gpu]
        command = build_command(
            variant,
            seed,
            output_json,
            extra_args,
            requested=requested,
            run_index=run_index,
        )
        print(
            f"[{run_index + 1}/{args.runs}] {variant.name}: {shlex.join(command)}",
            flush=True,
        )
        if args.dry_run:
            continue

        # A successful process must produce this run's result. Never let a
        # deterministic path from an earlier invocation satisfy that gate.
        try:
            output_json.unlink(missing_ok=True)
        except OSError as error:
            entry["status"] = "run-failed"
            entry["reason"] = (
                f"could not clear prior result {output_json}: {error}"
            )
            continue

        try:
            process = subprocess.run(
                command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, timeout=args.timeout_seconds,
            )
        except subprocess.TimeoutExpired as error:
            captured = error.stdout or ""
            if isinstance(captured, bytes):
                captured = captured.decode(errors="replace")
            output_log.write_text(captured + "\n[TIMEOUT]\n", encoding="utf-8")
            entry["status"] = "run-failed"
            entry["reason"] = f"timed out after {args.timeout_seconds}s"
            continue
        except OSError as error:
            entry["status"] = "run-failed"
            entry["reason"] = f"could not launch benchmark: {error}"
            continue

        output_log.write_text(process.stdout, encoding="utf-8")
        if process.returncode != 0:
            entry["status"] = "run-failed"
            entry["reason"] = (
                f"benchmark exited with {process.returncode}; see {output_log}"
            )
            continue
        if not output_json.is_file() or output_json.stat().st_size == 0:
            entry["status"] = "run-failed"
            entry["reason"] = (
                f"benchmark did not write a non-empty result: {output_json}"
            )
            continue

        try:
            result_data = _load_json(output_json)
            validated = validate_result(result_data, variant, seed, requested)
        except (OSError, json.JSONDecodeError, ValueError) as error:
            entry["status"] = "run-failed"
            entry["reason"] = f"invalid result {output_json}: {error}"
            continue

        entry["outputs"].append(str(output_json))
        run_entry = {
            "run": run_index + 1,
            "seed": seed,
            "output": str(output_json),
            "schema_version": validated.schema_version,
            "status": validated.status,
            "comparable": validated.comparable,
            "stats": dict(validated.stats),
            "frame_count": validated.frame_count,
        }
        if validated.comparison_environment is not None:
            run_entry["comparison_environment"] = dict(
                validated.comparison_environment
            )
        entry["runs"].append(run_entry)

    for entry in entries.values():
        _finalize_entry(entry, args.runs)
    enforce_gpu_comparability(entries.values())
    fatal = not args.dry_run and any(
        entry["status"] in {
            "run-failed", "build-missing", "unsupported"
        }
        for entry in entries.values()
    )

    suite_summary: dict[str, Any] = {
        "suite_schema_version": 2,
        "status": "incomplete" if fatal else "complete",
        "manifest": str(manifest.path),
        "runs_per_variant": args.runs,
        "fixed_seed": args.seed,
        "seed_base": args.seed_base,
        "seeds": seeds,
        "counterbalance": (
            "cyclic engine order within benchmark/backend/scene groups"
        ),
        "variants": list(entries.values()),
    }
    if not args.dry_run:
        summary_path = output_dir / "summary.json"
        _write_summary(summary_path, suite_summary)
        print(f"Wrote suite summary: {summary_path}")
        counts: dict[str, int] = defaultdict(int)
        for entry in entries.values():
            counts[entry["status"]] += 1
        print(
            "Variant statuses: "
            + ", ".join(f"{key}={value}" for key, value in sorted(counts.items()))
        )
    return 2 if fatal else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

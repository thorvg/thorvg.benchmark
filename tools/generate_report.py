#!/usr/bin/env python3
"""Generate API-grouped Markdown from a ``run_all.py`` suite summary."""

from __future__ import annotations

import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any, Mapping, Sequence

from run_all import (
    DEFAULT_MANIFEST,
    CapabilityManifest,
    enforce_gpu_comparability,
    load_manifest,
)


STATUS_LABELS = {
    "unsupported": "unsupported",
    "build-missing": "build missing",
    "run-failed": "run failed",
    "vsync-limited": "VSync-limited",
    "pending": "not run",
}


def _markdown(value: Any) -> str:
    return str(value).replace("|", "\\|").replace("\n", " ")


def _manifest_api(
    manifest: CapabilityManifest, engine_id: str, backend_id: str
) -> str:
    engine = manifest.engines.get(engine_id)
    if engine is not None and backend_id in engine.backends:
        return engine.backends[backend_id].api
    candidates = {
        engine_spec.backends[backend_id].api
        for engine_spec in manifest.engines.values()
        if backend_id in engine_spec.backends
    }
    if len(candidates) == 1:
        return next(iter(candidates))
    return "Other"


def _variant_api(variant: Mapping[str, Any], manifest: CapabilityManifest) -> str:
    manifest_api = _manifest_api(
        manifest, str(variant.get("engine", "")), str(variant.get("backend", ""))
    )
    if manifest_api != "Other":
        return manifest_api
    explicit = variant.get("graphics_api")
    if isinstance(explicit, str) and explicit and explicit != "Unknown":
        return explicit
    return manifest_api


def _variant_gpu(variant: Mapping[str, Any], manifest: CapabilityManifest) -> bool:
    engine = manifest.engines.get(str(variant.get("engine", "")))
    if engine is not None:
        backend = engine.backends.get(str(variant.get("backend", "")))
        if backend is not None:
            return backend.gpu
    return variant.get("gpu") is True


def _column_label(variant: Mapping[str, Any], manifest: CapabilityManifest) -> str:
    engine_id = str(variant.get("engine", "Unknown"))
    backend_id = str(variant.get("backend", "Unknown"))
    engine = manifest.engines.get(engine_id)
    engine_name = variant.get("engine_display_name")
    if not isinstance(engine_name, str) or not engine_name:
        engine_name = engine.display_name if engine is not None else engine_id
    backend_name = variant.get("backend_display_name")
    if not isinstance(backend_name, str) or not backend_name:
        if engine is not None and backend_id in engine.backends:
            backend_name = engine.backends[backend_id].display_name
        else:
            backend_name = backend_id
    return f"{engine_name} — {backend_name}"


def _variant_status(
    variant: Mapping[str, Any], *, legacy_summary: bool
) -> tuple[str, bool]:
    status = variant.get("status")
    if not isinstance(status, str):
        return ("legacy-v1" if legacy_summary else "run-failed"), False
    comparable = variant.get("comparable") is True
    if status == "legacy-v1":
        comparable = False
    return status, comparable


def _fps_summary(variant: Mapping[str, Any]) -> tuple[float | None, float | None]:
    summary = variant.get("summary")
    if not isinstance(summary, dict):
        return None, None
    fps = summary.get("fps")
    if not isinstance(fps, dict):
        return None, None
    mean = fps.get("mean")
    if isinstance(mean, bool) or not isinstance(mean, (int, float)):
        return None, None
    stdev = fps.get("stdev")
    if isinstance(stdev, bool) or not isinstance(stdev, (int, float)):
        return float(mean), None
    return float(mean), float(stdev)


def _format_fps(mean: float, stdev: float | None) -> str:
    if stdev is None:
        return f"{mean:.2f}"
    return f"{mean:.2f} ± {stdev:.2f}"


def _cell(variant: Mapping[str, Any] | None, *, legacy_summary: bool) -> str:
    if variant is None:
        return "—"
    status, comparable = _variant_status(variant, legacy_summary=legacy_summary)
    mean_fps, stdev_fps = _fps_summary(variant)
    if status == "legacy-v1":
        return (
            f"{_format_fps(mean_fps, stdev_fps)}†"
            if mean_fps is not None
            else "legacy v1†"
        )
    if status == "passed" and comparable and mean_fps is not None:
        return _format_fps(mean_fps, stdev_fps)
    if status == "passed" and not comparable:
        reason = variant.get("reason")
        if isinstance(reason, str) and reason:
            return f"diagnostic (not ranked: {_markdown(reason)})"
        return "diagnostic (not ranked)"
    return STATUS_LABELS.get(status, _markdown(status))


def _column_sort_key(
    column: tuple[str, str], manifest: CapabilityManifest
) -> tuple[int, int, str, str]:
    engine_id, backend_id = column
    engine_order = list(manifest.engines)
    engine_index = engine_order.index(engine_id) if engine_id in engine_order else len(engine_order)
    engine = manifest.engines.get(engine_id)
    backend_order = list(engine.backends) if engine is not None else []
    backend_index = backend_order.index(backend_id) if backend_id in backend_order else len(backend_order)
    return engine_index, backend_index, engine_id, backend_id


def render_report(data: Mapping[str, Any], manifest: CapabilityManifest) -> str:
    suite_schema_version = data.get("suite_schema_version")
    if isinstance(suite_schema_version, bool) or (
        suite_schema_version is not None
        and not isinstance(suite_schema_version, int)
    ):
        raise ValueError("suite_schema_version must be an integer")
    if suite_schema_version not in {None, 1, 2}:
        raise ValueError(
            f"unsupported suite_schema_version: {suite_schema_version!r}"
        )
    legacy_summary = suite_schema_version != 2

    raw_variants = data.get("variants")
    if not isinstance(raw_variants, list) or not raw_variants:
        return "No variants found in summary.\n"
    if not all(isinstance(variant, dict) for variant in raw_variants):
        raise ValueError("summary.variants must contain only objects")
    variants = [dict(variant) for variant in raw_variants]
    for variant in variants:
        variant["graphics_api"] = _variant_api(variant, manifest)
        variant["gpu"] = _variant_gpu(variant, manifest)
    enforce_gpu_comparability(variants)

    rows: dict[tuple[str, str], dict[tuple[str, str], Mapping[str, Any]]] = defaultdict(dict)
    api_columns: dict[str, set[tuple[str, str]]] = defaultdict(set)
    column_examples: dict[tuple[str, str], Mapping[str, Any]] = {}
    for variant in variants:
        benchmark = str(variant.get("benchmark", "Unknown"))
        scene = str(variant.get("scene", "Unknown"))
        engine = str(variant.get("engine", "Unknown"))
        backend = str(variant.get("backend", "Unknown"))
        column = (engine, backend)
        api = _variant_api(variant, manifest)
        rows[(benchmark, scene)][column] = variant
        api_columns[api].add(column)
        column_examples.setdefault(column, variant)

    workload_order = list(manifest.workloads)
    scene_order = list(manifest.default_scenes)

    def row_key(row: tuple[str, str]) -> tuple[int, str, int, str]:
        benchmark, scene = row
        benchmark_index = (
            workload_order.index(benchmark) if benchmark in workload_order else len(workload_order)
        )
        scene_index = scene_order.index(scene) if scene in scene_order else len(scene_order)
        return benchmark_index, benchmark, scene_index, scene

    configured_api_order = list(manifest.api_order)
    extra_apis = sorted(set(api_columns) - set(configured_api_order))
    api_order = [api for api in configured_api_order if api in api_columns] + extra_apis

    output = ["# Vector graphics benchmark report", ""]
    if data.get("status") == "incomplete":
        output.extend([
            "> **Warning: this benchmark suite is incomplete.** Missing, failed, or "
            "unsupported variants must be resolved before drawing suite-wide conclusions.",
            "",
        ])
    output.extend([
        (
            "Only schema-v2 runs marked `passed` and `comparable: true` are "
            "eligible for performance conclusions. API families are shown separately; "
            "the report does not calculate cross-API speed ratios."
        ),
        "",
        "Values are across-run mean FPS ± across-run sample standard deviation when available.",
    ])
    if legacy_summary or any(
        _variant_status(variant, legacy_summary=legacy_summary)[0] == "legacy-v1"
        for variant in variants
    ):
        output.extend([
            "",
            "† Legacy schema-v1 values are displayed for historical context only and are not ranked.",
        ])

    for api in api_order:
        columns = sorted(api_columns[api], key=lambda value: _column_sort_key(value, manifest))
        relevant_rows = [
            row for row in rows
            if any(column in rows[row] for column in columns)
        ]
        if not relevant_rows:
            continue
        output.extend(["", f"## {_markdown(api)}", ""])
        labels = [
            f"{_column_label(column_examples[column], manifest)} (mean FPS)"
            for column in columns
        ]
        output.append(
            "| Workload (scene) | " + " | ".join(_markdown(label) for label in labels) + " |"
        )
        output.append("|---|" + "---|" * len(columns))
        for benchmark, scene in sorted(relevant_rows, key=row_key):
            workload_name = manifest.workloads.get(benchmark, benchmark)
            cells = [
                _cell(
                    rows[(benchmark, scene)].get(column),
                    legacy_summary=legacy_summary,
                )
                for column in columns
            ]
            output.append(
                f"| {_markdown(workload_name)} ({_markdown(scene)}) | "
                + " | ".join(cells)
                + " |"
            )

    return "\n".join(output) + "\n"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Generate API-grouped Markdown from a benchmark suite summary."
    )
    parser.add_argument("summary_json", nargs="?", help="Path to summary.json")
    parser.add_argument("--manifest", default=str(DEFAULT_MANIFEST), help="Engine manifest")
    parser.add_argument("--output", help="Write Markdown to this file instead of stdout")
    args = parser.parse_args(argv)

    if args.summary_json:
        summary_path = Path(args.summary_json)
    elif Path("summary.json").exists():
        summary_path = Path("summary.json")
    else:
        parser.error("provide a summary.json path")
    if not summary_path.is_file():
        print(f"Error: File not found: {summary_path}", file=sys.stderr)
        return 1

    try:
        with summary_path.open("r", encoding="utf-8") as file:
            data = json.load(file)
        if not isinstance(data, dict):
            raise ValueError("summary JSON must be an object")
        manifest = load_manifest(Path(args.manifest))
        report = render_report(data, manifest)
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1

    try:
        if args.output:
            Path(args.output).write_text(report, encoding="utf-8")
        else:
            print(report, end="")
    except OSError as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

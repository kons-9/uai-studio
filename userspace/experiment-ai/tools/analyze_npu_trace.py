#!/usr/bin/env python3
"""Summarize per-epoch NPU timing from a ThreadMonitor dump.

The firmware stores dense ``npu_epoch`` records at the ST runtime callback
boundaries. This tool focuses on the POST_START -> PRE_END interval, which is
the measured NPU/ATON execution and wait portion, and reports the CPU boundary
intervals separately.
"""

import argparse
import json
import statistics
from pathlib import Path
from typing import Any


MODEL_NAMES = {0: "person", 1: "segmentation", 2: "face"}
EPOCH_FLAGS = {
    1 << 2: "blob",
    1 << 4: "pure_hw",
    1 << 5: "pure_sw",
    1 << 6: "hybrid",
    1 << 7: "internal",
}
EPOCH_CALLBACK_STAGES = {
    1: "cpu_start",  # POST_START - PRE_START
    2: "npu",        # PRE_END - POST_START
    3: "cpu_end",    # POST_END - PRE_END
}


def load_trace(path: Path) -> dict[str, Any]:
    if path.suffix.lower() == ".json":
        return json.loads(path.read_text(encoding="utf-8"))
    from decode_thread_monitor import decode

    return decode(path)


def model_name(model_id: int) -> str:
    return MODEL_NAMES.get(model_id, f"kind={model_id}")


def flag_name(flags: int) -> str:
    names = [name for bit, name in EPOCH_FLAGS.items() if flags & bit]
    return "+".join(names) if names else "none"


def percentile(values: list[int], fraction: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    index = min(len(ordered) - 1, int(round((len(ordered) - 1) * fraction)))
    return float(ordered[index])


def format_cycles(cycles: float, cpu_hz: float | None) -> str:
    if cpu_hz is None or cpu_hz <= 0.0:
        return f"{cycles:.0f} cyc"
    return f"{cycles / cpu_hz * 1_000_000.0:.2f} us ({cycles:.0f} cyc)"


def callback_stage(record: dict[str, Any]) -> str:
    return EPOCH_CALLBACK_STAGES.get(
        int(record.get("callback_type", -1)), "legacy_post_end"
    )


def summarize(trace: dict[str, Any], top: int, cpu_hz: float | None) -> None:
    records = [record for record in trace.get("records", [])
               if record.get("type") == "npu_epoch"]
    if not records:
        raise ValueError("trace contains no npu_epoch records; use firmware with epoch profiling")

    header = trace.get("header", {})
    dropped = int(header.get("dropped_count", 0))
    if dropped:
        print(f"warning: trace ring dropped {dropped} records")

    staged_by_model: dict[int, dict[str, list[dict[str, Any]]]] = {}
    for record in records:
        model_id = int(record.get("model_kind_id", 0xFFFFFFFF))
        staged_by_model.setdefault(model_id, {}).setdefault(
            callback_stage(record), []
        ).append(record)

    by_model: dict[int, list[dict[str, Any]]] = {}
    focus_by_model: dict[int, list[dict[str, Any]]] = {}
    focus_stage_by_model: dict[int, str] = {}
    for model_id, stages in staged_by_model.items():
        # PRE_END intervals cover POST_START -> PRE_END, i.e. the NPU/ATON
        # execution and wait portion. Old version-4 traces only emitted
        # POST_END, so retain a backwards-compatible fallback.
        if stages.get("npu"):
            focus_stage_by_model[model_id] = "npu"
            focus_by_model[model_id] = stages["npu"]
        elif stages.get("legacy_post_end"):
            focus_stage_by_model[model_id] = "legacy_post_end"
            focus_by_model[model_id] = stages["legacy_post_end"]
        else:
            # Version-4 traces written before stage splitting used callback
            # type POST_END for the complete interval.
            focus_stage_by_model[model_id] = "legacy_post_end"
            focus_by_model[model_id] = stages.get("cpu_end", [])

    for record in records:
        model_id = int(record.get("model_kind_id", 0xFFFFFFFF))
        by_model.setdefault(model_id, []).append(record)

    print(f"epoch records: {len(records)}")
    focus_by_block: dict[tuple[int, int, int], list[int]] = {}
    for model_id, model_records in focus_by_model.items():
        for record in model_records:
            key = (model_id, int(record.get("epoch_index", 0xFFFFFFFF)),
                   int(record.get("epoch_flags", 0)))
            focus_by_block.setdefault(key, []).append(
                int(record.get("cycle_elapsed", 0))
            )

    def stage_summary(stage_records: list[dict[str, Any]]) -> str:
        if not stage_records:
            return "none"
        cycles = [int(record.get("cycle_elapsed", 0))
                  for record in stage_records]
        return (
            f"n={len(cycles)} sum={format_cycles(sum(cycles), cpu_hz)} "
            f"med={format_cycles(statistics.median(cycles), cpu_hz)} "
            f"p95={format_cycles(percentile(cycles, 0.95), cpu_hz)}"
        )

    for model_id in sorted(by_model):
        stages = staged_by_model[model_id]
        focus_records = focus_by_model[model_id]
        focus_cycles = [int(record.get("cycle_elapsed", 0))
                        for record in focus_records]
        if not focus_cycles:
            continue
        stage_text = "; ".join(
            f"{stage}({stage_summary(stages.get(stage, []))})"
            for stage in ("cpu_start", "npu", "cpu_end", "legacy_post_end")
            if stages.get(stage)
        )
        print(
            f"{model_name(model_id)}: focus={focus_stage_by_model[model_id]} "
            f"events={len(focus_cycles)} blocks="
            f"{len({(record.get('epoch_index'), record.get('epoch_address')) for record in focus_records})} "
            f"sum={format_cycles(sum(focus_cycles), cpu_hz)} "
            f"median={format_cycles(statistics.median(focus_cycles), cpu_hz)} "
            f"p95={format_cycles(percentile(focus_cycles, 0.95), cpu_hz)} "
            f"max={format_cycles(max(focus_cycles), cpu_hz)} "
            f"stages={stage_text}"
        )

    ranked = sorted(
        ((sum(values), key, len(values), statistics.median(values))
         for key, values in focus_by_block.items()),
        reverse=True,
    )
    print(f"top {min(top, len(ranked))} epoch blocks by focused measured cycles:")
    for total, (model_id, epoch_index, flags), count, median in ranked[:top]:
        print(
            f"  {model_name(model_id)} epoch={epoch_index} "
            f"class={flag_name(flags)} samples={count} "
            f"total={format_cycles(total, cpu_hz)} "
            f"median={format_cycles(median, cpu_hz)}"
        )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path, help="raw ThreadMonitor dump or decoded JSON")
    parser.add_argument("--top", type=int, default=12,
                        help="number of slow epoch groups to print")
    parser.add_argument("--cpu-hz", type=float,
                        help="optional CPU frequency for cycle-to-time conversion")
    args = parser.parse_args()
    summarize(load_trace(args.trace), max(1, args.top), args.cpu_hz)


if __name__ == "__main__":
    main()

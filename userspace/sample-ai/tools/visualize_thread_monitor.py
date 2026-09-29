#!/usr/bin/env python3
"""Plot a ThreadMonitor raw dump or decoded JSON trace.

Examples:
    uv run --project userspace/sample-ai/tools \
        python userspace/sample-ai/tools/visualize_thread_monitor.py \
        build/thread_monitor_final.bin

    uv run --project userspace/sample-ai/tools \
        python userspace/sample-ai/tools/visualize_thread_monitor.py \
        build/thread_monitor_final.json --output build/thread_monitor.png
"""

import argparse
import json
from pathlib import Path
from typing import Any


NPU_TIMING_VALID = 1 << 2
UNKNOWN_MODEL_KIND_ID = 0xFFFFFFFF

MODEL_COLORS = ("#1565c0", "#ef6c00", "#2e7d32", "#6a1b9a")
MODEL_NAMES = {
    0: "person",
    1: "segmentation",
    2: "face",
}
PHASE_NAMES = {
    1: "model selection",
    2: "input preparation",
    3: "NPU execution",
    4: "output preparation",
    5: "output decoding",
    6: "result conversion",
    7: "input preparation wait",
}
PHASE_ORDER = (1, 2, 7, 3, 4, 5, 6)
PHASE_COLORS = (
    "#6a1b9a",
    "#1565c0",
    "#ef6c00",
    "#00838f",
    "#2e7d32",
    "#c62828",
    "#546e7a",
)


def load_trace(path: Path) -> dict[str, Any]:
    if path.suffix.lower() == ".json":
        return json.loads(path.read_text(encoding="utf-8"))

    # Keep the binary format definition in one place.
    from decode_thread_monitor import decode

    return decode(path)


def records_with_time(trace: dict[str, Any]) -> list[dict[str, Any]]:
    records = trace.get("records", [])
    if not records:
        raise ValueError("trace contains no valid records")

    normalized = []
    for record in records:
        item = dict(record)
        # timestamp_ms is tk_get_otm() time: milliseconds since system boot.
        # Keep that origin instead of silently shifting the first sample to 0.
        item["time_s"] = item.get("timestamp_ms", 0) / 1000.0
        flags = item.get("flags", 0)
        item["npu_timing_valid"] = item.get(
            "npu_timing_valid", bool(flags & NPU_TIMING_VALID)
        )
        normalized.append(item)
    return normalized


def phase_id(record: dict[str, Any]) -> int | None:
    if record.get("type") == "npu_execution":
        return 3
    if record.get("type") == "inference_phase":
        return record.get("phase_id")
    return None


def elapsed_ms(record: dict[str, Any]) -> int:
    return record.get("elapsed_ms", record.get("npu_elapsed_ms", 0))


def model_name(kind_id: int) -> str:
    if kind_id == UNKNOWN_MODEL_KIND_ID:
        return "unknown"
    return MODEL_NAMES.get(kind_id, f"kind id={kind_id}")


def model_color(kind_id: int) -> str:
    if kind_id == UNKNOWN_MODEL_KIND_ID:
        return "#9e9e9e"
    return MODEL_COLORS[kind_id % len(MODEL_COLORS)]


def add_gantt_panel(axis: Any, records: list[dict[str, Any]]) -> None:
    """Draw one Gantt row per model; phase is represented by color."""
    from matplotlib.patches import Patch

    phase_events = [record for record in records if phase_id(record) is not None]
    kind_ids = sorted({
        record.get("model_kind_id", UNKNOWN_MODEL_KIND_ID)
        for record in phase_events
    })

    if kind_ids:
        for row, kind_id in enumerate(kind_ids):
            for event in phase_events:
                if event.get("model_kind_id", UNKNOWN_MODEL_KIND_ID) != kind_id:
                    continue
                current_phase_id = phase_id(event)
                duration = max(0.0, elapsed_ms(event) / 1000.0)
                start = event["time_s"] - duration
                if duration > 0.0:
                    axis.broken_barh(
                        [(start, duration)],
                        (row - 0.32, 0.64),
                        facecolors=PHASE_COLORS[
                            (current_phase_id - 1) % len(PHASE_COLORS)
                        ],
                    )
        axis.set_yticks(range(len(kind_ids)))
        axis.set_yticklabels([
            f"{model_name(kind_id)} (kind id={kind_id})"
            for kind_id in kind_ids
        ])
        axis.set_ylim(-0.7, max(0.7, len(kind_ids) - 0.3))
        axis.legend(
            handles=[Patch(
                facecolor=PHASE_COLORS[(current_phase_id - 1) % len(PHASE_COLORS)],
                label=PHASE_NAMES[current_phase_id],
            ) for current_phase_id in PHASE_ORDER],
            loc="upper left",
            bbox_to_anchor=(0.0, 1.0),
            ncol=3,
            fontsize="x-small",
        )
        axis.invert_yaxis()
    else:
        axis.text(
            0.5,
            0.5,
            "No inference phase events",
            transform=axis.transAxes,
            ha="center",
            va="center",
        )

    axis.set_ylabel("model")
    axis.set_title("Inference timeline")
    axis.grid(True, axis="x", alpha=0.25)


def average_phase_times(records: list[dict[str, Any]]) -> dict[tuple[int, int], float]:
    groups: dict[tuple[int, int], list[int]] = {}
    for record in records:
        current_phase_id = phase_id(record)
        if current_phase_id is None:
            continue
        key = (
            record.get("model_kind_id", UNKNOWN_MODEL_KIND_ID),
            current_phase_id,
        )
        groups.setdefault(key, []).append(elapsed_ms(record))
    return {
        key: sum(values) / len(values)
        for key, values in groups.items()
        if values
    }


def add_average_panel(axis: Any, records: list[dict[str, Any]]) -> None:
    """Draw one stacked average-duration bar per model."""
    from matplotlib.patches import Patch

    averages = average_phase_times(records)
    kind_ids = sorted({kind_id for kind_id, _ in averages})
    if not kind_ids:
        axis.text(
            0.5,
            0.5,
            "No phase duration records",
            transform=axis.transAxes,
            ha="center",
            va="center",
        )
        return

    for row, kind_id in enumerate(kind_ids):
        left = 0.0
        for current_phase_id in PHASE_ORDER:
            duration = averages.get((kind_id, current_phase_id), 0.0)
            if duration <= 0.0:
                continue
            axis.barh(
                row,
                duration,
                left=left,
                height=0.64,
                color=PHASE_COLORS[(current_phase_id - 1) % len(PHASE_COLORS)],
            )
            if duration >= 35.0:
                axis.text(
                    left + duration / 2.0,
                    row,
                    f"{duration:.0f}",
                    ha="center",
                    va="center",
                    color="white",
                    fontsize="x-small",
                )
            left += duration
        axis.text(left, row, f"  {left:.1f} ms", va="center", fontsize="small")

    axis.set_yticks(range(len(kind_ids)))
    axis.set_yticklabels([
        f"{model_name(kind_id)} (kind id={kind_id})"
        for kind_id in kind_ids
    ])
    axis.set_ylim(-0.7, max(0.7, len(kind_ids) - 0.3))
    axis.set_ylabel("model")
    axis.set_xlabel("Average duration [ms]")
    axis.set_title("Average inference time by model (stacked by phase)")
    axis.grid(True, axis="x", alpha=0.25)
    axis.invert_yaxis()


def phase_interval(record: dict[str, Any]) -> tuple[float, float]:
    end = record["time_s"]
    duration = max(0.0, elapsed_ms(record) / 1000.0)
    return end - duration, end


def timeline_bounds(records: list[dict[str, Any]]) -> tuple[float, float]:
    points = [record["time_s"] for record in records]
    for record in records:
        if phase_id(record) is not None:
            points.extend(phase_interval(record))
    start = min(points)
    end = max(points)
    if start == end:
        return start - 0.5, end + 0.5
    return start, end


def plot_trace(trace: dict[str, Any], output: Path, show: bool, dpi: int) -> None:
    import matplotlib

    if not show:
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    records = records_with_time(trace)
    header = trace.get("header", {})
    phase_events = [record for record in records if phase_id(record) is not None]
    timing_records = [record for record in records
                      if record["npu_timing_valid"]]
    # Version 3 has one record per inference phase. Legacy traces only have the
    # latest NPU run copied into periodic samples, so retain that as a fallback.
    phase_records = phase_events if phase_events else timing_records
    faults = [record for record in records if record.get("fault_code", 0) != 0]
    x_start, x_end = timeline_bounds(phase_records)

    figure, axes = plt.subplots(
        2,
        1,
        figsize=(13, 7),
        gridspec_kw={"height_ratios": (1.35, 1.0)},
        constrained_layout=True,
    )
    figure.suptitle(
        "sample-ai ThreadMonitor\n"
        f"records={header.get('decoded_count', len(records))} "
        f"faults={header.get('fault_count', 0)} model-centric"
    )

    add_gantt_panel(axes[0], phase_records)
    add_average_panel(axes[1], phase_records)

    # Keep the timeline focused on inference events. This also includes the
    # reconstructed NPU start, which may precede the event end.
    from matplotlib.ticker import MaxNLocator

    axes[0].set_xlim(x_start, x_end)
    axes[0].xaxis.set_major_locator(MaxNLocator(nbins=8, steps=(1, 2, 5, 10)))
    axes[0].set_xlabel(
        "ThreadMonitor timestamp_ms / 1000 "
        "(system uptime at sample) [s]"
    )
    for fault in faults:
        if x_start <= fault["time_s"] <= x_end:
            axes[0].axvline(
                fault["time_s"], color="#c62828", linestyle=":", linewidth=1
            )

    output.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(output, dpi=dpi)
    if show:
        plt.show()
    plt.close(figure)

    if phase_records:
        values = [elapsed_ms(record) for record in phase_records]
        groups = {}
        for record in phase_records:
            key = (record.get("model_kind_id", UNKNOWN_MODEL_KIND_ID),
                   phase_id(record) or 3)
            groups.setdefault(key, []).append(elapsed_ms(record))
        averages = ", ".join(
            f"{model_name(kind)}/{PHASE_NAMES.get(phase, f'phase={phase}')}: "
            f"{sum(values) / len(values):.1f} ms"
            for (kind, phase), values in sorted(
                groups.items(), key=lambda item: str(item[0]))
        )
        print(
            f"saved {output} | phase samples={len(values)} "
            f"min={min(values)} ms max={max(values)} ms | averages: {averages}"
        )
    else:
        print(f"saved {output} | no NPU timing records")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path, help="raw .bin dump or decoded .json")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path("thread_monitor.png"),
        help="PNG output path (default: thread_monitor.png)",
    )
    parser.add_argument("--dpi", type=int, default=140)
    parser.add_argument("--show", action="store_true", help="also open a window")
    args = parser.parse_args()

    plot_trace(load_trace(args.trace), args.output, args.show, args.dpi)


if __name__ == "__main__":
    main()

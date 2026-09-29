#!/usr/bin/env python3
"""Plot a ThreadMonitor raw dump or decoded JSON trace.

Examples:
    uv run --project userspace/sample-ai/tools \
        python userspace/sample-ai/tools/visualize_thread_monitor.py \
        build/thread_monitor_final.bin

    uv run --project userspace/sample-ai/tools \
        python userspace/sample-ai/tools/visualize_thread_monitor.py \
        build/thread_monitor_final.json --output build/thread_monitor.png \
        --cpu-hz 600000000
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
CPU_PHASE_IDS = (1, 2, 4, 5, 6)
CPU_ACTIVE_STAGE_IDS = {
    "copy",
    "resize",
    "letterbox",
    "input_cache",
    "submit",
    "output_cache",
    "decode",
    "convert",
    "finalize",
}
CPU_WAIT_STAGE_IDS = {"irq_wait", "epoch_continue"}
PIPELINE_STAGE_ORDER = (
    "copy",
    "resize",
    "letterbox",
    "input_cache",
    "submit",
    "irq_wait",
    "epoch_continue",
    "output_cache",
    "decode",
    "convert",
    "finalize",
)
PIPELINE_STAGE_NAMES = {
    "copy": "copy",
    "resize": "resize",
    "letterbox": "letterbox",
    "input_cache": "input cache",
    "submit": "submit",
    "irq_wait": "IRQ wait",
    "epoch_continue": "epoch continue",
    "output_cache": "output cache",
    "decode": "decode",
    "convert": "convert",
    "finalize": "finalize",
}
PIPELINE_STAGE_COLORS = {
    "copy": "#5e35b1",
    "resize": "#3949ab",
    "letterbox": "#1e88e5",
    "input_cache": "#00838f",
    "submit": "#43a047",
    "irq_wait": "#fb8c00",
    "epoch_continue": "#f4511e",
    "output_cache": "#6d4c41",
    "decode": "#8e24aa",
    "convert": "#d81b60",
    "finalize": "#546e7a",
}
EPOCH_STAGE_ORDER = ("cpu_start", "npu", "cpu_end")
EPOCH_STAGE_NAMES = {
    "cpu_start": "CPU start",
    "npu": "NPU / ATON",
    "cpu_end": "CPU end",
}
EPOCH_STAGE_COLORS = {
    "cpu_start": "#7b1fa2",
    "npu": "#ef6c00",
    "cpu_end": "#00838f",
}


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
    value = record.get("elapsed_ms")
    return value if value is not None else record.get("npu_elapsed_ms", 0)


def model_name(kind_id: int) -> str:
    if kind_id == UNKNOWN_MODEL_KIND_ID:
        return "unknown"
    return MODEL_NAMES.get(kind_id, f"kind id={kind_id}")


def model_color(kind_id: int) -> str:
    if kind_id == UNKNOWN_MODEL_KIND_ID:
        return "#9e9e9e"
    return MODEL_COLORS[kind_id % len(MODEL_COLORS)]


def epoch_records(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [
        record for record in records
        if record.get("type") == "npu_epoch"
        and record.get("callback_stage") in EPOCH_STAGE_ORDER
    ]


def interval(record: dict[str, Any], cpu_hz: float) -> tuple[float, float]:
    end = record["time_s"]
    if record.get("type") == "npu_epoch":
        duration = max(0.0, record.get("cycle_elapsed", 0) / cpu_hz)
    elif record.get("type") == "pipeline_stage":
        if record.get("stage_timing_valid") and cpu_hz > 0.0:
            cycles_per_ms = max(1, int(cpu_hz / 1000.0))
            end += (record.get("stage_cycle_end", 0) % cycles_per_ms) / cpu_hz
            duration = max(
                0.0, record.get("stage_cycle_elapsed", 0) / cpu_hz
            )
        else:
            duration = max(0.0, elapsed_ms(record) / 1000.0)
    else:
        duration = max(0.0, elapsed_ms(record) / 1000.0)
    return end - duration, end


def stage_elapsed_ms(record: dict[str, Any], cpu_hz: float) -> float:
    if record.get("stage_timing_valid") and cpu_hz > 0.0:
        return max(0.0, record.get("stage_cycle_elapsed", 0) * 1000.0 / cpu_hz)
    return max(0.0, float(record.get("elapsed_ms") or
                           record.get("npu_elapsed_ms") or 0.0))


def add_pipeline_panel(axis: Any, records: list[dict[str, Any]],
                       cpu_hz: float) -> None:
    """Draw CPU stages and NPU execution on separate, non-stacked lanes."""
    from matplotlib.patches import Patch

    phase_events = [record for record in records if phase_id(record) is not None]
    stage_events = pipeline_stage_records(records)
    kind_ids = sorted({
        record.get("model_kind_id", UNKNOWN_MODEL_KIND_ID) for record in
        phase_events + stage_events
    })

    if kind_ids:
        labels = []
        for row, kind_id in enumerate(kind_ids):
            cpu_row = row * 2
            npu_row = cpu_row + 1
            labels.extend([
                f"{model_name(kind_id)} / CPU",
                f"{model_name(kind_id)} / NPU/ATON",
            ])
            matching_stages = [
                event for event in stage_events
                if event.get("model_kind_id", UNKNOWN_MODEL_KIND_ID) == kind_id
                and event.get("pipeline_stage") not in CPU_WAIT_STAGE_IDS
            ]
            for event in matching_stages:
                stage = event["pipeline_stage"]
                duration = stage_elapsed_ms(event, cpu_hz) / 1000.0
                if duration <= 0.0:
                    continue
                start, end = interval(event, cpu_hz)
                axis.broken_barh(
                    [(start, end - start)],
                    (cpu_row - 0.32, 0.64),
                    facecolors=(
                        "#607d8b" if stage in CPU_WAIT_STAGE_IDS
                        else PIPELINE_STAGE_COLORS[stage]
                    ),
                    hatch="//" if stage in CPU_WAIT_STAGE_IDS else None,
                    edgecolors="#424242" if stage in CPU_WAIT_STAGE_IDS else None,
                    linewidth=0.4,
                )

            # Keep the coarse phase events for NPU/ATON and the wait between
            # prefetched input and submission. CPU active work is taken from
            # operation stages, which also preserves sub-millisecond work.
            for event in phase_events:
                if event.get("model_kind_id", UNKNOWN_MODEL_KIND_ID) != kind_id:
                    continue
                current_phase_id = phase_id(event)
                if current_phase_id == 7:
                    continue
                duration = max(0.0, elapsed_ms(event) / 1000.0)
                start = event["time_s"] - duration
                if duration > 0.0:
                    target_row = npu_row if current_phase_id == 3 else cpu_row
                    if current_phase_id != 3:
                        # Do not duplicate CPU phases when operation stages
                        # are available. This fallback remains for old traces.
                        if matching_stages:
                            continue
                    axis.broken_barh(
                        [(start, duration)],
                        (target_row - 0.32, 0.64),
                        facecolors=(
                            "#ef6c00" if current_phase_id == 3
                            else "#607d8b" if current_phase_id == 7
                            else PHASE_COLORS[
                                (current_phase_id - 1) % len(PHASE_COLORS)
                            ]
                        ),
                        hatch="//" if current_phase_id == 7 else None,
                        edgecolors="#424242" if current_phase_id == 7 else None,
                    )
        axis.set_yticks(range(len(labels)))
        axis.set_yticklabels(labels)
        axis.set_ylim(-0.7, max(0.7, len(labels) - 0.3))
        active_stage_handles = [
            Patch(
                facecolor=PIPELINE_STAGE_COLORS[stage],
                label=PIPELINE_STAGE_NAMES[stage],
            )
            for stage in PIPELINE_STAGE_ORDER
            if stage not in CPU_WAIT_STAGE_IDS
        ]
        axis.legend(
            handles=active_stage_handles + [
                Patch(facecolor="#ef6c00", label="NPU / ATON"),
            ],
            loc="upper left",
            bbox_to_anchor=(0.0, 1.0),
            ncol=5,
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

    axis.set_ylabel("model / lane")
    axis.set_title(
        "Inference execution: CPU stages and NPU/ATON "
        "(overlap preserved)"
    )
    axis.grid(True, axis="x", alpha=0.25)


def pipeline_stage_records(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [
        record for record in records
        if record.get("type") == "pipeline_stage"
        and record.get("pipeline_stage") in PIPELINE_STAGE_ORDER
    ]


def add_stage_panel(axis: Any, records: list[dict[str, Any]]) -> None:
    """Draw every runtime stage on a model lane, including overlap."""
    from matplotlib.patches import Patch

    events = pipeline_stage_records(records)
    kind_ids = sorted({
        record.get("model_kind_id", UNKNOWN_MODEL_KIND_ID)
        for record in events
    })
    if not events:
        axis.text(
            0.5,
            0.5,
            "No pipeline stage records",
            transform=axis.transAxes,
            ha="center",
            va="center",
        )
        axis.set_title("Operation-level pipeline timeline")
        return

    labels = [model_name(kind_id) for kind_id in kind_ids]
    for row, kind_id in enumerate(kind_ids):
        for event in events:
            if event.get("model_kind_id", UNKNOWN_MODEL_KIND_ID) != kind_id:
                continue
            stage = event["pipeline_stage"]
            start, end = interval(event, 1.0)
            duration = end - start
            if duration <= 0.0:
                continue
            axis.broken_barh(
                [(start, duration)],
                (row - 0.32, 0.64),
                facecolors=PIPELINE_STAGE_COLORS[stage],
                edgecolors=model_color(kind_id),
                linewidth=0.45,
            )

    axis.set_yticks(range(len(labels)))
    axis.set_yticklabels(labels)
    axis.set_ylim(-0.7, max(0.7, len(labels) - 0.3))
    axis.legend(
        handles=[
            Patch(
                facecolor=PIPELINE_STAGE_COLORS[stage],
                label=PIPELINE_STAGE_NAMES[stage],
            )
            for stage in PIPELINE_STAGE_ORDER
        ],
        loc="upper left",
        bbox_to_anchor=(0.0, 1.0),
        ncol=4,
        fontsize="x-small",
    )
    axis.set_ylabel("model")
    axis.set_title(
        "Operation-level pipeline stages "
        "(prefetch overlap and NPU wait are preserved)"
    )
    axis.grid(True, axis="x", alpha=0.25)
    axis.invert_yaxis()


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


def add_epoch_panel(axis: Any, records: list[dict[str, Any]], cpu_hz: float) -> None:
    """Draw dense epoch callback stages on independent lanes."""
    from matplotlib.patches import Patch

    events = epoch_records(records)
    kind_ids = sorted({
        record.get("model_kind_id", UNKNOWN_MODEL_KIND_ID)
        for record in events
    })
    if not events:
        axis.text(
            0.5,
            0.5,
            "No npu_epoch records",
            transform=axis.transAxes,
            ha="center",
            va="center",
        )
        axis.set_title("Dense epoch timeline")
        return

    labels = []
    for row, kind_id in enumerate(kind_ids):
        for stage in EPOCH_STAGE_ORDER:
            labels.append(f"{model_name(kind_id)} / {EPOCH_STAGE_NAMES[stage]}")
        for event in events:
            if event.get("model_kind_id", UNKNOWN_MODEL_KIND_ID) != kind_id:
                continue
            stage = event["callback_stage"]
            start, end = interval(event, cpu_hz)
            duration = end - start
            if duration <= 0.0:
                continue
            target_row = row * len(EPOCH_STAGE_ORDER) + EPOCH_STAGE_ORDER.index(stage)
            axis.broken_barh(
                [(start, duration)],
                (target_row - 0.32, 0.64),
                facecolors=EPOCH_STAGE_COLORS[stage],
                edgecolors=model_color(kind_id),
                linewidth=0.4,
            )

    axis.set_yticks(range(len(labels)))
    axis.set_yticklabels(labels)
    axis.set_ylim(-0.7, max(0.7, len(labels) - 0.3))
    axis.legend(
        handles=[Patch(
            facecolor=EPOCH_STAGE_COLORS[stage],
            label=EPOCH_STAGE_NAMES[stage],
        ) for stage in EPOCH_STAGE_ORDER],
        loc="upper left",
        bbox_to_anchor=(0.0, 1.0),
        ncol=3,
        fontsize="x-small",
    )
    axis.set_ylabel("epoch / stage")
    axis.set_title(
        f"Dense epoch timeline at {cpu_hz / 1e6:.0f} MHz "
        "(CPU and NPU overlap preserved)"
    )
    axis.grid(True, axis="x", alpha=0.25)
    axis.invert_yaxis()


def average_decomposition(records: list[dict[str, Any]],
                          cpu_hz: float) -> dict[int, dict[str, float]]:
    averages = average_phase_times(records)
    stage_events = pipeline_stage_records(records)
    kind_ids = sorted({kind_id for kind_id, _ in averages} | {
        record.get("model_kind_id", UNKNOWN_MODEL_KIND_ID)
        for record in stage_events
    })
    result = {}
    for kind_id in kind_ids:
        model_stages = [
            record for record in stage_events
            if record.get("model_kind_id", UNKNOWN_MODEL_KIND_ID) == kind_id
        ]
        npu_samples = [
            record for record in records
            if record.get("model_kind_id", UNKNOWN_MODEL_KIND_ID) == kind_id
            and phase_id(record) == 3
        ]
        sample_count = len(npu_samples)
        if sample_count == 0:
            sample_count = sum(
                1 for record in model_stages
                if record.get("pipeline_stage") == "finalize"
            )
        sample_count = max(1, sample_count)
        stage_cpu = sum(
            stage_elapsed_ms(record, cpu_hz)
            for record in model_stages
            if record.get("pipeline_stage") in CPU_ACTIVE_STAGE_IDS
        ) / sample_count
        phase_cpu = sum(averages.get((kind_id, phase), 0.0)
                        for phase in CPU_PHASE_IDS)
        # Old traces do not have cycle-accurate stage records. Preserve their
        # phase-based CPU result, while new traces use the stage measurement.
        cpu_active = stage_cpu if model_stages else phase_cpu
        result[kind_id] = {
            "cpu": cpu_active,
            "npu": averages.get((kind_id, 3), 0.0),
        }
    return result


def add_average_panel(axis: Any, records: list[dict[str, Any]],
                      cpu_hz: float) -> None:
    """Draw average CPU active and NPU time side by side."""
    averages = average_decomposition(records, cpu_hz)
    kind_ids = sorted(averages)
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

    metrics = (
        ("cpu", "CPU active", "#1565c0"),
        ("npu", "NPU / ATON", "#ef6c00"),
    )
    offsets = (-0.16, 0.16)
    for row, kind_id in enumerate(kind_ids):
        for (metric, label, color), offset in zip(metrics, offsets):
            duration = averages[kind_id][metric]
            if duration < 0.0:
                continue
            axis.barh(
                row + offset,
                duration,
                height=0.20,
                color=color,
                label=label if row == 0 else None,
            )
            axis.text(
                duration,
                row + offset,
                f"  {duration:.3f}" if duration < 1.0 else f"  {duration:.1f}",
                va="center",
                fontsize="x-small",
            )

    axis.set_yticks(range(len(kind_ids)))
    axis.set_yticklabels([
        f"{model_name(kind_id)} (kind id={kind_id})"
        for kind_id in kind_ids
    ])
    axis.set_ylim(-0.7, max(0.7, len(kind_ids) - 0.3))
    axis.set_ylabel("model")
    axis.set_xlabel("Average recorded duration [ms]")
    axis.set_title(
        "Average execution decomposition (CPU active and NPU/ATON)"
    )
    axis.legend(loc="upper right", fontsize="x-small")
    axis.grid(True, axis="x", alpha=0.25)
    axis.invert_yaxis()


def timeline_bounds(records: list[dict[str, Any]], cpu_hz: float) -> tuple[float, float]:
    points = [record["time_s"] for record in records]
    for record in records:
        if (phase_id(record) is not None or
                record.get("type") in ("npu_epoch", "pipeline_stage")):
            points.extend(interval(record, cpu_hz))
    start = min(points)
    end = max(points)
    if start == end:
        return start - 0.5, end + 0.5
    return start, end


def plot_trace(
    trace: dict[str, Any], output: Path, show: bool, dpi: int, cpu_hz: float
) -> None:
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
    phase_records = (
        [record for record in phase_events if phase_id(record) != 7]
        if phase_events else timing_records
    )
    stage_records = pipeline_stage_records(records)
    faults = [record for record in records if record.get("fault_code", 0) != 0]
    display_records = phase_records + [
        record for record in stage_records
        if record.get("pipeline_stage") not in CPU_WAIT_STAGE_IDS
    ]
    x_start, x_end = timeline_bounds(display_records, cpu_hz)

    figure, axes = plt.subplots(
        2,
        1,
        figsize=(15, 9),
        gridspec_kw={"height_ratios": (3.2, 1.4)},
        constrained_layout=True,
    )
    figure.suptitle(
        "sample-ai ThreadMonitor\n"
        f"records={header.get('decoded_count', len(records))} "
        f"faults={header.get('fault_count', 0)} model-centric\n"
        "CPU/NPU execution timeline and average decomposition; overlaps are preserved"
    )

    # Panel 1 uses the absolute ThreadMonitor time axis. It combines the
    # operation-level CPU stages with the corresponding NPU/ATON lane. Panel 2
    # is a duration chart in milliseconds and intentionally has an independent
    # x-axis.
    add_pipeline_panel(axes[0], records, cpu_hz)
    add_average_panel(axes[1], records, cpu_hz)

    # Keep the timeline focused on inference events. This also includes the
    # reconstructed NPU start, which may precede the event end.
    from matplotlib.ticker import AutoMinorLocator, MaxNLocator

    axes[0].set_xlim(x_start, x_end)
    axes[0].xaxis.set_major_locator(
        MaxNLocator(nbins=16, steps=(1, 2, 2.5, 5, 10))
    )
    axes[0].xaxis.set_minor_locator(AutoMinorLocator(2))
    axes[0].grid(True, which="minor", axis="x", alpha=0.14)
    axes[0].set_xlabel(
        "ThreadMonitor timestamp_ms / 1000 "
        "(system uptime at sample) [s]"
    )
    axes[1].xaxis.set_major_locator(
        MaxNLocator(nbins=12, steps=(1, 2, 2.5, 5, 10))
    )
    axes[1].xaxis.set_minor_locator(AutoMinorLocator(2))
    axes[1].grid(True, which="minor", axis="x", alpha=0.14)
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
        execution = average_decomposition(records, cpu_hz)
        execution_averages = ", ".join(
            f"{model_name(kind)} CPU={metrics['cpu']:.3f} ms "
            f"NPU={metrics['npu']:.3f} ms"
            for kind, metrics in sorted(execution.items())
        )
        print(
            f"saved {output} | phase samples={len(values)} "
            f"stage samples={len(stage_records)} "
            f"min={min(values)} ms max={max(values)} ms | "
            f"phase averages: {averages} | "
            f"execution averages: {execution_averages}"
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
    parser.add_argument(
        "--cpu-hz",
        type=float,
        default=600000000.0,
        help="CPU clock used to convert npu_epoch cycles (default: 600000000)",
    )
    parser.add_argument("--show", action="store_true", help="also open a window")
    args = parser.parse_args()

    if args.cpu_hz <= 0.0:
        parser.error("--cpu-hz must be positive")
    plot_trace(load_trace(args.trace), args.output, args.show, args.dpi,
               args.cpu_hz)


if __name__ == "__main__":
    main()

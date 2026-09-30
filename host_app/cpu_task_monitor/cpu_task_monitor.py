#!/usr/bin/env python3
"""Parse and visualize ai-app CPU task monitor UART output.

The firmware emits one report header followed by zero or more task lines::

    cpu: period=680000000 cycles irq=0% count=119 unknown=0
    cpu: task=3 usage=78% cycles=533000000 dispatch=24 state=4

ANSI cursor-control sequences from minicom are ignored, so a raw terminal
capture can be passed directly to this tool.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import struct
import sys
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Iterable


ANSI_ESCAPE_RE = re.compile(
    r"(?:\x1b\][^\x07]*(?:\x07|\x1b\\)|\x1b\[[0-?]*[ -/]*[@-~])"
)
EVENT_RE = re.compile(
    r"(?P<period>cpu:\s+period=(?P<period_cycles>\d+)\s+cycles\s+"
    r"irq=(?P<irq>\d+)%\s+count=(?P<count>\d+)\s+"
    r"unknown=(?P<unknown>\d+))"
    r"|(?P<task>cpu:\s+task=(?P<task_id>\d+)\s+"
    r"usage=(?P<usage>\d+)%\s+cycles=(?P<cycles>\d+)\s+"
    r"dispatch=(?P<dispatch>\d+)\s+state=(?P<state>[0-9a-fA-F]+))"
    r"|(?P<loop>cpu:\s+loop\s+id=(?P<loop_task_id>\d+)\s+"
    r"n=(?P<loop_count>\d+)\s+avg=(?P<loop_average>\d+)\s+"
    r"max=(?P<loop_max>\d+)\s+"
    r"last=(?P<loop_last>\d+))"
)
TASK_NAME_RE = re.compile(
    r"cpu:\s+task_name\s+id=(?P<task_id>\d+)\s+"
    r"name=(?P<name>[A-Za-z0-9_.-]+)"
)
CPU_TRACE_MAGIC = 0x43544D4E
CPU_TRACE_SUPPORTED_VERSIONS = {2, 3}
CPU_TRACE_COMMIT_MAGIC = 0x43544D43
CPU_TRACE_HEADER = struct.Struct("<IHH14I")
CPU_TRACE_TASK_NAME = struct.Struct("<I28s")
CPU_TRACE_TASK_NAME_COUNT = 33
CPU_TRACE_DATA_OFFSET = (
    CPU_TRACE_HEADER.size + CPU_TRACE_TASK_NAME_COUNT * CPU_TRACE_TASK_NAME.size
)
CPU_TRACE_RECORD = struct.Struct("<11IB3xI3I")
CPU_TRACE_REPORT = 1
CPU_TRACE_TASK = 2
CPU_TRACE_TASK_LOOP = 3
CPU_TRACE_TASK_LOOP_INTERVAL = 4
DEFAULT_GANTT_REPEATS = 3
LOOP_INTERVAL_MERGE_GAP_MS = 100


@dataclass
class TaskSample:
    task_id: int
    usage_percent: int
    cycles: int
    dispatch_count: int
    state: int
    name: str = ""


@dataclass
class TaskLoopSample:
    task_id: int
    count: int
    total_cycles: int
    average_cycles: int
    max_cycles: int
    last_cycles: int
    name: str = ""


@dataclass
class TaskLoopInterval:
    task_id: int
    start_cycles: int
    end_cycles: int
    name: str = ""


@dataclass
class Report:
    sample: int
    period_cycles: int
    irq_percent: int
    interrupt_count: int
    unknown_task_events: int
    timestamp_cycles: int = 0
    tasks: list[TaskSample] = field(default_factory=list)
    loops: list[TaskLoopSample] = field(default_factory=list)
    loop_intervals: list[TaskLoopInterval] = field(default_factory=list)


def clean_terminal_text(text: str) -> str:
    """Remove terminal control sequences while keeping line boundaries."""

    text = ANSI_ESCAPE_RE.sub("", text)
    return "".join(
        character
        for character in text
        if character in "\n\r\t" or ord(character) >= 0x20
    )


def parse_reports(text: str) -> list[Report]:
    """Parse reports in stream order, tolerating unrelated UART messages."""

    cleaned = clean_terminal_text(text)
    task_names = {
        int(match.group("task_id")): match.group("name")
        for match in TASK_NAME_RE.finditer(cleaned)
    }
    reports: list[Report] = []
    current: Report | None = None

    # finditer also handles captures where a terminal control sequence caused
    # an otherwise complete line to be split in the middle.
    for match in EVENT_RE.finditer(cleaned):
        if match.group("period") is not None:
            values = match.groupdict()
            current = Report(
                sample=len(reports),
                period_cycles=int(values["period_cycles"]),
                irq_percent=int(values["irq"]),
                interrupt_count=int(values["count"]),
                unknown_task_events=int(values["unknown"]),
            )
            reports.append(current)
            continue

        if current is None:
            continue
        values = match.groupdict()
        if values["task"] is not None:
            task_id = int(values["task_id"])
            current.tasks.append(
                TaskSample(
                    task_id=task_id,
                    usage_percent=int(values["usage"]),
                    cycles=int(values["cycles"]),
                    dispatch_count=int(values["dispatch"]),
                    state=int(values["state"], 16),
                    name=task_names.get(task_id, ""),
                )
            )
        elif values["loop"] is not None:
            task_id = int(values["loop_task_id"])
            current.loops.append(
                TaskLoopSample(
                    task_id=task_id,
                    count=int(values["loop_count"]),
                    total_cycles=(int(values["loop_average"])
                                  * int(values["loop_count"])),
                    average_cycles=int(values["loop_average"]),
                    max_cycles=int(values["loop_max"]),
                    last_cycles=int(values["loop_last"]),
                    name=task_names.get(task_id, ""),
                )
            )

    return reports


def read_input(path: str) -> str:
    if path == "-":
        return sys.stdin.read()
    return Path(path).read_text(encoding="utf-8", errors="replace")


def parse_binary_trace(data: bytes) -> list[Report]:
    """Decode the PSRAM CPU task monitor ring into the UART-style model."""

    if len(data) < CPU_TRACE_HEADER.size:
        raise ValueError(f"CPU trace dump is too small: {len(data)} bytes")
    values = CPU_TRACE_HEADER.unpack_from(data)
    (magic, version, header_size, record_size, capacity, write_index,
     record_count, _dropped_count, _boot_count, _next_sequence,
     _monitor_task_id, _report_task_id, _last_period, _last_irq,
     task_name_count, task_name_entry_size, _reserved) = values
    if magic != CPU_TRACE_MAGIC:
        raise ValueError(f"invalid CPU trace magic: 0x{magic:08x}")
    if version not in CPU_TRACE_SUPPORTED_VERSIONS:
        raise ValueError(f"unsupported CPU trace version: {version}")
    data_offset = CPU_TRACE_DATA_OFFSET if version >= 3 else CPU_TRACE_HEADER.size
    if (header_size != data_offset
            or record_size != CPU_TRACE_RECORD.size
            or (version >= 3 and
                (task_name_entry_size != CPU_TRACE_TASK_NAME.size
                 or task_name_count > CPU_TRACE_TASK_NAME_COUNT))):
        raise ValueError(
            f"unsupported CPU trace layout: header={header_size}, "
            f"record={record_size}"
        )
    if capacity == 0 or write_index >= capacity or record_count > capacity:
        raise ValueError("invalid CPU trace ring state")
    if data_offset + capacity * record_size > len(data):
        raise ValueError("CPU trace dump does not contain the complete ring")

    task_names: dict[int, str] = {}
    if version >= 3:
        for index in range(CPU_TRACE_TASK_NAME_COUNT):
            task_id, encoded_name = CPU_TRACE_TASK_NAME.unpack_from(
                data, CPU_TRACE_HEADER.size + index * CPU_TRACE_TASK_NAME.size
            )
            if task_id != 0:
                task_names[task_id] = encoded_name.split(b"\0", 1)[0].decode(
                    "utf-8", errors="replace"
                )

    count = min(record_count, capacity)
    first_index = (write_index - count) % capacity
    reports: list[Report] = []
    current: Report | None = None
    current_timestamp = None
    latest_extended_timestamp: int | None = None
    for offset in range(count):
        index = (first_index + offset) % capacity
        fields = CPU_TRACE_RECORD.unpack_from(
            data, data_offset + index * record_size
        )
        (sequence, timestamp_cycles, period_cycles, task_id, state, cycles,
         dispatch_count, usage_percent, irq_percent, interrupt_count,
         unknown_events, record_type, commit_marker, _r0, _r1, _r2) = fields
        if commit_marker != (CPU_TRACE_COMMIT_MAGIC ^ sequence):
            continue
        if latest_extended_timestamp is None:
            extended_timestamp = timestamp_cycles
            latest_extended_timestamp = extended_timestamp
        else:
            cycle_epoch = latest_extended_timestamp & ~0xFFFFFFFF
            extended_timestamp = cycle_epoch + timestamp_cycles
            if extended_timestamp + 0x80000000 < latest_extended_timestamp:
                extended_timestamp += 1 << 32
            elif (extended_timestamp > latest_extended_timestamp + 0x80000000
                  and extended_timestamp >= 1 << 32):
                extended_timestamp -= 1 << 32
            latest_extended_timestamp = max(
                latest_extended_timestamp, extended_timestamp
            )
        if record_type == CPU_TRACE_REPORT:
            current = Report(
                sample=len(reports),
                period_cycles=period_cycles,
                irq_percent=irq_percent,
                interrupt_count=interrupt_count,
                unknown_task_events=unknown_events,
                timestamp_cycles=extended_timestamp,
            )
            reports.append(current)
            current_timestamp = extended_timestamp
        elif record_type == CPU_TRACE_TASK:
            if current is None or current_timestamp != extended_timestamp:
                current = Report(
                    sample=len(reports),
                    period_cycles=period_cycles,
                    irq_percent=irq_percent,
                    interrupt_count=interrupt_count,
                    unknown_task_events=unknown_events,
                    timestamp_cycles=extended_timestamp,
                )
                reports.append(current)
                current_timestamp = extended_timestamp
            current.tasks.append(TaskSample(
                task_id=task_id,
                usage_percent=usage_percent,
                cycles=cycles,
                dispatch_count=dispatch_count,
                state=state,
                name=task_names.get(task_id, ""),
            ))
        elif record_type == CPU_TRACE_TASK_LOOP:
            if current is None or current_timestamp != extended_timestamp:
                current = Report(
                    sample=len(reports),
                    period_cycles=period_cycles,
                    irq_percent=irq_percent,
                    interrupt_count=interrupt_count,
                    unknown_task_events=unknown_events,
                    timestamp_cycles=extended_timestamp,
                )
                reports.append(current)
                current_timestamp = extended_timestamp
            current.loops.append(TaskLoopSample(
                task_id=task_id,
                count=dispatch_count,
                total_cycles=cycles,
                average_cycles=cycles // dispatch_count if dispatch_count else 0,
                max_cycles=usage_percent,
                last_cycles=state,
                name=task_names.get(task_id, ""),
            ))
        elif record_type == CPU_TRACE_TASK_LOOP_INTERVAL:
            if current is None:
                continue
            current.loop_intervals.append(TaskLoopInterval(
                task_id=task_id,
                start_cycles=extended_timestamp - cycles,
                end_cycles=extended_timestamp,
                name=task_names.get(task_id, ""),
            ))
    return reports


def read_reports(path: str) -> list[Report]:
    if path == "-":
        return parse_reports(sys.stdin.read())
    data = Path(path).read_bytes()
    if len(data) >= 4 and struct.unpack_from("<I", data)[0] == CPU_TRACE_MAGIC:
        return parse_binary_trace(data)
    return parse_reports(data.decode("utf-8", errors="replace"))


def task_order(reports: Iterable[Report], top_tasks: int) -> list[int]:
    totals: dict[int, int] = {}
    for report in reports:
        for task in report.tasks:
            totals[task.task_id] = totals.get(task.task_id, 0) + task.cycles
    ordered = [task_id for task_id, _ in sorted(
        totals.items(), key=lambda item: (-item[1], item[0])
    )]
    return ordered if top_tasks <= 0 else ordered[:top_tasks]


def report_x_values(reports: list[Report], cpu_hz: float) -> tuple[list[float], str]:
    if cpu_hz <= 0.0:
        return [float(report.sample + 1) for report in reports], "report"
    elapsed = 0.0
    values: list[float] = []
    for report in reports:
        elapsed += report.period_cycles / cpu_hz
        values.append(elapsed)
    return values, "elapsed time (s)"


def write_csv(path: Path, reports: list[Report]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow([
            "sample",
            "period_cycles",
            "irq_percent",
            "interrupt_count",
            "unknown_task_events",
            "task_id",
            "task_name",
            "task_usage_percent",
            "task_cycles",
            "dispatch_count",
            "state",
            "loop_count",
            "loop_total_cycles",
            "loop_average_cycles",
            "loop_max_cycles",
            "loop_last_cycles",
        ])
        for report in reports:
            task_samples = {task.task_id: task for task in report.tasks}
            loop_samples = {loop.task_id: loop for loop in report.loops}
            task_ids = sorted(set(task_samples) | set(loop_samples))
            if not task_ids:
                writer.writerow([
                    report.sample,
                    report.period_cycles,
                    report.irq_percent,
                    report.interrupt_count,
                    report.unknown_task_events,
                    "",
                    "",
                    "",
                    "",
                    "",
                    "",
                    "",
                    "",
                    "",
                    "",
                    "",
                ])
                continue
            for task_id in task_ids:
                task = task_samples.get(task_id)
                loop = loop_samples.get(task_id)
                name = (task.name if task is not None else "") or (
                    loop.name if loop is not None else ""
                )
                writer.writerow([
                    report.sample,
                    report.period_cycles,
                    report.irq_percent,
                    report.interrupt_count,
                    report.unknown_task_events,
                    task_id,
                    name,
                    task.usage_percent if task is not None else "",
                    task.cycles if task is not None else "",
                    task.dispatch_count if task is not None else "",
                    f"0x{task.state:x}" if task is not None else "",
                    loop.count if loop is not None else "",
                    loop.total_cycles if loop is not None else "",
                    loop.average_cycles if loop is not None else "",
                    loop.max_cycles if loop is not None else "",
                    loop.last_cycles if loop is not None else "",
                ])


def write_json(path: Path, reports: list[Report]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps({"reports": [asdict(report) for report in reports]}, indent=2)
        + "\n",
        encoding="utf-8",
    )


def plot_reports(
    path: Path,
    reports: list[Report],
    cpu_hz: float,
    top_tasks: int,
    gantt_repeats: int,
    title: str | None,
) -> None:
    if not reports:
        raise ValueError("no 'cpu: period=...' reports found in input")

    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np

    task_ids = task_order(reports, top_tasks)
    if not task_ids:
        loop_totals: dict[int, int] = {}
        for report in reports:
            for loop in report.loops:
                loop_totals[loop.task_id] = (
                    loop_totals.get(loop.task_id, 0) + loop.total_cycles
                )
        ordered_loops = [task_id for task_id, _ in sorted(
            loop_totals.items(), key=lambda item: (-item[1], item[0])
        )]
        task_ids = ordered_loops if top_tasks <= 0 else ordered_loops[:top_tasks]

    task_names: dict[int, str] = {}
    for report in reports:
        for sample in [*report.tasks, *report.loops]:
            if sample.name:
                task_names[sample.task_id] = sample.name
    task_labels = {
        task_id: f"{name} (ID {task_id})"
        for task_id, name in task_names.items()
    }
    for task_id in task_ids:
        task_labels.setdefault(task_id, f"task {task_id}")
    x, x_label = report_x_values(reports, cpu_hz)
    usage = {
        task_id: [
            next((task.usage_percent for task in report.tasks
                  if task.task_id == task_id), 0)
            for report in reports
        ]
        for task_id in task_ids
    }
    loop_values = {
        task_id: [
            next((loop.average_cycles for loop in report.loops
                  if loop.task_id == task_id), 0)
            for report in reports
        ]
        for task_id in task_ids
    }
    loop_max_values = {
        task_id: [
            next((loop.max_cycles for loop in report.loops
                  if loop.task_id == task_id), 0)
            for report in reports
        ]
        for task_id in task_ids
    }
    irq = [report.irq_percent for report in reports]
    unknown = [report.unknown_task_events for report in reports]

    figure, axes = plt.subplots(
        3,
        1,
        figsize=(13, 11),
        sharex=False,
        gridspec_kw={"height_ratios": (2, 1, 1.5)},
        constrained_layout=True,
    )
    top_axis, gantt_axis, loop_axis = axes

    if task_ids:
        colors = plt.get_cmap("tab20")(np.linspace(0.02, 0.96, len(task_ids)))
        top_axis.stackplot(
            x,
            *[usage[task_id] for task_id in task_ids],
            labels=[task_labels[task_id] for task_id in task_ids],
            colors=colors,
            alpha=0.85,
        )
    else:
        gantt_axis.text(
            0.5,
            0.5,
            "No task samples found",
            ha="center",
            va="center",
            transform=gantt_axis.transAxes,
        )
        gantt_axis.set_yticks([])

    top_axis.plot(x, irq, color="#c62828", linewidth=2.0, label="interrupts")
    top_axis.set_ylim(0, 100)
    top_axis.set_ylabel("CPU usage (%)")
    top_axis.grid(axis="y", alpha=0.25)
    top_axis.legend(loc="upper left", ncol=4, fontsize="small")
    top_axis.set_title(title or "ai-app CPU task monitor")
    if any(unknown):
        top_axis.text(
            0.995,
            0.98,
            f"unknown task events: {sum(unknown)}",
            transform=top_axis.transAxes,
            ha="right",
            va="top",
            color="#ad1457",
        )

    all_loop_intervals = [
        interval
        for report in reports
        for interval in report.loop_intervals
        if interval.end_cycles > interval.start_cycles
    ]
    intervals_by_task: dict[int, list[TaskLoopInterval]] = {}
    for interval in all_loop_intervals:
        intervals_by_task.setdefault(interval.task_id, []).append(interval)
    for task_intervals in intervals_by_task.values():
        task_intervals.sort(key=lambda interval: (
            interval.start_cycles, interval.end_cycles
        ))

    repeated_tasks = {
        task_id: task_intervals
        for task_id, task_intervals in intervals_by_task.items()
        if len(task_intervals) >= gantt_repeats
    }
    if repeated_tasks:
        # Start early enough to include the requested number of recent loop
        # intervals for every recurring task represented in the trace.
        window_start_cycles = min(
            task_intervals[-gantt_repeats].start_cycles
            for task_intervals in repeated_tasks.values()
        )
        window_end_cycles = max(
            interval.end_cycles
            for task_intervals in repeated_tasks.values()
            for interval in task_intervals[-1:]
        )
    elif all_loop_intervals:
        # A short trace may not have enough intervals per task yet.
        window_start_cycles = min(
            interval.start_cycles for interval in all_loop_intervals
        )
        window_end_cycles = max(
            interval.end_cycles for interval in all_loop_intervals
        )
    else:
        window_start_cycles = 0
        window_end_cycles = 0

    if all_loop_intervals and cpu_hz > 0.0:
        x_scale = 1.0 / cpu_hz
        gantt_x_label = "elapsed time (s; selected loop window)"
    elif all_loop_intervals:
        mean_period_cycles = max(
            1.0,
            sum(report.period_cycles for report in reports) / len(reports),
        )
        x_scale = 1.0 / mean_period_cycles
        gantt_x_label = "report periods (selected loop window)"
    else:
        x_scale = 1.0
        gantt_x_label = x_label

    if all_loop_intervals:
        window_span_cycles = max(1, window_end_cycles - window_start_cycles)
        margin_cycles = window_span_cycles * 0.05
        window_start_cycles -= int(margin_cycles)
        window_end_cycles += int(margin_cycles)
        origin_cycles = window_start_cycles
        gantt_xlim = (
            0.0,
            (window_end_cycles - window_start_cycles) * x_scale,
        )
        visible_intervals = [
            interval for interval in all_loop_intervals
            if interval.end_cycles >= window_start_cycles
            and interval.start_cycles <= window_end_cycles
        ]
    else:
        origin_cycles = 0
        gantt_xlim = None
        visible_intervals = []

    plotted_task_ids = sorted({
        interval.task_id for interval in visible_intervals
    })
    loop_intervals = visible_intervals

    row_for_task = {task_id: row
                    for row, task_id in enumerate(plotted_task_ids)}
    color_for_task = {
        task_id: colors[task_ids.index(task_id)]
        for task_id in plotted_task_ids
        if task_id in task_ids
    }
    for task_id in plotted_task_ids:
        if task_id not in color_for_task:
            color_for_task[task_id] = plt.get_cmap("tab20")(
                (len(color_for_task) + 1) / 20.0
            )
    # With a CPU clock this is seconds; without one, x_scale uses report
    # periods, so 0.1 joins gaps shorter than roughly one tenth of a period.
    merge_gap = LOOP_INTERVAL_MERGE_GAP_MS / 1000.0
    merge_gap_label = (
        f"{LOOP_INTERVAL_MERGE_GAP_MS} ms"
        if cpu_hz > 0.0 else "0.1 report periods"
    )
    for task_id in plotted_task_ids:
        task_intervals = []
        for interval in loop_intervals:
            if interval.task_id != task_id:
                continue
            start = (interval.start_cycles - origin_cycles) * x_scale
            end = (interval.end_cycles - origin_cycles) * x_scale
            if end > start:
                task_intervals.append((start, end))
        task_intervals.sort()
        bursts: list[list[float]] = []
        for start, end in task_intervals:
            if bursts and start - bursts[-1][1] <= merge_gap:
                bursts[-1][1] = max(bursts[-1][1], end)
            else:
                bursts.append([start, end])
        bars = [(start, end - start) for start, end in bursts]
        gantt_axis.broken_barh(
            bars,
            (row_for_task[task_id] - 0.32, 0.64),
            facecolors=color_for_task[task_id],
            edgecolors="#37474f",
            linewidth=0.25,
        )
    if plotted_task_ids:
        gantt_axis.set_yticks(range(len(plotted_task_ids)))
        gantt_axis.set_yticklabels([task_labels[task_id]
                                    for task_id in plotted_task_ids])
        gantt_axis.set_ylim(-0.7, max(0.7, len(plotted_task_ids) - 0.3))
        gantt_axis.invert_yaxis()
    else:
        gantt_axis.text(
            0.5,
            0.5,
            "No per-loop intervals in this trace; use a v3 CPU trace",
            ha="center",
            va="center",
            transform=gantt_axis.transAxes,
        )
        gantt_axis.set_yticks([])
    gantt_axis.set_ylabel("task (ID)")
    gantt_axis.set_xlabel(gantt_x_label)
    gantt_title = (
        f"Window covers latest {gantt_repeats} loops per recurring task"
        if repeated_tasks else
        "Task activity from captured loop intervals"
    )
    gantt_axis.set_title(
        f"{gantt_title}\nGaps up to {merge_gap_label} joined"
    )
    gantt_axis.grid(axis="x", alpha=0.25)
    if gantt_xlim is not None:
        gantt_axis.set_xlim(*gantt_xlim)

    loop_scale = 1000.0 / cpu_hz if cpu_hz > 0.0 else 1.0
    loop_unit = "ms" if cpu_hz > 0.0 else "cycles"
    plotted_loops = False
    for task_id in task_ids:
        if not any(loop_values[task_id]) and not any(loop_max_values[task_id]):
            continue
        color = colors[task_ids.index(task_id)] if task_ids else None
        loop_axis.plot(
            x,
            [value * loop_scale for value in loop_values[task_id]],
            color=color,
            label=f"{task_labels[task_id]} avg",
        )
        loop_axis.plot(
            x,
            [value * loop_scale for value in loop_max_values[task_id]],
            color=color,
            linestyle="--",
            alpha=0.7,
            label=f"{task_labels[task_id]} max",
        )
        plotted_loops = True
    if not plotted_loops:
        loop_axis.text(
            0.5,
            0.5,
            "No task loop samples found",
            ha="center",
            va="center",
            transform=loop_axis.transAxes,
        )
    else:
        loop_axis.legend(loc="upper left", ncol=2, fontsize="small")
    loop_axis.set_ylabel(f"loop duration ({loop_unit})")
    loop_axis.set_xlabel(x_label)
    loop_axis.grid(axis="y", alpha=0.25)
    path.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(path, dpi=140)
    plt.close(figure)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Visualize ai-app cpu_task_monitor UART output"
    )
    parser.add_argument(
        "input",
        help="UART capture text file, or '-' to read stdin",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path("cpu_task_monitor.png"),
        help="PNG/SVG output path (default: cpu_task_monitor.png)",
    )
    parser.add_argument(
        "--csv",
        type=Path,
        help="also write a long-form CSV report",
    )
    parser.add_argument(
        "--json",
        type=Path,
        help="also write normalized report JSON",
    )
    parser.add_argument(
        "--cpu-hz",
        type=float,
        default=0.0,
        help="DWT frequency; use seconds on x-axis when non-zero",
    )
    parser.add_argument(
        "--top-tasks",
        type=int,
        default=12,
        help="number of task IDs to draw, 0 means all (default: 12)",
    )
    parser.add_argument(
        "--gantt-repeats",
        type=int,
        default=DEFAULT_GANTT_REPEATS,
        help=(
            "set the Gantt window to include the latest N executions of each "
            f"recurring task (default: {DEFAULT_GANTT_REPEATS})"
        ),
    )
    parser.add_argument("--title", help="plot title")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        reports = read_reports(args.input)
        if not reports:
            raise ValueError("no CPU monitor reports found")
        if args.gantt_repeats <= 0:
            raise ValueError("--gantt-repeats must be greater than zero")
        if args.csv is not None:
            write_csv(args.csv, reports)
        if args.json is not None:
            write_json(args.json, reports)
        plot_reports(
            args.output,
            reports,
            args.cpu_hz,
            args.top_tasks,
            args.gantt_repeats,
            args.title,
        )
    except (OSError, ValueError, ImportError) as error:
        print(f"cpu_task_monitor: {error}", file=sys.stderr)
        return 2

    print(f"wrote {args.output} ({len(reports)} reports)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

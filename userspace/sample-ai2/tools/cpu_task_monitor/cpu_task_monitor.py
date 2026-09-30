#!/usr/bin/env python3
"""Parse and visualize sample-ai2 CPU task monitor UART output.

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
)
CPU_TRACE_MAGIC = 0x43544D4E
CPU_TRACE_VERSION = 1
CPU_TRACE_COMMIT_MAGIC = 0x43544D43
CPU_TRACE_HEADER = struct.Struct("<IHH14I")
CPU_TRACE_RECORD = struct.Struct("<11IB3xI3I")
CPU_TRACE_REPORT = 1
CPU_TRACE_TASK = 2


@dataclass
class TaskSample:
    task_id: int
    usage_percent: int
    cycles: int
    dispatch_count: int
    state: int


@dataclass
class Report:
    sample: int
    period_cycles: int
    irq_percent: int
    interrupt_count: int
    unknown_task_events: int
    tasks: list[TaskSample] = field(default_factory=list)


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
        current.tasks.append(
            TaskSample(
                task_id=int(values["task_id"]),
                usage_percent=int(values["usage"]),
                cycles=int(values["cycles"]),
                dispatch_count=int(values["dispatch"]),
                state=int(values["state"], 16),
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
     _reserved0, _reserved1, _reserved2) = values
    if magic != CPU_TRACE_MAGIC:
        raise ValueError(f"invalid CPU trace magic: 0x{magic:08x}")
    if version != CPU_TRACE_VERSION:
        raise ValueError(f"unsupported CPU trace version: {version}")
    if header_size != CPU_TRACE_HEADER.size or record_size != CPU_TRACE_RECORD.size:
        raise ValueError(
            f"unsupported CPU trace layout: header={header_size}, "
            f"record={record_size}"
        )
    if capacity == 0 or write_index >= capacity or record_count > capacity:
        raise ValueError("invalid CPU trace ring state")
    if header_size + capacity * record_size > len(data):
        raise ValueError("CPU trace dump does not contain the complete ring")

    count = min(record_count, capacity)
    first_index = (write_index - count) % capacity
    reports: list[Report] = []
    current: Report | None = None
    current_timestamp = None
    for offset in range(count):
        index = (first_index + offset) % capacity
        fields = CPU_TRACE_RECORD.unpack_from(data, header_size + index * record_size)
        (sequence, timestamp_cycles, period_cycles, task_id, state, cycles,
         dispatch_count, usage_percent, irq_percent, interrupt_count,
         unknown_events, record_type, commit_marker, _r0, _r1, _r2) = fields
        if commit_marker != (CPU_TRACE_COMMIT_MAGIC ^ sequence):
            continue
        if record_type == CPU_TRACE_REPORT:
            current = Report(
                sample=len(reports),
                period_cycles=period_cycles,
                irq_percent=irq_percent,
                interrupt_count=interrupt_count,
                unknown_task_events=unknown_events,
            )
            reports.append(current)
            current_timestamp = timestamp_cycles
        elif record_type == CPU_TRACE_TASK:
            if current is None or current_timestamp != timestamp_cycles:
                current = Report(
                    sample=len(reports),
                    period_cycles=period_cycles,
                    irq_percent=irq_percent,
                    interrupt_count=interrupt_count,
                    unknown_task_events=unknown_events,
                )
                reports.append(current)
                current_timestamp = timestamp_cycles
            current.tasks.append(TaskSample(
                task_id=task_id,
                usage_percent=usage_percent,
                cycles=cycles,
                dispatch_count=dispatch_count,
                state=state,
            ))
    return reports


def read_reports(path: str) -> list[Report]:
    if path == "-":
        return parse_reports(sys.stdin.read())
    data = Path(path).read_bytes()
    if len(data) >= 4 and struct.unpack_from("<I", data)[0] == CPU_TRACE_MAGIC:
        return parse_binary_trace(data)
    return parse_reports(data.decode("utf-8", errors="replace"))


def select_reports(reports: list[Report], window: int) -> list[Report]:
    if window <= 0 or len(reports) <= window:
        return reports
    selected = reports[-window:]
    # Keep the x-axis local to the selected window.
    return [
        Report(
            sample=index,
            period_cycles=report.period_cycles,
            irq_percent=report.irq_percent,
            interrupt_count=report.interrupt_count,
            unknown_task_events=report.unknown_task_events,
            tasks=report.tasks,
        )
        for index, report in enumerate(selected)
    ]


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
            "task_usage_percent",
            "task_cycles",
            "dispatch_count",
            "state",
        ])
        for report in reports:
            if not report.tasks:
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
                ])
                continue
            for task in report.tasks:
                writer.writerow([
                    report.sample,
                    report.period_cycles,
                    report.irq_percent,
                    report.interrupt_count,
                    report.unknown_task_events,
                    task.task_id,
                    task.usage_percent,
                    task.cycles,
                    task.dispatch_count,
                    f"0x{task.state:x}",
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
    title: str | None,
) -> None:
    if not reports:
        raise ValueError("no 'cpu: period=...' reports found in input")

    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np

    task_ids = task_order(reports, top_tasks)
    x, x_label = report_x_values(reports, cpu_hz)
    usage = {
        task_id: [
            next((task.usage_percent for task in report.tasks
                  if task.task_id == task_id), 0)
            for report in reports
        ]
        for task_id in task_ids
    }
    irq = [report.irq_percent for report in reports]
    unknown = [report.unknown_task_events for report in reports]

    figure, axes = plt.subplots(
        2,
        1,
        figsize=(13, 8),
        sharex=True,
        gridspec_kw={"height_ratios": (2, 1)},
        constrained_layout=True,
    )
    top_axis, heatmap_axis = axes

    if task_ids:
        colors = plt.get_cmap("tab20")(np.linspace(0.02, 0.96, len(task_ids)))
        top_axis.stackplot(
            x,
            *[usage[task_id] for task_id in task_ids],
            labels=[f"task {task_id}" for task_id in task_ids],
            colors=colors,
            alpha=0.85,
        )
        matrix = np.array([usage[task_id] for task_id in task_ids])
        image = heatmap_axis.imshow(
            matrix,
            aspect="auto",
            origin="lower",
            interpolation="nearest",
            extent=(x[0], x[-1] if len(x) > 1 else x[0] + 1, -0.5, len(task_ids) - 0.5),
            vmin=0,
            vmax=100,
            cmap="magma",
        )
        heatmap_axis.set_yticks(range(len(task_ids)))
        heatmap_axis.set_yticklabels([f"task {task_id}" for task_id in task_ids])
        figure.colorbar(image, ax=heatmap_axis, label="usage (%)")
    else:
        heatmap_axis.text(
            0.5,
            0.5,
            "No task samples found",
            ha="center",
            va="center",
            transform=heatmap_axis.transAxes,
        )
        heatmap_axis.set_yticks([])

    top_axis.plot(x, irq, color="#c62828", linewidth=2.0, label="interrupts")
    top_axis.set_ylim(0, 100)
    top_axis.set_ylabel("CPU usage (%)")
    top_axis.grid(axis="y", alpha=0.25)
    top_axis.legend(loc="upper left", ncol=4, fontsize="small")
    top_axis.set_title(title or "sample-ai2 CPU task monitor")
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

    heatmap_axis.set_xlabel(x_label)
    heatmap_axis.set_ylabel("task")
    heatmap_axis.grid(False)
    path.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(path, dpi=140)
    plt.close(figure)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Visualize sample-ai2 cpu_task_monitor UART output"
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
        "--window",
        type=int,
        default=0,
        help="plot only the last N reports, 0 means all",
    )
    parser.add_argument("--title", help="plot title")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        reports = select_reports(read_reports(args.input), args.window)
        if not reports:
            raise ValueError("no CPU monitor reports found")
        if args.csv is not None:
            write_csv(args.csv, reports)
        if args.json is not None:
            write_json(args.json, reports)
        plot_reports(args.output, reports, args.cpu_hz, args.top_tasks, args.title)
    except (OSError, ValueError, ImportError) as error:
        print(f"cpu_task_monitor: {error}", file=sys.stderr)
        return 2

    print(f"wrote {args.output} ({len(reports)} reports)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

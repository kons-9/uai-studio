#!/usr/bin/env python3
"""Decode a raw ThreadMonitor internal APP RAM dump into JSON.

The input is the 128 KiB linker-reserved region whose address is exposed by
__sample_ai_thread_monitor_start__. The format is kept independent from the
firmware ABI so the dump can be decoded after a crash.
"""

import argparse
import json
import struct
from pathlib import Path


TRACE_MAGIC = 0x544D4F4E
TRACE_VERSION = 4
LEGACY_TRACE_VERSION = 2
COMMIT_MAGIC = 0x434D4954
HEADER = struct.Struct("<IHH14I")
RECORD = struct.Struct("<12IBBH3I")
PHASE_NAMES = {
    1: "model_selection",
    2: "input_preparation",
    3: "npu_execution",
    4: "output_preparation",
    5: "output_decoding",
    6: "result_conversion",
    7: "input_preparation_wait",
}
MODEL_KIND_NAMES = {
    0: "person",
    1: "segmentation",
    2: "face",
}
CALLBACK_STAGE_NAMES = {
    1: "cpu_start",
    2: "npu",
    3: "cpu_end",
}
PIPELINE_STAGE_NAMES = {
    0: "copy",
    1: "resize",
    2: "letterbox",
    3: "input_cache",
    4: "submit",
    5: "irq_wait",
    6: "epoch_continue",
    7: "output_cache",
    8: "decode",
    9: "convert",
    10: "finalize",
}


def signed32(value: int) -> int:
    return value if value < 0x80000000 else value - 0x100000000


def record_type_name(value: int) -> str:
    return {1: "sample", 2: "fault", 3: "npu_execution",
            4: "inference_phase", 5: "npu_epoch",
            6: "pipeline_stage"}.get(
        value, "unknown"
    )


def decode(path: Path) -> dict:
    data = path.read_bytes()
    if len(data) < HEADER.size:
        raise ValueError(f"dump is too small: {len(data)} bytes")

    values = HEADER.unpack_from(data)
    (magic, version, header_size, record_size, capacity, write_index,
     record_count, dropped_count, boot_count, monitored_task_id,
     monitor_task_id, last_fault_code, fault_count, next_sequence,
     _reserved0, _reserved1, _reserved2) = values
    if magic != TRACE_MAGIC:
        raise ValueError(f"invalid magic: 0x{magic:08x}")
    if version not in (LEGACY_TRACE_VERSION, 3, TRACE_VERSION):
        raise ValueError(f"unsupported version: {version}")
    if header_size != HEADER.size or record_size != RECORD.size:
        raise ValueError(
            f"unsupported layout: header={header_size}, record={record_size}"
        )
    if capacity == 0 or write_index >= capacity or record_count > capacity:
        raise ValueError(
            f"invalid ring state: capacity={capacity}, write_index={write_index}, "
            f"record_count={record_count}"
        )
    if header_size + capacity * record_size > len(data):
        raise ValueError("dump does not contain the complete trace region")

    count = min(record_count, capacity)
    first_index = (write_index - count) % capacity
    records = []
    for offset in range(count):
        index = (first_index + offset) % capacity
        values = RECORD.unpack_from(data, header_size + index * record_size)
        (sequence, timestamp_ms, monitored_id, monitor_id, task_state,
         wait_factor, wait_object_id, current_priority, base_priority,
         progress_tick, reference_status, fault_code, record_type, flags,
         phase_id, commit_marker, npu_elapsed_ms, kind_or_status) = values
        if commit_marker != (COMMIT_MAGIC ^ sequence):
            continue
        records.append({
            "index": index,
            "sequence": sequence,
            "timestamp_ms": timestamp_ms,
            "monitored_task_id": monitored_id,
            "monitor_task_id": monitor_id,
            "task_state": task_state,
            "wait_factor": wait_factor,
            "wait_object_id": wait_object_id,
            "current_priority": signed32(current_priority),
            "base_priority": signed32(base_priority),
            "progress_tick": progress_tick,
            "reference_status": signed32(reference_status),
            "fault_code": fault_code,
            "type": record_type_name(record_type),
            "type_id": record_type,
            "phase_id": (phase_id if version >= 3 else None),
            "phase": (PHASE_NAMES.get(phase_id)
                      if record_type in (3, 4) else None),
            "flags": flags,
            "npu_timing_valid": bool(flags & (1 << 2)),
            "npu_cycle_valid": bool(flags & (1 << 3)),
            "npu_elapsed_ms": npu_elapsed_ms,
            "elapsed_ms": (npu_elapsed_ms
                           if record_type in (3, 4) else None),
            "stage_timing_valid": (record_type == 6 and
                                    bool(flags & (1 << 3))),
            "stage_cycle_end": (progress_tick if record_type == 6 else None),
            "stage_cycle_elapsed": (npu_elapsed_ms
                                     if record_type == 6 else None),
            "model_kind": (MODEL_KIND_NAMES.get(kind_or_status)
                            if version >= 3 else None),
            "model_kind_id": (kind_or_status if version >= 3
                               else None),
            "npu_status": (kind_or_status if version < 3
                            else None),
            "epoch_index": (wait_factor if record_type == 5 else None),
            "epoch_flags": (task_state if record_type == 5 else None),
            "epoch_address": (wait_object_id if record_type == 5 else None),
            "callback_type": (signed32(reference_status)
                              if record_type == 5 else None),
            "callback_stage": (CALLBACK_STAGE_NAMES.get(
                signed32(reference_status), "legacy_post_end")
                               if record_type == 5 else None),
            "pipeline_stage_id": (phase_id if record_type == 6 else None),
            "pipeline_stage": (PIPELINE_STAGE_NAMES.get(
                phase_id, f"stage={phase_id}") if record_type == 6 else None),
            "cycle_end": (progress_tick if record_type == 5 else None),
            "cycle_elapsed": (npu_elapsed_ms if record_type == 5 else None),
            "cycle_start": ((progress_tick - npu_elapsed_ms) & 0xFFFFFFFF
                            if record_type == 5 else None),
        })

    return {
        "header": {
            "magic": f"0x{magic:08x}",
            "version": version,
            "capacity": capacity,
            "write_index": write_index,
            "record_count": record_count,
            "decoded_count": len(records),
            "dropped_count": dropped_count,
            "boot_count": boot_count,
            "monitored_task_id": monitored_task_id,
            "monitor_task_id": monitor_task_id,
            "last_fault_code": last_fault_code,
            "fault_count": fault_count,
            "next_sequence": next_sequence,
        },
        "records": records,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dump", type=Path, help="raw 128 KiB trace dump")
    parser.add_argument("-o", "--output", type=Path,
                        help="write JSON to this file instead of stdout")
    parser.add_argument("--pretty", action="store_true",
                        help="pretty-print JSON")
    args = parser.parse_args()

    result = decode(args.dump)
    text = json.dumps(result, indent=2 if args.pretty else None,
                      separators=None if args.pretty else (",", ":"))
    if args.output:
        args.output.write_text(text + "\n", encoding="utf-8")
    else:
        print(text)


if __name__ == "__main__":
    main()

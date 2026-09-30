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
TRACE_VERSION = 5
LEGACY_TRACE_VERSIONS = {2, 3, 4}
COMMIT_MAGIC = 0x434D4954
HEADER = struct.Struct("<IHH14I")
RECORD = struct.Struct("<12IBBH3I")
MODEL_NAME_ENTRY = struct.Struct("<I28s")
MODEL_NAME_CAPACITY = 16
TRACE_DATA_OFFSET = HEADER.size + MODEL_NAME_CAPACITY * MODEL_NAME_ENTRY.size


def signed32(value: int) -> int:
    return value if value < 0x80000000 else value - 0x100000000


def record_type_name(value: int) -> str:
    return {1: "sample", 2: "fault", 3: "npu_execution",
            4: "inference_phase", 5: "npu_epoch",
            6: "pipeline_stage", 7: "ai_runtime_step"}.get(
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
    if version not in LEGACY_TRACE_VERSIONS | {TRACE_VERSION}:
        raise ValueError(f"unsupported version: {version}")
    model_name_count = _reserved0
    model_name_entry_size = _reserved1
    data_offset = HEADER.size
    if version == TRACE_VERSION:
        if (header_size != TRACE_DATA_OFFSET
                or model_name_entry_size != MODEL_NAME_ENTRY.size
                or model_name_count > MODEL_NAME_CAPACITY):
            raise ValueError(
                f"unsupported model-name table: offset={header_size}, "
                f"entry={model_name_entry_size}, count={model_name_count}"
            )
        data_offset = TRACE_DATA_OFFSET
    elif header_size != HEADER.size:
        raise ValueError(f"unsupported header size: {header_size}")
    if record_size != RECORD.size:
        raise ValueError(
            f"unsupported layout: header={header_size}, record={record_size}"
        )
    if capacity == 0 or write_index >= capacity or record_count > capacity:
        raise ValueError(
            f"invalid ring state: capacity={capacity}, write_index={write_index}, "
            f"record_count={record_count}"
        )
    if data_offset + capacity * record_size > len(data):
        raise ValueError("dump does not contain the complete trace region")

    if version == TRACE_VERSION:
        model_names = {}
        for index in range(MODEL_NAME_CAPACITY):
            model_id, encoded_name = MODEL_NAME_ENTRY.unpack_from(
                data, HEADER.size + index * MODEL_NAME_ENTRY.size
            )
            name = encoded_name.split(b"\0", 1)[0].decode(
                "utf-8", errors="replace"
            )
            if model_id != 0xFFFFFFFF and name:
                model_names[model_id] = name
        if len(model_names) != model_name_count:
            raise ValueError(
                "model-name count does not match the metadata table"
            )
    else:
        # Legacy traces do not contain model-name metadata. Keep their IDs
        # unresolved instead of maintaining a Python-side ID/name table.
        model_names = {}

    count = min(record_count, capacity)
    first_index = (write_index - count) % capacity
    records = []
    for offset in range(count):
        index = (first_index + offset) % capacity
        values = RECORD.unpack_from(data, data_offset + index * record_size)
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
            "model_kind_id": (kind_or_status if version >= 3
                               else None),
            "npu_status": (kind_or_status if version < 3
                            else None),
            "epoch_index": (wait_factor if record_type == 5 else None),
            "epoch_flags": (task_state if record_type == 5 else None),
            "epoch_address": (wait_object_id if record_type == 5 else None),
            "callback_type": (signed32(reference_status)
                              if record_type == 5 else None),
            "pipeline_stage_id": (phase_id if record_type == 6 else None),
            "ai_runtime_inference_id": (task_state
                                         if record_type == 7 else None),
            "ai_runtime_lane_id": (wait_factor if record_type == 7 else None),
            "ai_runtime_step_id": (wait_object_id if record_type == 7 else None),
            "ai_runtime_begin": (bool(flags & (1 << 4))
                                 if record_type == 7 else None),
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
            "model_names": {
                str(model_id): name
                for model_id, name in sorted(model_names.items())
            },
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

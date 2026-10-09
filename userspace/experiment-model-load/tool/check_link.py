import argparse
import pathlib
import re
import subprocess

from layout import BLOB_ADDRESS, BLOB_CAPACITY, WEIGHTS_ADDRESS, WEIGHTS_CAPACITY

EXPECTED = {
    "HAL_GetTick": "hal_time.c",
    "HAL_Delay": "hal_time.c",
    "HAL_DCMIPP_PIPE_VsyncEventCallback": "frame_events.c",
    "HAL_DCMIPP_PIPE_FrameEventCallback": "frame_events.c",
    "experiment_original_vsync": "dcmipp_callbacks.c",
    "experiment_original_frame": "dcmipp_callbacks.c",
    "USART1_IRQHandler": "main.cpp",
    "CSI_IRQHandler": "irq_handlers.c",
    "DCMIPP_IRQHandler": "irq_handlers.c",
}


def audit(symbols, map_text, npu=False, multi=False):
    expected = dict(EXPECTED)
    if npu:
        expected.update(NPU0_IRQHandler="npu_model.c", model_load_NPU0_IRQHandler="ll_aton_runtime.c")
        expected["experiment_npu_prepare" if multi else "experiment_npu_start"] = "npu_model.c"
    definitions = {}
    for line in symbols.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[2] in expected:
            if fields[2] in definitions:
                raise ValueError("multiple definitions: " + fields[2])
            definitions[fields[2]] = (int(fields[0], 16), fields[1])
    for symbol, source in expected.items():
        address, binding = definitions.get(symbol, (0, "missing"))
        if binding != "T" or address == 0:
            raise ValueError(f"{symbol}: expected strong text symbol, got {binding}")
        pattern = rf"\.text\.{re.escape(symbol)}\s+(0x[0-9a-fA-F]+)\s+0x[0-9a-fA-F]+\s+(\S+)"
        objects = [origin for location, origin in re.findall(pattern, map_text) if int(location, 16) == address]
        if len(objects) != 1 or not objects[0].endswith((source + ".obj", source + ".o")):
            raise ValueError(f"{symbol}: unexpected or missing map origin: {objects}")

def audit_slots(symbols):
    addresses = {}
    for line in symbols.splitlines():
        fields = line.split()
        if len(fields) == 3:
            addresses[fields[2]] = int(fields[0], 16)
    for name, base, capacity in (("activations", 0x90400000, 0x800000),
                                 ("weights", WEIGHTS_ADDRESS, WEIGHTS_CAPACITY),
                                 ("blob", BLOB_ADDRESS, BLOB_CAPACITY),
                                 ("scratch", 0x342e0000, 0xe0000)):
        start = addresses.get(f"__experiment_{name}_start__")
        end = addresses.get(f"__experiment_{name}_end__")
        if start != base or end is None or not base < end <= base + capacity:
            raise ValueError(f"{name}: missing or invalid reserved slot: {start}, {end}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", type=pathlib.Path, required=True)
    parser.add_argument("--map", type=pathlib.Path, required=True)
    parser.add_argument("--nm", default="arm-none-eabi-nm")
    parser.add_argument("--npu", action="store_true")
    parser.add_argument("--multi", action="store_true")
    arguments = parser.parse_args()
    try:
        symbols = subprocess.run([arguments.nm, "--defined-only", str(arguments.elf)], check=True,
                                 capture_output=True, text=True).stdout
        audit(symbols, arguments.map.read_text(), arguments.npu, arguments.multi)
        audit_slots(symbols)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + "\n")
    print("PASS: HAL overrides and IRQ handlers have the expected strong definitions and source objects")


if __name__ == "__main__":
    main()

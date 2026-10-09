import argparse
import pathlib
import re
import subprocess

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


def audit(symbols, map_text):
    definitions = {}
    expected = dict(EXPECTED)
    for fragment, source in (
        ("5dma2d11Dma2dDriver8Transfer", "dma2d_driver.cpp"),
        ("2ui6Canvas8FillRect", "canvas.cpp"),
    ):
        matches = [line.split()[2] for line in symbols.splitlines()
                   if len(line.split()) == 3 and fragment in line.split()[2]]
        if len(matches) != 1:
            raise ValueError(f"missing or ambiguous shared implementation: {fragment}")
        expected[matches[0]] = source
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
        if len(objects) != 1 or not objects[0].endswith(
            (source + ".obj", source + ".o", source + ".obj)", source + ".o)")
        ):
            raise ValueError(f"{symbol}: unexpected or missing map origin: {objects}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", type=pathlib.Path, required=True)
    parser.add_argument("--map", type=pathlib.Path, required=True)
    parser.add_argument("--nm", default="arm-none-eabi-nm")
    arguments = parser.parse_args()
    try:
        symbols = subprocess.run([arguments.nm, "--defined-only", str(arguments.elf)], check=True,
                                 capture_output=True, text=True).stdout
        audit(symbols, arguments.map.read_text())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + "\n")
    print("PASS: HAL/IRQ overrides and shared UI/DMA2D implementations have the expected strong definitions and origins")


if __name__ == "__main__":
    main()
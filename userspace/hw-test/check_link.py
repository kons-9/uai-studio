import argparse
import pathlib
import re
import subprocess

EXPECTED = {
    "HAL_GetTick": "hal_time.c",
    "HAL_Delay": "hal_time.c",
    "BSP_XSPI_NOR_Init": "stm32n6570_discovery_xspi.c",
    "BSP_XSPI_RAM_Init": "stm32n6570_discovery_xspi.c",
    "HAL_DCMIPP_PIPE_VsyncEventCallback": "dcmipp_callbacks.c",
    "HAL_DCMIPP_PIPE_FrameEventCallback": "dcmipp_callbacks.c",
    "BSP_CAMERA_FrameEventCallback": "camera_driver.cpp",
    "AiCameraPipe2FrameEventCallback": "camera_driver.cpp",
    "MX_DCMIPP_ClockConfig": "camera_driver.cpp",
    "USART1_IRQHandler": "console_registers.cpp",
    "CSI_IRQHandler": "camera_driver.cpp",
    "DCMIPP_IRQHandler": "camera_driver.cpp",
}

SHARED = (
    ("5dma2d11Dma2dDriver8Transfer", "dma2d_driver.cpp"),
    ("PeripheralDriver4Copy", "peripheral_driver.cpp"),
    ("NorDriver4Read", "nor_driver.cpp"),
    ("ReadDisplayState", "lcd_registers.cpp"),
    ("CacheDriver15CleanInvalidate", "cache_driver.cpp"),
    ("CameraDriver5StartERK", "camera_driver.cpp"),
    ("CameraDriver7Recover", "camera_driver.cpp"),
    ("CameraDriver9Configure", "camera_driver.cpp"),
    ("IspControls4Read", "isp_controls.cpp"),
    ("DisplayDriver7Present", "display_driver.cpp"),
    ("TouchDriver7ReadRaw", "touch_driver.cpp"),
    ("ConsoleDriver5Write", "console_driver.cpp"),
    ("board12Milliseconds", "time.cpp"),
    ("2ui6Canvas8FillRect", "canvas.cpp"),
)


def audit(symbols, map_text):
    definitions = {}
    expected = dict(EXPECTED)
    for fragment, source in SHARED:
        matches = [line.split()[2] for line in symbols.splitlines()
                   if len(line.split()) == 3 and fragment in line.split()[2]
                   and not re.search(r"\.(?:part|isra|constprop|clone)(?:\.\d+)?$",
                                     line.split()[2])]
        if len(matches) != 1:
            raise ValueError(f"missing or ambiguous shared implementation: {fragment}")
        expected[matches[0]] = source
    for line in symbols.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[2] == "npu_cache_enable":
            expected[fields[2]] = "npu_cache.c"
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
        shared_source = source in {shared_source for _, shared_source in SHARED}
        kernel_symbol = symbol in EXPECTED or symbol == "npu_cache_enable"
        if (shared_source or kernel_symbol) and "kernel/" not in objects[0]:
            raise ValueError(f"{symbol}: implementation must come from kernel: {objects[0]}")


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
    print("PASS: HAL/IRQ overrides and shared UI/driver implementations have the expected strong definitions and origins")


if __name__ == "__main__":
    main()

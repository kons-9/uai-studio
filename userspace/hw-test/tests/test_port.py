import importlib.util
import pathlib
import re
import subprocess
import tempfile
import unittest


APP = pathlib.Path(__file__).resolve().parents[1]
ROOT = APP.parents[1]
EXPERIMENT = APP.parent / "experiment-hw-test"


def load_module(name, path):
    specification = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


def canonical(path):
    if path.suffix in (".c", ".h", ".cpp", ".hpp"):
        probe = ROOT / ("hwtest-format-probe" + path.suffix)
        return subprocess.run(["clang-format-21", "--style=file", "--assume-filename", str(probe)],
            input=path.read_bytes(), capture_output=True, check=True).stdout
    return path.read_bytes()


class PortTests(unittest.TestCase):
    def test_camera_and_ui_preserve_experiment_processing(self):
        paths = ["src/camera_runtime", "src/ui"]
        for directory in paths:
            for original in (EXPERIMENT / directory).rglob("*"):
                if not original.is_file() or "__pycache__" in original.parts:
                    continue
                relative = original.relative_to(EXPERIMENT)
                with self.subTest(path=relative):
                    self.assertEqual(canonical(APP / relative), canonical(original))
        for name in ("commands.hpp", "hwtest.hpp", "shell.hpp", "display_log.hpp"):
            self.assertEqual(canonical(APP / "src" / name), canonical(EXPERIMENT / "src" / name))
        self.assertFalse((APP / "src" / "middleware").exists())

    def test_hardware_cases_do_not_access_hal_bsp_or_registers(self):
        forbidden = re.compile(
            r"\b(?:HAL_\w+|__HAL_\w+|BSP_\w+|SCB_\w+|RIF_\w+|RCC_\w+|"
            r"SecurePeripheral|DWT|CoreDebug|\w+_HandleTypeDef|__DSB|__ISB|__DMB|asm)\b|"
            r"\b(?:LTDC\w*|DMA2D|RNG|CRC|HASH|RTC|TIM\d+|XSPI\d+|RISAF\w*)\s*->")
        literals = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
        for path in (APP / "src" / "tests").rglob("*"):
            if path.suffix not in (".cpp", ".hpp"):
                continue
            with self.subTest(path=path.relative_to(APP)):
                text = path.read_text()
                self.assertNotRegex(text, r'#include\s*[<"][^>"\n]*(?:stm32|mx66|aps256|/registers/|/c_bsp/|CMSIS)')
                self.assertNotRegex(literals.sub("", text), forbidden)

    def test_hardware_cases_compile_without_hal_headers(self):
        external = {"sdmmc_driver", "ethernet_driver", "usb_driver", "audio_driver", "external_input_driver"}
        sources = [str(path) for path in (APP / "src" / "tests").rglob("*.cpp")
                   if path.parent.name not in external]
        subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
            "-I", str(APP / "src"), "-I", str(ROOT / "kernel"), "-I", str(ROOT / "kernel" / "middleware"),
            "-I", str(ROOT / "kernel" / "utkernel" / "linux" / "include"), *sources], check=True)

    def test_generated_memory_contract_has_no_models_and_avoids_hw_scratch(self):
        generator = load_module("hwtest_generate_memory", APP / "generate_memory.py")
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory)
            generator.generate(APP / "config" / "memory_layout.json", output)
            generated = output / "middleware" / "memory" / "generated"
            config = (generated / "memory_config.hpp").read_text()
            self.assertIn("std::array<std::size_t, 0U>", config)
            self.assertTrue((generated / "static_memory_layout" / "raw.hpp").is_file())
            linker = (output / "memory-contract.ld").read_text()
            self.assertIn("0x91010000", linker)
            self.assertIn("INSERT AFTER .bss", linker)
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-I", str(output), "-I", str(ROOT / "kernel" / "utkernel" / "linux" / "include"),
                "-I", str(ROOT / "kernel"), "-I", str(ROOT / "kernel" / "middleware"), "-fsyntax-only",
                str(ROOT / "kernel" / "middleware" / "memory" / "static_memory_layout.cpp"),
                str(ROOT / "kernel" / "middleware" / "memory_manager" / "memory_manager.cpp")], check=True)

    def test_build_registration_links_shared_ui_and_driver_without_npu(self):
        script = '''
set(PROJECT_SOURCE_DIR "@ROOT@")
set(CMAKE_SOURCE_DIR "@ROOT@")
set(APP_TARGET hw-test)
set(STM32CUBE_N6_DIR /missing-stm32cube)
set(CUBEMX_OUTPUT_DIR /missing-cubemx)
set(UAI_GENERATED_INCLUDE_DIR /missing-generated)
foreach(command target_include_directories target_compile_definitions target_compile_options target_link_libraries set_source_files_properties add_custom_command add_custom_target add_dependencies target_sources target_link_options set_target_properties)
    function(${command})
    endfunction()
endforeach()
function(add_library name)
    if(name STREQUAL "uai-drivers")
        set(driver_sources "${ARGN}" PARENT_SCOPE)
    endif()
endfunction()
function(add_executable name)
    set(application_sources "${ARGN}" PARENT_SCOPE)
endfunction()
include("@ROOT@/build-system/cmake/camera_board.cmake")
if(NOT UAI_CAMERA_BOARD_APP STREQUAL "hw-test")
    message(FATAL_ERROR "hw-test camera board missing")
endif()
set(CMAKE_CURRENT_SOURCE_DIR "@ROOT@/kernel/driver")
include("@ROOT@/kernel/driver/CMakeLists.txt")
if(driver_sources MATCHES "/npu_driver/")
    message(FATAL_ERROR "hw-test still depends on NPU runtime")
endif()
if(NOT driver_sources MATCHES "dma2d_driver.cpp")
    message(FATAL_ERROR "shared DMA2D driver missing")
endif()
if(NOT driver_sources MATCHES "peripheral_driver.cpp")
    message(FATAL_ERROR "shared peripheral driver missing")
endif()
set(CMAKE_CURRENT_SOURCE_DIR "@ROOT@/userspace/hw-test")
include("@ROOT@/userspace/hw-test/CMakeLists.txt")
if(application_sources MATCHES "src/(middleware|driver)/")
    message(FATAL_ERROR "local middleware or driver implementation still compiled")
endif()
if(NOT application_sources MATCHES "TARGET_OBJECTS:uai-driver-time-override")
    message(FATAL_ERROR "kernel HAL time override missing")
endif()
'''.replace("@ROOT@", ROOT.as_posix())
        subprocess.run(["cmake", "-P", "/dev/stdin"], input=script, text=True, check=True)
        definition = (APP / "CMakeLists.txt").read_text()
        self.assertIn("uai::middleware uai::drivers", definition)
        self.assertNotIn("--wrap=", definition)

    def test_link_audit_requires_strong_kernel_driver_objects(self):
        checker = load_module("hwtest_check_link", APP / "check_link.py")
        expected = dict(checker.EXPECTED)
        expected["_ZN3uai2ai5dma2d11Dma2dDriver8TransferE"] = "dma2d_driver.cpp"
        expected["_ZN3uai2ai10peripheral16PeripheralDriver4CopyE"] = "peripheral_driver.cpp"
        expected["_ZN3uai2ai3nor9NorDriver4ReadE"] = "nor_driver.cpp"
        expected["_ZN3uai2ai3lcd16ReadDisplayStateEv"] = "lcd_registers.cpp"
        expected["_ZN3uai2ai2ui6Canvas8FillRectE"] = "canvas.cpp"
        symbols = []
        placements = []
        for index, (symbol, source) in enumerate(expected.items()):
            address = 0x34000400 + index * 32
            symbols.append(f"{address:08x} T {symbol}")
            origin = f"CMakeFiles/hw-test.elf.dir/{source}.obj"
            if source in ("hal_time.c", "stm32n6570_discovery_xspi.c", "dma2d_driver.cpp", "canvas.cpp", "peripheral_driver.cpp", "nor_driver.cpp", "lcd_registers.cpp"):
                origin = f"kernel/libuai.a({source}.obj)"
            placements.append(f".text.{symbol} 0x{address:x} 0x20 {origin}")
        checker.audit("\n".join(symbols), "\n".join(placements))
        with self.assertRaises(ValueError):
            checker.audit("\n".join(symbols).replace(" T _ZN3uai2ai5dma2d", " W _ZN3uai2ai5dma2d"),
                "\n".join(placements))
        with self.assertRaises(ValueError):
            checker.audit("\n".join(symbols), "\n".join(placements).replace(
                "kernel/libuai.a(peripheral_driver.cpp.obj)", "userspace/liblocal.a(peripheral_driver.cpp.obj)"))
        with self.assertRaises(ValueError):
            checker.audit("\n".join(symbols), "\n".join(placements).replace(
                "kernel/libuai.a(stm32n6570_discovery_xspi.c.obj)", "vendor/libbsp.a(stm32n6570_discovery_xspi.c.obj)"))


if __name__ == "__main__":
    unittest.main()
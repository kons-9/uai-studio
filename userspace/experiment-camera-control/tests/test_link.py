import pathlib
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import check_link


class LinkTests(unittest.TestCase):
    def test_make_uses_camera_ioc_with_host_config_template(self):
        root = pathlib.Path(__file__).resolve().parents[3]
        result = subprocess.run([
            "make", "-n", "-C", str(root / "userspace/experiment-camera-control"),
            "generate", "configure", f"CONFIG_FILE={root / 'build-system/host-config/local.mk.example'}",
        ], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(f'CUBEMX_IOC="{root}/userspace/experiment-camera-lcd/config/stm32n6570-dk-fullsecure.ioc"', result.stdout)
        self.assertIn(f'CUBEMX_OUTPUT_DIR="{root}/build-experiment-camera-control/cubemx"', result.stdout)
        self.assertIn('-DAPP_TARGET="experiment-camera-control"', result.stdout)
        self.assertIn('-DEXPERIMENT_PREKERNEL_READY=ON', result.stdout)

    def test_camera_control_cli_defaults(self):
        root = pathlib.Path(__file__).resolve().parents[3]
        with tempfile.TemporaryDirectory() as temporary:
            script = pathlib.Path(temporary) / "cli.cmake"
            script.write_text(
                "cmake_minimum_required(VERSION 3.16)\n"
                f'set(CMAKE_SOURCE_DIR "{root.as_posix()}")\n'
                'set(CMAKE_BINARY_DIR "${CMAKE_CURRENT_LIST_DIR}/build")\n'
                'set(APP_TARGET "experiment-camera-control")\n'
                'set(UAI_KERNEL_APPS ai-app mini-ai-app)\n'
                'foreach(name CUBEMX_IOC CUBEMX_OUTPUT_DIR STM32_RAM_IMAGE STM32_RAM_ADDRESS STM32_RAM_ENTRY STM32_RAM_STACK STM32_RAM_XPSR)\n'
                '  set(ENV{${name}} "")\n'
                'endforeach()\n'
                f'include("{root.as_posix()}/build-system/cmake/camera_board.cmake")\n'
                f'include("{root.as_posix()}/build-system/cmake/stm32_cli.cmake")\n'
                'if(NOT CUBEMX_IOC STREQUAL "${CMAKE_SOURCE_DIR}/userspace/experiment-camera-lcd/config/stm32n6570-dk-fullsecure.ioc")\n'
                '  message(FATAL_ERROR "wrong CubeMX input")\n'
                'endif()\n'
                'if(NOT CUBEMX_OUTPUT_DIR STREQUAL "${CMAKE_BINARY_DIR}/cubemx")\n'
                '  message(FATAL_ERROR "CubeMX output is not isolated")\n'
                'endif()\n'
                'if(NOT STM32_RAM_IMAGE STREQUAL "${CMAKE_BINARY_DIR}/userspace/experiment-camera-control/experiment-camera-control.bin")\n'
                '  message(FATAL_ERROR "wrong RAM image")\n'
                'endif()\n'
                'if(NOT STM32_RAM_ADDRESS STREQUAL "0x34000400" OR NOT STM32_RAM_ENTRY STREQUAL "0x34000800" OR NOT STM32_RAM_STACK STREQUAL "0x34200000")\n'
                '  message(FATAL_ERROR "RAM launch settings do not match camera-pipe2")\n'
                'endif()\n', encoding="utf-8")
            result = subprocess.run(["cmake", "-P", str(script)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_camera_board_profiles_preserve_app_identity(self):
        profile = pathlib.Path(__file__).resolve().parents[3] / "build-system/cmake/camera_board.cmake"
        cases = {
            "experiment-camera-lcd": "experiment-camera-lcd",
            "experiment-camera-pipe2": "experiment-camera-pipe2",
            "experiment-camera-lcd-touch": "experiment-camera-lcd-touch",
            "experiment-camera-control": "experiment-camera-pipe2",
            "ai-app": "",
            "experiment-hw-test": "",
            "experiment-gpu": "",
        }
        with tempfile.TemporaryDirectory() as temporary:
            script = pathlib.Path(temporary) / "profile.cmake"
            script.write_text(
                "cmake_minimum_required(VERSION 3.16)\n"
                f'include("{profile.as_posix()}")\n'
                'if(NOT UAI_CAMERA_BOARD_APP STREQUAL EXPECTED_PROFILE)\n'
                '  message(FATAL_ERROR "wrong camera board profile")\n'
                'endif()\n'
                'if(NOT APP_TARGET STREQUAL EXPECTED_APP)\n'
                '  message(FATAL_ERROR "application identity changed")\n'
                'endif()\n', encoding="utf-8")
            for target, expected in cases.items():
                with self.subTest(target=target):
                    result = subprocess.run([
                        "cmake", f"-DAPP_TARGET={target}", f"-DEXPECTED_APP={target}",
                        f"-DEXPECTED_PROFILE={expected}", "-P", str(script),
                    ], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def setUp(self):
        self.symbols = "\n".join(f"{0x34001000 + index * 32:08x} T {symbol}"
                                 for index, symbol in enumerate(check_link.EXPECTED))
        self.mapping = "\n".join(f" .text.{symbol}\n 0x{0x34001000 + index * 32:x} 0x20 build/{source}.obj"
                                 for index, (symbol, source) in enumerate(check_link.EXPECTED.items()))

    def test_expected_and_discarded_sections(self):
        discarded = " .text.HAL_GetTick 0x00000000 0x10 libhal.a(time.o)\n"
        check_link.audit(self.symbols, discarded + self.mapping)

    def test_weak_wrong_source_missing(self):
        with self.assertRaisesRegex(ValueError, "strong"):
            check_link.audit(self.symbols.replace(" T HAL_GetTick", " W HAL_GetTick"), self.mapping)
        with self.assertRaisesRegex(ValueError, "origin"):
            check_link.audit(self.symbols, self.mapping.replace("build/hal_time.c.obj", "libhal.a(time.o)"))
        with self.assertRaises(ValueError):
            check_link.audit(self.symbols, "")


if __name__ == "__main__":
    unittest.main()
#!/usr/bin/env python3

from datetime import datetime
import os
from pathlib import Path
import shlex
import subprocess
import sys


BASE_TARGETS = (
    "help", "setup", "init", "configure", "generate", "cubemx-generate",
    "build", "monitor", "ram-run", "ram-load", "sign", "program", "flash",
    "run", "clean",
)
ALIASES = {
    "init": "setup", "cubemx-generate": "generate", "ram-load": "ram-run",
    "run": "ram-run", "flash": "program", "ai-init": "ai-load",
}


class ApplicationTasks:
    def __init__(self, environment=None):
        self.environment = dict(os.environ if environment is None else environment)
        self.root = Path(self.value("PROJECT_ROOT", str(Path(__file__).resolve().parents[2])))
        self.sample = self.path("SAMPLE_DIR")
        self.build_dir = self.path("BUILD_DIR")
        self.host = self.path("HOST_APP_DIR")
        self.app = self.value("APP_TARGET", self.sample.name)
        self.completed = set()
        self.verify_directory = self.path(
            "AI_VERIFY_DIR", str(self.build_dir / "verify" / datetime.now().strftime("%Y%m%d-%H%M%S")))

    def value(self, name, default=""):
        return self.environment.get(name) or default

    def enabled(self, name):
        return self.value(name).lower() in ("1", "on", "true", "yes")

    def path(self, name, default=""):
        path = Path(self.value(name, default))
        return path if path.is_absolute() else self.root / path

    def tool(self, name, default=""):
        tool = self.value(name, default)
        return str(self.root / tool) if "/" in tool and not Path(tool).is_absolute() else tool

    def run(self, command, environment=None, cwd=None, capture=False, check=True):
        command = [str(argument) for argument in command]
        print(shlex.join(command), flush=True)
        return subprocess.run(
            command, cwd=cwd or self.root,
            env=environment or self.environment, check=check, text=True,
            stdout=subprocess.PIPE if capture else None,
        )

    def cmake_build(self, target):
        self.run([self.tool("CMAKE", "cmake"), "--build", self.build_dir, "--target", target])

    def execute(self, target):
        available = self.value("UAI_TARGETS").split()
        if available and target not in available:
            raise ValueError(f"Target is not enabled for {self.app}: {target}")
        target = ALIASES.get(target, target)
        if target in self.completed:
            return
        if target.startswith("ai-model-"):
            self.generate_model(target.removeprefix("ai-model-"))
        elif target in ("ram-run", "sign", "program"):
            self.execute("build")
            self.cmake_build("stm32-sign" if target == "sign" else target)
        else:
            handler = getattr(self, target.replace("-", "_"), None)
            if not callable(handler):
                raise ValueError(f"Unknown target: {target}")
            handler()
        self.completed.add(target)

    def help(self):
        makefile = self.value("SAMPLE_MAKEFILE", str(self.sample / "Makefile"))
        targets = self.value("UAI_TARGETS").split() or [*BASE_TARGETS, *self.value("APP_TARGETS").split()]
        for target in targets:
            print(f"make -f {shlex.quote(makefile)} {target}")
        print(f"\nHost settings: {self.value('CONFIG_FILE')}")

    def generate(self):
        ioc = self.path("CUBEMX_IOC")
        if not ioc.is_file():
            raise ValueError(f"CubeMX IOC file not found: {ioc}")
        output_dir = self.path("CUBEMX_OUTPUT_DIR")
        settings_file = output_dir / ".uai-settings"
        stamp = output_dir / ".uai-generated"
        script = self.root / "project-tools/scripts/cubemx-generate.sh"
        outputs = [self.root / output for output in shlex.split(self.value("CUBEMX_OUTPUTS"))]
        if not outputs:
            raise ValueError("CUBEMX_OUTPUTS must declare the required generated files")
        settings = "\n".join(self.value(name) for name in
                             ("CUBEMX_EXECUTABLE", "CUBEMX_IOC", "CUBEMX_OUTPUT_DIR")) + "\n"
        inputs = [ioc, script, Path(__file__)]
        inputs.extend(self.root / path for path in self.value("UAI_BUILD_INPUTS").split())
        unchanged = settings_file.is_file() and settings_file.read_text() == settings
        current = stamp.is_file() and all(output.is_file() for output in outputs)
        if unchanged and current:
            oldest_output = min(path.stat().st_mtime_ns for path in (stamp, *outputs))
            if all(not path.exists() or path.stat().st_mtime_ns <= oldest_output for path in inputs):
                return
        output_dir.mkdir(parents=True, exist_ok=True)
        stamp.unlink(missing_ok=True)
        temporary = settings_file.with_name(settings_file.name + ".tmp")
        temporary.write_text(settings)
        temporary.replace(settings_file)
        self.run(["sh", script])
        missing = [str(output) for output in outputs if not output.is_file()]
        if missing:
            raise ValueError("CubeMX output is missing: " + ", ".join(missing))
        for output in (*outputs, stamp):
            output.touch()

    def configure(self):
        self.execute("generate")
        if self.value("UI_LAYOUT_JSON"):
            self.execute("ui-layout-check")
        self.run([self.tool("CMAKE", "cmake"), *shlex.split(self.value("CMAKE_ARGS"))])

    def build(self):
        self.execute("configure")
        self.cmake_build(self.app)

    def setup(self):
        if self.enabled("ENABLE_AI"):
            self.execute("ai-models")
        self.execute("configure")

    def clean(self):
        self.cmake_build("clean")

    def monitor(self):
        self.run(["sh", self.root / "project-tools/scripts/uart-monitor.sh"])

    def generate_model(self, name):
        environment = dict(self.environment)
        environment["PATH"] = self.value("STEDGEAI_BIN") + os.pathsep + environment.get("PATH", "")
        self.run(["sh", self.path("AI_MODEL_GENERATOR"), name], environment=environment)

    def ai_models(self):
        for name in self.value("AI_MODEL_NAMES").split():
            self.execute(f"ai-model-{name}")

    def ai_build(self):
        self.execute("ai-models")
        self.execute("build")

    def programmer_connection(self):
        connection = "port=" + self.value("STM32_PROGRAM_PORT", "swd")
        if self.value("STM32_PROGRAM_SERIAL"):
            connection += " sn=" + self.value("STM32_PROGRAM_SERIAL")
        return connection

    def program_images(self, setting):
        loader = self.path("STM32_EXTERNAL_LOADER")
        if not self.value("STM32_EXTERNAL_LOADER") or not loader.is_file():
            raise ValueError(f"STM32_EXTERNAL_LOADER is missing: {loader}")
        for image in shlex.split(self.value(setting)):
            image = self.root / image
            if not image.is_file():
                raise ValueError(f"AI image is missing: {image}")
            self.run([
                self.tool("STM32_PROGRAMMER_CLI"), "-c", self.programmer_connection(),
                "-el", loader, "-w", image, "-v",
                *shlex.split(self.value("STM32_PROGRAM_EXTRA_ARGS")),
            ])

    def ai_load_weights(self):
        self.execute("build")
        self.program_images("AI_WEIGHT_IMAGES")

    def ai_load_blobs(self):
        self.execute("ai-load-weights")
        self.program_images("AI_BLOB_IMAGES")

    def ai_load(self):
        self.execute("ai-load-blobs")

    def ai_run(self):
        self.execute("ai-load")
        self.execute("ram-run")

    def dump_monitor(self, prefix, label, start_symbol, end_symbol):
        self.execute("build")
        elf = self.path("THREAD_MONITOR_ELF")
        result = self.run([self.tool("THREAD_MONITOR_NM", "arm-none-eabi-nm"), "-n", elf], capture=True)
        symbols = {}
        for line in result.stdout.splitlines():
            fields = line.split()
            if len(fields) == 3 and fields[2] in (start_symbol, end_symbol):
                symbols[fields[2]] = int(fields[0], 16)
        if start_symbol not in symbols or end_symbol not in symbols:
            raise ValueError(f"{label} linker symbols were not found")
        size = symbols[end_symbol] - symbols[start_symbol]
        if size <= 0:
            raise ValueError(f"{label} region size is invalid")
        output = self.path(prefix + "_DUMP")
        output.parent.mkdir(parents=True, exist_ok=True)
        connection = self.value("THREAD_MONITOR_CONNECTION", self.programmer_connection() + " mode=HOTPLUG")
        programmer = [self.tool("STM32_PROGRAMMER_CLI"), "-c", connection]
        try:
            self.run([*programmer, "-halt", "-u", hex(symbols[start_symbol]), str(size), output])
        finally:
            self.run([*programmer, "-run"], capture=True, check=False)

    def thread_monitor_dump(self):
        self.dump_monitor("THREAD_MONITOR", "ThreadMonitor", "__sample_ai_thread_monitor_start__", "__sample_ai_thread_monitor_end__")

    def cpu_task_monitor_dump(self):
        self.dump_monitor("CPU_TASK_MONITOR", "CPU task monitor", "__sample_ai_cpu_task_monitor_start__", "__sample_ai_cpu_task_monitor_end__")

    def analysis_environment(self):
        environment = dict(self.environment)
        environment["MPLCONFIGDIR"] = str(self.build_dir / "matplotlib")
        if self.value("THREAD_MONITOR_LD_PRELOAD"):
            environment["LD_PRELOAD"] = self.value("THREAD_MONITOR_LD_PRELOAD")
        return environment

    def thread_monitor(self):
        self.execute("thread-monitor-dump")
        output = self.path("THREAD_MONITOR_PNG")
        output.parent.mkdir(parents=True, exist_ok=True)
        json_output = self.path("THREAD_MONITOR_JSON")
        json_output.parent.mkdir(parents=True, exist_ok=True)
        frequency = self.value("THREAD_MONITOR_CPU_HZ", "600000000")
        if self.value("THREAD_MONITOR_FORMAT", "kernel") == "kernel":
            command = [self.tool("THREAD_MONITOR_UV", "uv"), "run", "--project", self.host,
                       "python", self.host / "ai_model_monitor/ai_model_monitor.py", "all",
                       self.path("THREAD_MONITOR_DUMP"), "--json", json_output, "--png", output,
                       "--cpu-hz", frequency]
        elif self.value("THREAD_MONITOR_FORMAT") == "experiment":
            python = self.tool("THREAD_MONITOR_PYTHON", "python3")
            self.run([python, self.sample / "tool/decode_thread_monitor.py", self.path("THREAD_MONITOR_DUMP"), "--output", json_output])
            self.run([python, self.sample / "tool/analyze_npu_trace.py", json_output, "--cpu-hz", frequency])
            command = [self.tool("THREAD_MONITOR_UV", "uv"), "run", "--project", self.sample / "tool",
                       "python", self.sample / "tool/visualize_thread_monitor.py", json_output,
                       "--output", output, "--cpu-hz", frequency]
        else:
            raise ValueError("THREAD_MONITOR_FORMAT must be kernel or experiment")
        self.run(command, environment=self.analysis_environment())

    def cpu_task_monitor(self):
        self.execute("cpu-task-monitor-dump")
        for setting in ("CPU_TASK_MONITOR_PNG", "CPU_TASK_MONITOR_JSON", "CPU_TASK_MONITOR_CSV"):
            self.path(setting).parent.mkdir(parents=True, exist_ok=True)
        environment = self.analysis_environment()
        environment["UV_CACHE_DIR"] = str(self.build_dir / "uv-cache")
        self.run([
            self.tool("THREAD_MONITOR_UV", "uv"), "run", "--project", self.host,
            "python", self.host / "cpu_task_monitor/cpu_task_monitor.py", self.path("CPU_TASK_MONITOR_DUMP"),
            "-o", self.path("CPU_TASK_MONITOR_PNG"), "--json", self.path("CPU_TASK_MONITOR_JSON"),
            "--csv", self.path("CPU_TASK_MONITOR_CSV"), "--cpu-hz", self.value("THREAD_MONITOR_CPU_HZ", "600000000"),
        ], environment=environment)

    def ui_command(self, arguments):
        command = [self.tool("UI_DESIGNER_PYTHON", "python3"), self.host / "ui_designer", *arguments,
                   "--layout", self.path("UI_LAYOUT_JSON")]
        if self.value("UI_FEATURE_CATALOG"):
            command.extend(["--feature-catalog", self.path("UI_FEATURE_CATALOG")])
        self.run(command)

    def ui_layout(self):
        self.ui_command(["generate", "--output", self.path("UI_LAYOUT_HEADER"),
                         *shlex.split(self.value("UI_LAYOUT_FORMAT_ARGS"))])

    def ui_layout_check(self):
        self.ui_command(["validate", "--check-font"])
        self.ui_command(["generate", "--check", "--output", self.path("UI_LAYOUT_HEADER"),
                         *shlex.split(self.value("UI_LAYOUT_FORMAT_ARGS"))])

    def ui_designer(self):
        self.ui_command(["serve", "--port", self.value("UI_DESIGNER_PORT", "8765")])

    def test(self):
        host_build = self.path("HOST_BUILD_DIR", str(self.root / "build" / (self.app + "-host")))
        command = [self.tool("CMAKE", "cmake"), "-S", self.sample, "-B", host_build]
        if self.value("MODEL_PYTHON", "python3") != "python3":
            command.append("-DPython3_EXECUTABLE=" + self.tool("MODEL_PYTHON"))
        self.run(command)
        self.run([self.tool("CMAKE", "cmake"), "--build", host_build, "--parallel", "2"])
        self.run([self.tool("CTEST", "ctest"), "--test-dir", host_build, "--output-on-failure"])

    def ui_tool(self):
        self.run([self.tool("UI_TOOL_PYTHON", "python3"), "-m", self.value("UI_TOOL_MODULE"),
                  "serve", "--port", self.value("UI_TOOL_PORT", "8767"), "--no-browser"], cwd=self.sample)

    def model_fixture_directory(self):
        return self.path("MODEL_LOAD_FIXTURE_DIR", str(self.build_dir / "userspace" / self.app / "model-load-fixture"))

    def model_npu_enabled(self):
        return self.enabled(self.value("MODEL_NPU_SETTING"))

    def model_payload_directory(self):
        if self.model_npu_enabled():
            payload = self.build_dir / "userspace" / self.app / "model-package"
            if self.enabled(self.value("MODEL_MULTI_SETTING")):
                payload /= self.value("MODEL_NAME", "person")
        else:
            payload = self.model_fixture_directory()
        return self.path("MODEL_LOAD_PAYLOAD_DIR", str(payload))

    def model_fixture(self):
        self.run([self.tool("MODEL_PYTHON", "python3"), self.sample / "tool/fixture.py",
                  "--output-dir", self.model_fixture_directory()])

    def model_generate(self):
        if not self.value("MODEL_SOURCE"):
            raise ValueError("Set MODEL_SOURCE to the model to generate")
        stedgeai = str(Path(self.value("STEDGEAI_BIN")) / "stedgeai") if self.value("STEDGEAI_BIN") else "stedgeai"
        command = [self.tool("MODEL_PYTHON", "python3"), self.sample / "tool/model.py", "generate",
                   "--model", self.path("MODEL_SOURCE"), "--stedgeai", self.tool("MODEL_STEDGEAI", stedgeai),
                   "--runtime-version", self.value("MODEL_RUNTIME_VERSION", "1201")]
        if self.value("MODEL_DESCRIPTOR"):
            command.extend(["--name", self.value("MODEL_NAME", "person"), "--descriptor", self.path("MODEL_DESCRIPTOR")])
        else:
            command.extend(["--kind", self.value("MODEL_KIND", "1"), "--input-bytes", self.value("MODEL_INPUT_BYTES"),
                            "--output-bytes", self.value("MODEL_OUTPUT_BYTES")])
        if self.value("MODEL_SEMANTICS"):
            command.extend(["--semantics", self.path("MODEL_SEMANTICS")])
        self.run(command)

    def model_package(self):
        if not self.model_npu_enabled():
            raise ValueError(f"model-package requires {self.value('MODEL_NPU_SETTING')}=ON")
        self.execute("build")

    def model_upload(self):
        custom_manifest = self.value(self.value("MODEL_MANIFEST_SETTING"))
        if custom_manifest and (not self.value("MODEL_WEIGHTS") or not self.value("MODEL_BLOB")):
            raise ValueError("Custom manifests require MODEL_WEIGHTS and MODEL_BLOB")
        if self.value("UART_DEVICE", "auto") == "auto":
            raise ValueError("Set UART_DEVICE to the board's serial device")
        if not custom_manifest:
            self.execute("model-package" if self.model_npu_enabled() else "model-fixture")
        payload = self.model_payload_directory()
        command = [self.tool("MODEL_PYTHON", "python3"), self.sample / "tool/upload.py",
                   "--uart", self.value("UART_DEVICE"),
                   "--manifest", custom_manifest or str(self.path("MODEL_MANIFEST", str(payload / "manifest.bin"))),
                   "--weights", self.path("MODEL_WEIGHTS", str(payload / "weights.bin")),
                   "--blob", self.path("MODEL_BLOB", str(payload / "blob.bin")),
                   "--baud", self.value("UART_BAUD", "115200"), "--timeout", self.value("MODEL_UPLOAD_TIMEOUT", "1200"),
                   "--startup-timeout", self.value("MODEL_STARTUP_TIMEOUT", "60"), "--monitor-after-upload"]
        if self.enabled("MODEL_WAIT_READY"):
            command.append("--wait-ready")
        running_npu = self.model_npu_enabled() and self.enabled("MODEL_RUN")
        if self.model_npu_enabled():
            if not self.value("STM32_PROGRAMMER_CLI"):
                raise ValueError("STM32_PROGRAMMER_CLI is required for direct PSRAM loading")
            command.extend(["--direct-programmer", self.tool("STM32_PROGRAMMER_CLI")])
            if self.value("STM32_PROGRAM_SERIAL"):
                command.extend(["--stlink-serial", self.value("STM32_PROGRAM_SERIAL")])
        if running_npu:
            command.append("--run")
        for setting, option in (("MODEL_INPUT", "--input"), ("MODEL_IMAGE", "--image"),
                                ("MODEL_RESULT_DIR", "--result-dir"), ("MODEL_POLICY", "--policy")):
            if self.value(setting):
                command.extend([option, self.path(setting)])
        if self.enabled("MODEL_INPUT_ZEROS"):
            command.append("--input-zeros")
        if running_npu and self.value("MODEL_EXPECTED_OUTPUT_CRC"):
            command.extend(["--expected-output-crc", self.value("MODEL_EXPECTED_OUTPUT_CRC")])
        self.run(command)

    def verify(self, mode):
        command = [self.tool("AI_VERIFY_PYTHON", "python3"), self.path("VERIFY_TOOL"), mode]
        if mode == "analyze":
            if not self.value("AI_VERIFY_LOG"):
                raise ValueError("Set AI_VERIFY_LOG to a saved UART log")
            command.extend([self.path("AI_VERIFY_LOG"), "--output", self.verify_directory / "statistics.json"])
        else:
            command.extend(["--device", self.value("UART_DEVICE", "auto"), "--baud", self.value("UART_BAUD", "115200"),
                            "--log", self.verify_directory / (mode + ".log"),
                            "--output", self.verify_directory / (mode + ".json")])
            if mode == "endurance":
                command.extend(["--seconds", self.value("AI_VERIFY_SECONDS", "1800"),
                                "--step-seconds", self.value("AI_VERIFY_STEP_SECONDS", "10")])
        self.run(command)

    def verify_exposure(self):
        self.verify("exposure")

    def verify_endurance(self):
        self.verify("endurance")

    def verify_stats(self):
        self.verify("analyze")


def main(arguments=None):
    tasks = ApplicationTasks()
    try:
        for target in arguments or sys.argv[1:] or ["help"]:
            tasks.execute(target)
    except subprocess.CalledProcessError as error:
        return error.returncode
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
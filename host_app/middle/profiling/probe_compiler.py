"""
μAI-Studio Probe Compiler - Compiles C probe functions to PIC binaries

Takes a C source file containing a probe function, compiles it to
position-independent code (PIC) using arm-none-eabi-gcc, extracts
the .text section as raw binary, and optionally deploys via MCP.

Usage:
    python probe_compiler.py compile my_probe.c -o my_probe.bin
    python probe_compiler.py deploy my_probe.c --tcp 192.168.1.100:5005
    python probe_compiler.py list-templates
"""

import argparse
import os
import subprocess
import sys
import tempfile
import struct
from typing import Optional

RESET = "\033[0m"
BOLD = "\033[1m"
DIM = "\033[2m"
RED = "\033[31m"
GREEN = "\033[32m"
YELLOW = "\033[33m"
CYAN = "\033[36m"

# Default cross-compiler
DEFAULT_CC = "arm-none-eabi-gcc"
DEFAULT_OBJCOPY = "arm-none-eabi-objcopy"
DEFAULT_OBJDUMP = "arm-none-eabi-objdump"

# Compiler flags for position-independent Thumb code
PIC_CFLAGS = [
    "-mcpu=cortex-m4",
    "-mthumb",
    "-mfloat-abi=soft",
    "-fPIC",
    "-fno-exceptions",
    "-fno-rtti",
    "-ffunction-sections",
    "-fdata-sections",
    "-Os",
    "-nostdlib",
    "-ffreestanding",
    "-std=c11",
]

# Linker script for PIC probe (position-independent, .text only)
PROBE_LINKER_SCRIPT = """\
SECTIONS
{
    .text 0x00000000 : {
        *(.text*)
        *(.rodata*)
    }
    /DISCARD/ : {
        *(.comment)
        *(.ARM.attributes)
        *(.ARM.exidx*)
    }
}
"""

# ----------------------------------------------------------------
# Probe templates
# ----------------------------------------------------------------

PROBE_TEMPLATES = {
    "timer": {
        "description": "Measure function execution time",
        "source": """\
/* Probe: measure execution time of a target function.
 * The target function address is passed via probe_arena metadata.
 * DWT->CYCCNT is used for cycle-accurate timing on Cortex-M.
 */
#include <stdint.h>

/* DWT registers (Cortex-M) */
#define DWT_CYCCNT  (*(volatile uint32_t *)0xE0001004)
#define DWT_CONTROL (*(volatile uint32_t *)0xE0001000)

/* Result structure placed at the start of the probe */
typedef struct {
    uint32_t cycles;
    uint32_t call_count;
} ProbeResult;

static ProbeResult result __attribute__((section(".data")));

typedef void (*TargetFn)(void);

void probe_entry(void)
{
    DWT_CONTROL |= 1;  /* enable cycle counter */
    uint32_t start = DWT_CYCCNT;

    /* Target function pointer — set by the loader */
    volatile TargetFn target = (TargetFn)0;
    if (target) {
        target();
    }

    uint32_t end = DWT_CYCCNT;
    result.cycles = end - start;
    result.call_count++;
}
""",
    },
    "nop": {
        "description": "No-op probe for testing",
        "source": """\
/* Probe: NOP — used to verify OTA loading works */
void probe_entry(void)
{
    __asm volatile("nop");
}
""",
    },
    "read_reg": {
        "description": "Read a hardware register",
        "source": """\
/* Probe: read a memory-mapped register.
 * Address is configurable.
 */
#include <stdint.h>

static volatile uint32_t read_value;

void probe_entry(void)
{
    /* Example: read SysTick->VAL */
    volatile uint32_t *reg = (volatile uint32_t *)0xE000E018;
    read_value = *reg;
}
""",
    },
    "stack_check": {
        "description": "Check stack watermark of a task",
        "source": """\
/* Probe: walk the stack looking for untouched fill pattern.
 * Stack bottom and size set by loader metadata.
 */
#include <stdint.h>

static uint32_t stack_free_bytes;

void probe_entry(void)
{
    /* Stack filled with 0xDEADBEEF by RTOS at creation */
    volatile uint32_t *bottom = (volatile uint32_t *)0x20001000;
    uint32_t depth = 256; /* words */
    uint32_t free = 0;

    for (uint32_t i = 0; i < depth; i++) {
        if (bottom[i] == 0xDEADBEEF) {
            free++;
        } else {
            break;
        }
    }
    stack_free_bytes = free * 4;
}
""",
    },
}


# ----------------------------------------------------------------
# Compiler
# ----------------------------------------------------------------

class ProbeCompiler:
    """Compiles C probe source to position-independent binary."""

    def __init__(self, cc: str = DEFAULT_CC,
                 objcopy: str = DEFAULT_OBJCOPY,
                 objdump: str = DEFAULT_OBJDUMP):
        self.cc = cc
        self.objcopy = objcopy
        self.objdump = objdump

    def check_toolchain(self) -> bool:
        """Verify that the cross-compiler is available."""
        try:
            result = subprocess.run(
                [self.cc, "--version"],
                capture_output=True, text=True, timeout=10)
            return result.returncode == 0
        except (FileNotFoundError, subprocess.TimeoutExpired):
            return False

    def compile(self, source_path: str, output_path: str,
                extra_flags: Optional[list] = None) -> bool:
        """Compile C source to position-independent binary.

        Steps:
          1. Compile to object file (.o) with PIC flags
          2. Link with custom linker script (text-only, position-independent)
          3. Extract .text section as raw binary via objcopy
        """
        if not os.path.exists(source_path):
            print(f"{RED}Source file not found: {source_path}{RESET}")
            return False

        with tempfile.TemporaryDirectory() as tmpdir:
            obj_path = os.path.join(tmpdir, "probe.o")
            elf_path = os.path.join(tmpdir, "probe.elf")
            ld_path = os.path.join(tmpdir, "probe.ld")

            # Write linker script
            with open(ld_path, "w") as f:
                f.write(PROBE_LINKER_SCRIPT)

            # Step 1: Compile to object file
            flags = PIC_CFLAGS + (extra_flags or [])
            cmd_compile = [self.cc] + flags + ["-c", source_path, "-o", obj_path]

            print(f"{DIM}[1/3] Compiling: {' '.join(cmd_compile)}{RESET}")
            result = subprocess.run(cmd_compile, capture_output=True, text=True)
            if result.returncode != 0:
                print(f"{RED}Compilation failed:{RESET}")
                print(result.stderr)
                return False

            # Step 2: Link with PIC linker script
            cmd_link = [
                self.cc,
                "-mcpu=cortex-m4", "-mthumb", "-nostdlib",
                "-T", ld_path,
                "-Wl,--gc-sections",
                obj_path, "-o", elf_path,
            ]

            print(f"{DIM}[2/3] Linking: {' '.join(cmd_link)}{RESET}")
            result = subprocess.run(cmd_link, capture_output=True, text=True)
            if result.returncode != 0:
                print(f"{RED}Linking failed:{RESET}")
                print(result.stderr)
                return False

            # Step 3: Extract raw binary
            cmd_objcopy = [
                self.objcopy,
                "-O", "binary",
                "-j", ".text",
                elf_path, output_path,
            ]

            print(f"{DIM}[3/3] Extracting binary: {' '.join(cmd_objcopy)}{RESET}")
            result = subprocess.run(cmd_objcopy, capture_output=True, text=True)
            if result.returncode != 0:
                print(f"{RED}Binary extraction failed:{RESET}")
                print(result.stderr)
                return False

        # Validate output
        if not os.path.exists(output_path):
            print(f"{RED}Output file not created{RESET}")
            return False

        size = os.path.getsize(output_path)
        if size == 0:
            print(f"{RED}Output binary is empty{RESET}")
            return False

        print(f"{GREEN}✓ Probe compiled: {output_path} ({size} bytes){RESET}")
        return True

    def compile_source(self, source_code: str, output_path: str) -> bool:
        """Compile from source string."""
        with tempfile.NamedTemporaryFile(
                suffix=".c", mode="w", delete=False) as f:
            f.write(source_code)
            src_path = f.name

        try:
            return self.compile(src_path, output_path)
        finally:
            os.unlink(src_path)

    def disassemble(self, binary_path: str) -> Optional[str]:
        """Disassemble a probe binary for verification."""
        with tempfile.TemporaryDirectory() as tmpdir:
            elf_path = os.path.join(tmpdir, "probe.elf")

            # Convert binary back to ELF for disassembly
            cmd = [
                self.objcopy,
                "-I", "binary",
                "-O", "elf32-littlearm",
                "-B", "arm",
                "--rename-section", ".data=.text,alloc,load,readonly,code",
                binary_path, elf_path,
            ]
            result = subprocess.run(cmd, capture_output=True, text=True)
            if result.returncode != 0:
                return None

            cmd = [self.objdump, "-d", "-m", "arm", elf_path]
            result = subprocess.run(cmd, capture_output=True, text=True)
            if result.returncode == 0:
                return result.stdout
            return None


# ----------------------------------------------------------------
# CLI commands
# ----------------------------------------------------------------

def cmd_compile(args):
    """Compile a probe source file."""
    compiler = ProbeCompiler(cc=args.cc, objcopy=args.objcopy)
    if not compiler.check_toolchain():
        print(f"{YELLOW}Warning: Cross-compiler not found ({args.cc}).{RESET}")
        print(f"{YELLOW}Install arm-none-eabi-gcc or specify --cc.{RESET}")
        return 1

    output = args.output or os.path.splitext(args.source)[0] + ".bin"
    ok = compiler.compile(args.source, output)
    return 0 if ok else 1


def cmd_compile_template(args):
    """Compile a built-in probe template."""
    if args.template not in PROBE_TEMPLATES:
        print(f"{RED}Unknown template: {args.template}{RESET}")
        print(f"Available: {', '.join(PROBE_TEMPLATES.keys())}")
        return 1

    compiler = ProbeCompiler(cc=args.cc, objcopy=args.objcopy)
    if not compiler.check_toolchain():
        print(f"{YELLOW}Warning: Cross-compiler not found ({args.cc}).{RESET}")
        return 1

    template = PROBE_TEMPLATES[args.template]
    output = args.output or f"{args.template}_probe.bin"
    ok = compiler.compile_source(template["source"], output)
    return 0 if ok else 1


def cmd_list_templates(args):
    """List available probe templates."""
    print(f"{BOLD}Available Probe Templates{RESET}")
    print(f"{'─' * 50}")
    for name, info in PROBE_TEMPLATES.items():
        print(f"  {GREEN}{name:<16s}{RESET} {info['description']}")
    return 0


def cmd_deploy(args):
    """Compile and deploy a probe via MCP."""
    from mcp_client import McpClient

    compiler = ProbeCompiler(cc=args.cc, objcopy=args.objcopy)
    if not compiler.check_toolchain():
        print(f"{YELLOW}Warning: Cross-compiler not found.{RESET}")
        return 1

    # Compile
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
        bin_path = f.name

    try:
        ok = compiler.compile(args.source, bin_path)
        if not ok:
            return 1

        # Read binary
        with open(bin_path, "rb") as f:
            binary = f.read()

        # Connect and deploy
        if args.tcp:
            parts = args.tcp.split(":")
            host, port = parts[0], int(parts[1]) if len(parts) > 1 else 5005
            client = McpClient.tcp(host, port)
        elif args.demo:
            client = McpClient.demo()
        else:
            print(f"{RED}Specify --tcp or --demo.{RESET}")
            return 1

        client.connect()
        probe_name = args.name or os.path.splitext(
            os.path.basename(args.source))[0]
        result = client.deploy_probe(probe_name, binary)
        print(f"{GREEN}{result}{RESET}")
        client.close()

    finally:
        if os.path.exists(bin_path):
            os.unlink(bin_path)

    return 0


def cmd_disasm(args):
    """Disassemble a probe binary."""
    compiler = ProbeCompiler(objdump=args.objdump)
    text = compiler.disassemble(args.binary)
    if text:
        print(text)
    else:
        print(f"{RED}Disassembly failed.{RESET}")
    return 0


# ----------------------------------------------------------------
# Main
# ----------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="μAI-Studio Probe Compiler")
    parser.add_argument("--cc", default=DEFAULT_CC,
                        help="Cross-compiler path")
    parser.add_argument("--objcopy", default=DEFAULT_OBJCOPY,
                        help="objcopy path")
    parser.add_argument("--objdump", default=DEFAULT_OBJDUMP,
                        help="objdump path")

    sub = parser.add_subparsers(dest="command")

    # compile
    p_compile = sub.add_parser("compile", help="Compile a probe source")
    p_compile.add_argument("source", help="C source file")
    p_compile.add_argument("-o", "--output", help="Output binary path")

    # template
    p_template = sub.add_parser("template",
                                help="Compile a built-in template")
    p_template.add_argument("template", help="Template name")
    p_template.add_argument("-o", "--output", help="Output binary path")

    # list-templates
    sub.add_parser("list-templates", help="List probe templates")

    # deploy
    p_deploy = sub.add_parser("deploy", help="Compile and deploy")
    p_deploy.add_argument("source", help="C source file")
    p_deploy.add_argument("--name", help="Probe name")
    p_deploy.add_argument("--tcp", help="MCP server TCP endpoint")
    p_deploy.add_argument("--demo", action="store_true")

    # disasm
    p_disasm = sub.add_parser("disasm", help="Disassemble a probe binary")
    p_disasm.add_argument("binary", help="Binary file to disassemble")

    args = parser.parse_args()

    if args.command == "compile":
        sys.exit(cmd_compile(args))
    elif args.command == "template":
        sys.exit(cmd_compile_template(args))
    elif args.command == "list-templates":
        sys.exit(cmd_list_templates(args))
    elif args.command == "deploy":
        sys.exit(cmd_deploy(args))
    elif args.command == "disasm":
        sys.exit(cmd_disasm(args))
    else:
        parser.print_help()


if __name__ == "__main__":
    main()

import argparse
import pathlib
import re
from layout import BLOB_ADDRESS, BLOB_CAPACITY


def generate(names, header, linker):
    if not names or len(set(names)) != len(names) or any(not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", name) for name in names):
        raise ValueError("model names must be unique C identifiers beginning with a letter")
    declarations = ["#pragma once", "#include <cstddef>", "#include <cstdint>", 'extern "C" {']
    for name in names:
        declarations += [
            f"bool experiment_{name}_start(std::uint8_t *, std::uint32_t, std::uint8_t **, std::uint32_t);",
            f"int experiment_{name}_poll();", f"bool experiment_{name}_deinit();"]
    declarations += ["}", "namespace model_loader {", "struct ModelApi {", "const char *name;",
                     "bool (*start)(std::uint8_t *, std::uint32_t, std::uint8_t **, std::uint32_t);",
                     "int (*poll)();", "bool (*deinit)();", "};", "inline constexpr ModelApi registry[] = {"]
    declarations += [f'{{"{name}", experiment_{name}_start, experiment_{name}_poll, experiment_{name}_deinit}},' for name in names]
    declarations += ["};", f"inline constexpr std::size_t registry_count = {len(names)};", "}"]
    sections = ["SECTIONS", "{", f"  OVERLAY {BLOB_ADDRESS:#x} :", "  {"]
    sections += [f"    .model_blob_{name} {{ KEEP(*(.model_blob_{name})) }}" for name in names]
    sections += ["  } > MODEL_BLOB", "}", "INSERT AFTER .experiment_blob;"]
    sections += [f'ASSERT(SIZEOF(.model_blob_{name}) <= {BLOB_CAPACITY:#x}, "{name} blob overflow")' for name in names]
    header.write_text("\n".join(declarations) + "\n")
    linker.write_text("\n".join(sections) + "\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--models", nargs="+", required=True)
    parser.add_argument("--header", type=pathlib.Path, required=True)
    parser.add_argument("--linker", type=pathlib.Path, required=True)
    arguments = parser.parse_args()
    try:
        generate(arguments.models, arguments.header, arguments.linker)
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
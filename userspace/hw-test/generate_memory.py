import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from host_app.auto_static_memory_layout.common import (
    generate_memory_regions,
    generate_static_sections,
    normalize_layout,
    read_document,
    write_if_changed,
)
from host_app.auto_static_memory_layout.emitters import (
    generate_memory_config,
    write_key_header,
    write_raw_header,
)


def generate(input_path, output_dir):
    document = read_document(input_path)
    layout = normalize_layout(document)
    generated = output_dir / "middleware" / "memory" / "generated"
    write_key_header(generated / "static_memory_layout" / "key.hpp", document)
    write_raw_header(generated / "static_memory_layout" / "raw.hpp", document)
    write_if_changed(generated / "memory_config.hpp", generate_memory_config(document))
    linker = "MEMORY\n{\n" + generate_memory_regions(layout) + "\n}\n\nSECTIONS\n{\n"
    linker += generate_static_sections(layout) + "\n}\nINSERT AFTER .bss;\n"
    write_if_changed(output_dir / "memory-contract.ld", linker)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=pathlib.Path, required=True)
    parser.add_argument("--output-dir", type=pathlib.Path, required=True)
    arguments = parser.parse_args()
    generate(arguments.input, arguments.output_dir)


if __name__ == "__main__":
    main()
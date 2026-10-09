import pathlib
import sys

from manifest import decode


def generate(header):
    model = decode(header)
    if not (model.weights_address == 0x91010000 and model.blob_address == 0x91020000 and
            model.weights_bytes <= 0x10000 and model.blob_bytes <= 0x10000):
        raise ValueError("manifest must use separate 64 KiB weights/blob slots at 0x91010000/0x91020000")
    return "#pragma once\ninline constexpr unsigned char expected_header[] = {" + ",".join(map(str, header)) + "};\n"


if __name__ == "__main__":
    try:
        output = pathlib.Path(sys.argv[2])
        contents = generate(pathlib.Path(sys.argv[1]).read_bytes())
        if not output.exists() or output.read_text() != contents:
            output.write_text(contents)
    except (ValueError, OSError) as error:
        sys.exit(str(error))

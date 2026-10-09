import pathlib
import sys

from manifest import decode
from layout import BLOB_ADDRESS, BLOB_CAPACITY, WEIGHTS_ADDRESS, WEIGHTS_CAPACITY


def generate(header):
    model = decode(header)
    if not (model.weights_address == WEIGHTS_ADDRESS and model.blob_address == BLOB_ADDRESS and
            model.weights_bytes <= WEIGHTS_CAPACITY and model.blob_bytes <= BLOB_CAPACITY):
        raise ValueError("manifest exceeds the reserved weights/blob slots")
    return "#pragma once\ninline constexpr unsigned char expected_header[] = {" + ",".join(map(str, header)) + "};\n"


if __name__ == "__main__":
    try:
        output = pathlib.Path(sys.argv[2])
        contents = generate(pathlib.Path(sys.argv[1]).read_bytes())
        if not output.exists() or output.read_text() != contents:
            output.write_text(contents)
    except (ValueError, OSError) as error:
        sys.exit(str(error))

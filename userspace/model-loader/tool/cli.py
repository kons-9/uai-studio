import argparse
import pathlib
import sys


def main(argv=None):
    arguments = list(sys.argv[1:] if argv is None else argv)
    parser = argparse.ArgumentParser(prog="model-loader")
    parser.add_argument("command", choices=("contract", "upload", "result"))
    if not arguments or arguments[0] in ("-h", "--help"):
        parser.print_help()
        return 0
    selected = parser.parse_args(arguments[:1]).command
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
    if selected == "contract":
        from contract import main as command
        return command(arguments[1:])
    if selected == "upload":
        from upload import main as command
        return command(arguments[1:])
    import json
    import zlib
    from contract import verify_package
    from manifest import decode
    from results import validate_policy
    from tensor_io import save_result
    result = argparse.ArgumentParser(prog="model-loader result")
    result.add_argument("--package", type=pathlib.Path, required=True)
    result.add_argument("--input", type=pathlib.Path, required=True)
    result.add_argument("--output", type=pathlib.Path, required=True)
    result.add_argument("--policy", type=pathlib.Path)
    result.add_argument("--image-metadata", type=pathlib.Path)
    settings = result.parse_args(arguments[1:])
    try:
        document = verify_package(settings.package / "manifest.bin")
        if document is None:
            raise ValueError("result interpretation requires a v3 package")
        header = (settings.package / "manifest.bin").read_bytes()
        data = settings.input.read_bytes()
        policy = validate_policy(json.loads(settings.policy.read_text())) if settings.policy else None
        image_metadata = json.loads(settings.image_metadata.read_text()) if settings.image_metadata else None
        save_result(settings.output, header, decode(header), data, zlib.crc32(data), image_metadata, document, policy)
        print(settings.output / "decoded.json")
        return 0
    except (OSError, ValueError, KeyError, ImportError) as error:
        result.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
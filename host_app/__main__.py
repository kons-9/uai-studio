"""Start the unified GUI or run a host tool's CLI."""

from pathlib import Path
import sys

if __package__:
    from .cli import main
else:
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from host_app.cli import main


if __name__ == "__main__":
    raise SystemExit(main())
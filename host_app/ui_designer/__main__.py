#!/usr/bin/env python3
"""Command-line entry point for the UI designer package."""

from pathlib import Path
import sys

if __package__:
    from .cli import main
else:
    # ``python host_app/ui_designer`` executes this file without a package
    # context. Add host_app so the package can still import itself.
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from ui_designer.cli import main


if __name__ == "__main__":
    raise SystemExit(main())

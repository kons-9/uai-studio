#!/usr/bin/env python3
"""Command-line entry point for the UI designer package."""

from pathlib import Path
import sys

if __package__:
    from .cli import main
else:
    # ``python host_app/ui_designer`` executes this file without a package
    # context. Import through the same package as the shared host GUI.
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    from host_app.ui_designer.cli import main


if __name__ == "__main__":
    raise SystemExit(main())

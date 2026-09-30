#!/usr/bin/env python3
"""Command-line entry point for the memory-layout generator package."""

from pathlib import Path
import sys

if __package__:
    from .cli import main
else:
    # ``python host_app/auto_static_memory_layout`` executes this file without a
    # package context. Add ``tools`` so the package can still import itself.
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from auto_static_memory_layout.cli import main


if __name__ == "__main__":
    raise SystemExit(main())

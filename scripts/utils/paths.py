"""Where the repository is, for the scripts that read or write files in it."""

from pathlib import Path

# scripts/utils/paths.py -> the repository root.
ROOT = Path(__file__).resolve().parents[2]

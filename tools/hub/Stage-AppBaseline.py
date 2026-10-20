"""Retired legacy baseline copier; use explicit dependency-only selection."""
import runpy
from pathlib import Path
if __name__ == "__main__":
    runpy.run_path(str(Path(__file__).with_name("Stage-Dependencies.py")), run_name="__main__")

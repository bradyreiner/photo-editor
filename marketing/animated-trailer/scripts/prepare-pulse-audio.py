"""Prepare the user-selected Sun in Your Eyes score and action-timed foley."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).with_name("prepare-pulse-sun-audio.py")),run_name="__main__")

import subprocess
import sys


def run_zephyr(items, parallel=8):
    """Process items through this repo's zephyr_tool.py batch processor."""
    safe_parallel = min(parallel, 4)
    cmd = [sys.executable, "zephyr_tool.py", "--parallel", str(safe_parallel), *items]
    result = subprocess.run(cmd, capture_output=True, text=True, check=True)
    return result.stdout.strip().splitlines()

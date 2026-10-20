"""Check help text and exit status together (CTest's output regex ignores exit status)."""

import subprocess
import sys

result = subprocess.run(sys.argv[1:], capture_output=True, text=True, timeout=10)
print(result.stdout, end="")
print(result.stderr, end="", file=sys.stderr)
raise SystemExit(0 if result.returncode == 0 and "Usage:" in result.stdout else 1)

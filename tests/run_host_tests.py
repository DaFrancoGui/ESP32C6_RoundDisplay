"""Exercise the production touch transform without an ESP32 or IDF installation."""
import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cc", default="cc", help='C compiler command, e.g. "gcc" or "zig cc"')
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix="round-display-tests-") as work:
    executable = Path(work) / "coordinates.exe"
    command = shlex.split(args.cc) + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
        "-I" + str(root / "components/chsc6x_touch/include"),
        str(root / "tests/test_coordinates.c"), "-o", str(executable)]
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], check=True)

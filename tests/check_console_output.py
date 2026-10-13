#!/usr/bin/env python3
"""Run the preprocessor many times with stdout redirected and check that the console output
is always complete.

Found by fuzz testing: before main() flushed std::cout explicitly, a few percent of runs on
Windows lost all console output even though every output file was written correctly.

Usage: check_console_output.py --exe <ParametricPreprocessor> --idf <input.idf> [--runs N]
"""

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--idf", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=200)
    args = parser.parse_args()

    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        shutil.copy(args.idf, tmp)
        for i in range(args.runs):
            proc = subprocess.run([str(args.exe.resolve()), args.idf.name], cwd=tmp, capture_output=True, timeout=60)
            lines = proc.stdout.decode("latin-1").splitlines()
            complete = (
                proc.returncode == 0
                and lines[:1] == ["ParametricPreprocessor Started."]
                and lines[-1:] != []
                and lines[-1].startswith("ParametricPreprocessor Finished")
            )
            if not complete:
                failures += 1
                print(f"run {i + 1}: exit code {proc.returncode}, {len(proc.stdout)} bytes of console output")
    print(f"{args.runs - failures} of {args.runs} runs had complete console output")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

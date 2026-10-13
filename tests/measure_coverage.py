#!/usr/bin/env python3
"""Measure line coverage of the test inputs for both the Fortran original and the C++ port.

Builds both programs with gcov instrumentation (needs gfortran, g++ and gcov on PATH), runs
every case in tests/inputs, tests/inputs_cpp_only and resources/ (normal and verbose modes),
and prints the line coverage of each source file. Lines that were never executed are
written to <build dir>/uncovered_<source>.txt. PORTING_NOTES.md explains why the remaining
uncovered lines cannot be reached.

usage: measure_coverage.py [--build-dir build/coverage]
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
INPUT_DIRS = [REPO / "tests" / "inputs", REPO / "tests" / "inputs_cpp_only", REPO / "resources"]
# (verbose, stdin). In verbose mode the Fortran waits for a key at the end: an empty line
# makes its READ fail before its final debug PRINT, "x" lets it reach that PRINT.
MODES = [(False, b"x\n"), (True, b"x\n"), (True, b"\n")]


def build(build_dir: Path) -> dict:
    fortran_dir, cpp_dir = build_dir / "fortran", build_dir / "cpp"
    for d in (fortran_dir, cpp_dir):
        if d.exists():
            shutil.rmtree(d)
        d.mkdir(parents=True)
    shutil.copy(REPO / "resources" / "ParametricPreprocessor.f90", fortran_dir)
    for src in (REPO / "src" / "ParametricPreprocessor").glob("*.[ch][ch]"):
        shutil.copy(src, cpp_dir)
    subprocess.run(["gfortran", "-O0", "--coverage", "-ffree-line-length-none", "-o", "pp.exe", "ParametricPreprocessor.f90"],
                   cwd=fortran_dir, check=True)
    subprocess.run(["g++", "-std=c++17", "-O0", "--coverage", "-o", "pp.exe", "main.cc", "ParametricPreprocessor.cc"],
                   cwd=cpp_dir, check=True)
    return {"Fortran": (fortran_dir, ["ParametricPreprocessor.f90"]),
            "C++": (cpp_dir, ["ParametricPreprocessor.cc", "main.cc"])}


def set_up_case(case: Path, work: Path) -> list:
    """Copy the case into work and return the command line arguments (see compare_outputs.py)."""
    if case.suffix == ".idf":
        shutil.copy(case, work)
        return [case.name]
    args = []
    for line in case.read_text().splitlines():
        words = line.split()
        if words and words[0] == "copy":
            (work / words[2]).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(case.parent / words[1], work / words[2])
        elif words and words[0] == "run":
            args = words[1:]
    return args


def summarize(build_dir: Path, label: str, directory: Path, sources: list) -> None:
    subprocess.run(["gcov"] + sources, cwd=directory, capture_output=True, check=True)
    for src in sources:
        executable = 0
        missed = []
        for line in (directory / (src + ".gcov")).read_text(errors="replace").splitlines():
            m = re.match(r"\s*([^:]+):\s*(\d+):(.*)", line)
            if not m or m.group(1).strip() == "-":
                continue
            executable += 1
            if m.group(1).strip().startswith(("#####", "=====")):
                missed.append(f"{m.group(2)}: {m.group(3).strip()}")
        executed = executable - len(missed)
        report = build_dir / f"uncovered_{src}.txt"
        report.write_text("\n".join(missed) + "\n")
        print(f"{label:8} {src:28} {executed:5}/{executable:<5} lines ({100.0 * executed / max(executable, 1):5.1f}%)"
              f"  uncovered lines: {report}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", type=Path, default=REPO / "build" / "coverage")
    args = parser.parse_args()
    build_dir = args.build_dir.resolve()
    programs = build(build_dir)
    cases = sorted(p for d in INPUT_DIRS for pattern in ("*.idf", "*.args") for p in d.glob(pattern))
    for label, (directory, sources) in programs.items():
        with tempfile.TemporaryDirectory() as tmp:
            for case in cases:
                for n, (verbose, stdin) in enumerate(MODES):
                    work = Path(tmp, f"{case.parent.name}-{case.stem}-{n}")
                    work.mkdir()
                    case_args = set_up_case(case, work)
                    env = {k: v for k, v in os.environ.items() if k != "DeveloperFlag"}
                    if verbose:
                        env["DeveloperFlag"] = "Y"
                    subprocess.run([str(directory / "pp.exe")] + case_args, cwd=work, env=env, input=stdin,
                                   capture_output=True, timeout=120)
        summarize(build_dir, label, directory, sources)
    print(f"{len(cases)} cases, each run {len(MODES)} times")
    return 0


if __name__ == "__main__":
    sys.exit(main())

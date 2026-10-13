#!/usr/bin/env python3
"""Regression tests for the C++ ParametricPreprocessor.

Each input IDF is copied into a fresh directory, the preprocessor is run on it, and every
file it produces (output IDFs, the .err file and parametric.int) is compared with the
expected results. Console output is compared too, with the timing line masked.

A case can also be a <name>.args file for testing the command line. Each line is either
  copy <file in the inputs directory> <destination relative to the work directory>
  run [arguments...]

Two modes:
  compare against stored expected results (default, used by CTest):
      compare_outputs.py --exe <ParametricPreprocessor> --inputs <dir> --expected <dir>
  compare against the original Fortran executable (differential testing):
      compare_outputs.py --exe <ParametricPreprocessor> --inputs <dir> --reference-exe <fortran exe>
  regenerate stored expected results from the Fortran executable:
      compare_outputs.py --inputs <dir> --reference-exe <fortran exe> --expected <dir> --update
"""

import argparse
import difflib
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

TIME_LINE = re.compile(r"(Time:)\s*[-0-9.]+")
STDOUT_NAME = "stdout.txt"


def normalize(data: bytes) -> str:
    """Line endings differ by platform (Fortran and C++ text mode both use CRLF on Windows)."""
    return data.decode("latin-1").replace("\r\n", "\n")


def run_case(exe: Path, case: Path, work: Path) -> dict:
    """Run exe for one case inside work and return {relative file name: normalized text}."""
    work.mkdir(parents=True)
    inputs = set()
    if case.suffix == ".args":
        args = []
        for line in case.read_text().splitlines():
            words = line.split()
            if words and words[0] == "copy":
                dest = work / words[2]
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy(case.parent / words[1], dest)
                inputs.add(dest)
            elif words and words[0] == "run":
                args = words[1:]
    else:
        shutil.copy(case, work / case.name)
        inputs.add(work / case.name)
        args = [case.name]
    proc = subprocess.run([str(exe)] + args, cwd=work, capture_output=True, timeout=120)
    results = {}
    for path in sorted(work.rglob("*")):
        if path.is_file() and path not in inputs:
            results[path.relative_to(work).as_posix()] = normalize(path.read_bytes())
    results[STDOUT_NAME] = TIME_LINE.sub(r"\1 <time>", normalize(proc.stdout))
    return results


def load_expected(expected_dir: Path) -> dict:
    return {
        p.relative_to(expected_dir).as_posix(): normalize(p.read_bytes())
        for p in sorted(expected_dir.rglob("*"))
        if p.is_file()
    }


def diff(expected: dict, actual: dict, label: str) -> list:
    problems = []
    for name in sorted(set(expected) | set(actual)):
        if name not in actual:
            problems.append(f"{label}: missing file {name}")
        elif name not in expected:
            problems.append(f"{label}: unexpected file {name}")
        elif expected[name] != actual[name]:
            lines = difflib.unified_diff(
                expected[name].splitlines(), actual[name].splitlines(), "expected/" + name, "actual/" + name, lineterm="", n=2
            )
            problems.append(f"{label}: {name} differs\n" + "\n".join(list(lines)[:40]))
    return problems


def main() -> int:
    sys.stdout.reconfigure(errors="backslashreplace")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, help="C++ ParametricPreprocessor executable under test")
    parser.add_argument("--inputs", type=Path, required=True, nargs="+", help="directories containing input IDF files")
    parser.add_argument("--expected", type=Path, help="directory of stored expected results")
    parser.add_argument("--reference-exe", type=Path, help="original Fortran ParametricPreprocessor executable")
    parser.add_argument("--update", action="store_true", help="regenerate --expected from --reference-exe")
    args = parser.parse_args()

    idfs = sorted(p for d in args.inputs for pattern in ("*.idf", "*.args") for p in d.glob(pattern))
    if not idfs:
        print("no input files found")
        return 1

    problems = []
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        for idf in idfs:
            if args.update:
                ref = run_case(args.reference_exe.resolve(), idf, tmp_path / "ref" / idf.stem)
                out_dir = args.expected / idf.stem
                if out_dir.exists():
                    shutil.rmtree(out_dir)
                out_dir.mkdir(parents=True)
                for name, text in ref.items():
                    (out_dir / name).parent.mkdir(parents=True, exist_ok=True)
                    (out_dir / name).write_text(text, encoding="latin-1", newline="\n")
                print(f"updated {idf.stem}")
                continue
            actual = run_case(args.exe.resolve(), idf, tmp_path / "cpp" / idf.stem)
            if args.reference_exe:
                expected = run_case(args.reference_exe.resolve(), idf, tmp_path / "ref" / idf.stem)
            else:
                expected = load_expected(args.expected / idf.stem)
            case_problems = diff(expected, actual, idf.name)
            print(f"{'FAIL' if case_problems else 'ok  '} {idf.name}")
            problems.extend(case_problems)

    for problem in problems:
        print(problem)
    if not args.update:
        print(f"{len(idfs) - len({p.split(':')[0] for p in problems})} of {len(idfs)} inputs match")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Check that every file the preprocessor writes for the test inputs is a valid EnergyPlus model.

For each input (.idf, or .args command-line case, see compare_outputs.py) the preprocessor is
run, then EnergyPlus simulates every case file it wrote. A case passes when EnergyPlus prints
"EnergyPlus Completed Successfully" and its .err file has no Severe or Fatal errors.
Warnings are reported but allowed.

The test inputs simulate the design days only, so no weather file is needed. Inputs that
are expected to fail in the preprocessor simply produce no case files.

usage: check_energyplus.py --exe <ParametricPreprocessor> --energyplus <energyplus executable>
                           --inputs <dir> [<dir> ...] [--jobs N]
"""

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


def set_up_case(case: Path, work: Path) -> tuple:
    """Copy the case into work; return (command line arguments, input files)."""
    if case.suffix == ".idf":
        shutil.copy(case, work)
        return [case.name], {work / case.name}
    args, inputs = [], set()
    for line in case.read_text().splitlines():
        words = line.split()
        if words and words[0] == "copy":
            dest = work / words[2]
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(case.parent / words[1], dest)
            inputs.add(dest)
        elif words and words[0] == "run":
            args = words[1:]
    return args, inputs


def simulate(energyplus: Path, idf: Path) -> tuple:
    """Run EnergyPlus on idf; return (passed, number of warnings, problem description)."""
    out_dir = idf.parent / (idf.stem + "_eplus")
    proc = subprocess.run([str(energyplus), "-d", str(out_dir), str(idf)], capture_output=True, timeout=1800)
    err_file = out_dir / "eplusout.err"
    err = err_file.read_text(errors="replace") if err_file.exists() else ""
    warnings = len(re.findall(r"\*\* Warning \*\*", err))
    severe = re.findall(r"\*\* +Severe +\*\*.*|\*\* +Fatal +\*\*.*", err)
    completed = "EnergyPlus Completed Successfully" in err
    if completed and not severe and proc.returncode == 0:
        return True, warnings, ""
    problem = "; ".join(s.strip() for s in severe[:3]) or f"exit code {proc.returncode}, not completed"
    return False, warnings, problem


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, required=True, help="ParametricPreprocessor executable")
    parser.add_argument("--energyplus", type=Path, required=True, help="EnergyPlus executable (energyplus.exe)")
    parser.add_argument("--inputs", type=Path, required=True, nargs="+")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()

    cases = sorted(p for d in args.inputs for pattern in ("*.idf", "*.args") for p in d.glob(pattern))
    failures = simulated = 0
    with tempfile.TemporaryDirectory() as tmp:
        to_simulate = []
        for case in cases:
            work = Path(tmp, f"{case.parent.name}-{case.stem}")
            work.mkdir()
            case_args, inputs = set_up_case(case, work)
            subprocess.run([str(args.exe.resolve())] + case_args, cwd=work, capture_output=True, timeout=300)
            written = sorted(p for p in work.rglob("*.idf") if p not in inputs)
            if not written:
                print(f"--   {case.name}: no case files written")
            to_simulate += [(case, idf) for idf in written]
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            results = list(pool.map(lambda item: (item, simulate(args.energyplus, item[1])), to_simulate))
        for (case, idf), (passed, warnings, problem) in results:
            simulated += 1
            if passed:
                print(f"ok   {case.name} -> {idf.name}: {warnings} warnings")
            else:
                failures += 1
                print(f"FAIL {case.name} -> {idf.name}: {problem}")
    print(f"{simulated - failures} of {simulated} case files simulated without severe errors")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

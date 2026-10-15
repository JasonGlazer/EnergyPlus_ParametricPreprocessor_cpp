# AGENTS.md

Rules for working on this repository: a C++ port of the EnergyPlus `ParametricPreprocessor`.
The original Fortran is `resources/ParametricPreprocessor.f90`.

**The prime directive:** for any input where the Fortran program is well defined, the C++
program must produce **byte-for-byte identical** results:
- every output IDF
- `<input>.err`
- `parametric.int`
- the console output (only the elapsed-time value may differ)

"Close enough" is a failure. Read `PORTING_NOTES.md` before changing behavior.

## 1. Reproducing Fortran behavior

1. **The Fortran is the specification, including its bugs.**
   - Do not fix a quirk of the original, even an obvious bug: existing user input files depend on the current results.
   - Each preserved quirk has a `FORTRAN-COMPAT` comment in the code and an entry in `PORTING_NOTES.md`.
   - Fixing a quirk is a separate, explicit decision by the user.
2. **Never guess what the Fortran does. Run it.** Build the reference and compare outputs:
   ```bash
   gfortran -O0 -ffree-line-length-none -o ref.exe resources/ParametricPreprocessor.f90
   ```
   Also build a bounds-checked copy to detect undefined behavior:
   ```bash
   gfortran -g -fcheck=all -ffree-line-length-none -o ref_check.exe resources/ParametricPreprocessor.f90
   ```
3. **Fortran semantics that have caused bugs before.** Check all of these when touching related code:
   - **Blank padding.** Fortran strings are fixed length and blank padded, so trailing blanks never matter in comparisons. Use `lenTrim`, `rtrim`, `isStrEq`, `fortranCompare` and `sub` rather than raw `std::string` comparison.
   - **Substrings.** `s(a:b)` is 1-based and inclusive, and positions past the end read as blanks.
   - **1-based arrays.** The intermediate code mixes positive symbol references with negative token constants, so keep arrays 1-based (`Array1<T>`).
   - **Implicit `SAVE`.** A local variable with an initializer keeps its value between calls. State that persists across calls or cases lives in members (stacks, `m_indexObjectLines`, `m_indexFoundExpression`).
   - **Persistent stacks.** The evaluation stack and the structure stack are not reset between expressions or cases. Leftover values change results.
   - **Number formatting.** `F13.6` and `E18.6E3` behave as follows:
     - mantissas look like `0.dddddd`
     - exponents have three digits
     - `-0.000000E+000` keeps its sign
     - a value too wide for the field prints as asterisks
     - `NaN` and `Infinity` are spelled out
     - the small-number threshold `0.00001` is a *single-precision* literal
   - **`INT()` truncates through a 32-bit integer.** NaN and out-of-range values become `-2147483648`, and results between -1 and 0 are `+0`, never `-0`.
   - **`READ(... ,*)` number parsing.** Only the characters `-0123456789.E+` are accepted, so lowercase `e` and `D` exponents give 0.
   - **Output records are right-trimmed and tabs become spaces.** Error and warning text is cut to 300 characters when stored.
4. **Undefined behavior in the Fortran is the only allowed divergence.** Examples:
   - out-of-bounds access
   - an uninitialized variable
   - a runtime `READ` error

   Before you call something undefined behavior, prove it:
   - `ref_check.exe` reports a runtime error, **or**
   - the output changes between runs or between `-O0` and `-O3`, **or**
   - the code reads a variable that was never set.

   Then make the C++ behave sensibly and match the `-O0` Fortran output where one exists. Add a row to "Deliberate differences" in `PORTING_NOTES.md` and a test in `tests/inputs_cpp_only/`.
5. **Match platform-independent behavior, not a compiler's math library.** For example, `cos` of about 1e43 differs between MSVC and MinGW `libm`. Document such cases; do not emulate an inaccurate libm.
6. **Mirror the Fortran's structure.** Keep the same routine names, order and comments, so the two sources can be read side by side.

## 2. Testing rules

1. **Run the full suite before claiming anything works.** Run it on MSVC and on MinGW:
   ```bash
   cmake -S . -B build/msvc -G "Visual Studio 17 2022" -A x64
   ```
   ```bash
   cmake --build build/msvc --config Release
   ```
   ```bash
   ctest --test-dir build/msvc -C Release --output-on-failure
   ```
   ```bash
   cmake -S . -B build/mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DPARAMETRIC_DIFFERENTIAL_TESTS=ON
   ```
   ```bash
   cmake --build build/mingw
   ```
   ```bash
   ctest --test-dir build/mingw --output-on-failure
   ```
   The suites are:
   - `regression`: stored Fortran results
   - `regression_cpp_only`: the undefined-behavior cases
   - `console_output`: checks console output is never lost
   - `differential`: builds the Fortran and compares live, including `resources/`
   - `energyplus_simulation`: simulates every generated case file in EnergyPlus 26.2 (only when CMake finds EnergyPlus; set `ENERGYPLUS_EXE` if needed)

   Keep build directories short (`build/...`). MSBuild fails on paths longer than 260 characters.
2. **Report results faithfully.** If a test fails, say so and show the diff. Never weaken a comparison, mask extra output, or delete a failing case to get a green run.
3. **Expected results come from the Fortran, never from the C++.**
   - Regenerate `tests/expected/` only from the Fortran reference:
     ```bash
     python tests/compare_outputs.py --reference-exe ref.exe --inputs tests/inputs --expected tests/expected --update
     ```
   - `tests/expected_cpp_only/` is the only place where expected output comes from the C++. Generate it the same way with the C++ executable as `--reference-exe`.
   - Never hand-edit files in either `expected` directory.
4. **Every input in `tests/inputs/` must be well defined in the Fortran.**
   - Run each new input through `ref_check.exe`; it must exit 0 without a runtime error.
   - Also compare against a Fortran built at `-O3`; uninitialized variables only show up when optimized.
   - Anything else belongs in `tests/inputs_cpp_only/`, with a comment in the file explaining why.
5. **Test inputs are complete EnergyPlus 26.2 models, generated by `tests/make_inputs.py`.**
   - Never hand-edit the `.idf` files in `tests/inputs/` or `tests/inputs_cpp_only/`. Change the case in `make_inputs.py`, run it, then regenerate the expected results (rule 3).
   - Each case adds its Parametric objects and expressions to the base model, which comes from `ExampleFiles/1ZoneUncontrolled.idf` and simulates the design days only. If the block defines a unique object such as `Timestep` or `Building`, leave that object out of the base model.
   - Preserve what the case tests: the line layout a quirk depends on, and the order in which expressions are evaluated.
   - A result that is not a valid EnergyPlus value (`True`, `NaN`, asterisks...) goes in a `sink()`, an informational `Output:PreprocessorMessage`. Do not put it in an `Output:Variable` key: keys are regular expressions, so `*` or `+3` stop EnergyPlus.
   - Every case file the preprocessor writes must simulate without severe errors. Check with:
     ```bash
     python tests/check_energyplus.py --exe <ParametricPreprocessor> --energyplus C:/EnergyPlusV26-2-0/energyplus.exe --inputs tests/inputs tests/inputs_cpp_only
     ```
6. **Every bug gets a regression test that fails without the fix.** Prove it fails without the fix, for example by building the old code in a scratch directory. Name tests after what they cover (`fuzz_*`, `coverage_*`) and give each case a header that says why it exists.
7. **Keep coverage complete.** After changing code or tests, run:
   ```bash
   python tests/measure_coverage.py
   ```
   - Every reachable line must stay covered in **both** the Fortran and the C++.
   - A new uncovered line needs either a test or an entry in the unreachable-code table in `PORTING_NOTES.md` explaining why no input can reach it.
8. **Use random differential testing for anything touching the tokenizer, evaluator or number formatting.**
   - Generate random `Parametric:Logic` blocks and expressions.
   - Run them through the C++, the Fortran, and the bounds-checked Fortran.
   - Skip cases where the bounds-checked build fails.
   - Diagnose every mismatch to its root cause: a port bug, Fortran undefined behavior, a test-harness bug, or a libm difference.
   - Turn each real finding into a regression input.
9. **Suspect flakiness, and investigate it.**
   - A result that changes between identical runs means undefined behavior in the Fortran or a real bug in the C++.
   - Re-run in a loop and tally the outcomes before drawing conclusions.
   - An example found this way: console output was lost when stdout was redirected, so `main()` now flushes `std::cout` explicitly.
10. **Comparisons cover everything.** `tests/compare_outputs.py` compares:
   - every file the program writes, including files in subdirectories
   - the console output, with only the time masked

   Line endings are normalized because both programs write CRLF on Windows. Command-line cases are `.args` files (`copy <src> <dest>` and `run <args...>` lines).

## 3. Housekeeping

- Keep the C++ warning-free with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` (GCC) and `/W4` (MSVC).
- Update `PORTING_NOTES.md` in the same change as any behavior, test or coverage change: quirks, deliberate differences, verification and coverage numbers.
- Put scratch builds and temporary inputs outside the repository or under `build/`, which is git-ignored.
- Commit only when asked.

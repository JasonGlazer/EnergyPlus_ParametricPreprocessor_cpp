# Porting notes: Fortran to C++

`src/ParametricPreprocessor/` is a routine-for-routine port of
`resources/ParametricPreprocessor.f90`. The goal is **identical output**. For any input
where the Fortran program is well defined, the C++ program writes byte-for-byte the same:

- output IDF files
- `<input>.err` error file
- `parametric.int` intermediate file
- console messages

This is checked by the tests in `tests/` (see the README).

## Structure

| Fortran                                | C++                                                         |
|----------------------------------------|-------------------------------------------------------------|
| `PROGRAM` main sequence                | `main.cc` and `Preprocessor::run()`                         |
| module-level variables                 | members of `ParametricPreprocessor::Preprocessor`           |
| `SUBROUTINE Foo` / `FUNCTION Foo`      | `Preprocessor::foo()`, same order, same comments            |
| grow-by-doubling `ALLOCATABLE` arrays  | `Array1<T>`, a 1-based, bounds-checked `std::vector` wrapper |
| `SAVE` variables in `getNext...` routines | `m_indexObjectLines`, `m_indexFoundExpression`           |
| `CHARACTER(len=N)` blank-padded strings | `std::string`, kept right-trimmed. Fortran comparisons ignore trailing blanks, so helpers (`isStrEq`, `fortranCompare`, `sub`, `lenTrim`) reproduce those semantics. |
| `F13.6` / `E18.6E3` formatting in `RealToString` | `realToString()`, which emulates both edit descriptors, including `0.dddddd` mantissas, three-digit exponents, `NaN` and `Infinity` |

Arrays stay 1-based on purpose. The intermediate code mixes positive symbol-table
references with negative token constants, and the field and line-number arithmetic is
carried over unchanged.

## Deliberate differences

These cases are undefined behaviour in the Fortran: out-of-bounds array access, an
uninitialized variable, or a `READ` error that stops the program. The C++ behaves
predictably instead. Each one has a test in `tests/inputs_cpp_only/`.

| Situation | Fortran | C++ |
|---|---|---|
| The input file is missing, or no file is given | `numCases` is never initialized. At `-O0` it happens to be 0, and the C++ matches that output exactly. With optimization the `ALLOCATE` fails and the program crashes. | Reports the error and stops |
| `DISABLE` or `ENABLE` with no object name | Searches for an uninitialized name and prints whatever is in memory | Searches for an empty name |
| A `Parametric:Logic` line assigns an undeclared parameter (`$undefined = 4`) | Writes to `Symbol(0)`; sometimes crashes | Warning only, as before; the assignment is skipped |
| An unmatched `)` in an expression | Reads `opStack(0)`; result unpredictable | Error `Stack underflow.`; the rest of that parenthesis is ignored |
| A numeric-looking value that is malformed (e.g. `-` or `1E` in `SetValueForRun`) | List-directed `READ` aborts the program | Treated as `0` |
| Lines longer than 2000 characters, values longer than 200 | Silently truncated | Not truncated |
| More than 100 operators pending in one expression | Overflows `opStack` | Stack grows |
| Exit status | Always 0 | 0, or 1 if an internal error is caught (out-of-range access) |
| Elapsed time on the console | `CPU_TIME` | Wall-clock time (`std::chrono::steady_clock`) |
| `DeveloperFlag` debug output | Ends with a debug `PRINT` of internal arrays | That last debug `PRINT` is dropped; the other verbose messages are kept |

Stored error-message text and context are still cut to 300 characters, as the Fortran
`ErrMsgType` did.

## Quirks preserved on purpose (`FORTRAN-COMPAT` in the source)

These are bugs, or at least surprises, in the original. Fixing any of them would change
results for existing input files, so the port keeps them. Each is marked with a
`FORTRAN-COMPAT` comment and could be fixed as a follow-up.

### Expression evaluation
1. **`<>` evaluates as `>`.** `EvaluateExpression` handles `tokNE` with a greater-than comparison.
2. **`LEN(...)` is mapped to `INT`.** For a string argument the result is `0`, with a warning.
3. **`MOD(a~b)` is mapped to `LOG`.** It returns `LOG(b)`, and `a` is left on the evaluation stack.
4. **`ACOS(x)` returns `x`.** `tokFuncACOS` is missing from the operator list in `expressionToRPN`, so the function is dropped.
5. **Unary minus is ignored and swallows the next character.**
   - `- $X` evaluates as `$X`.
   - `-$X` (no space) is read as `-$` followed by an invalid function name `X`.
   - Negative numeric literals such as `-2.5` and `-12` work.
6. **A single-digit negative literal at the end of an expression is dropped.** For example, the `-2` in `=8*-2` is lost. A warning `Unknown type of operator` is reported, and the operator then uses whatever value is left on the evaluation stack.
7. **Only the top value decides numeric vs. string comparison.** `isEvalStackTopReal(2)` checks only the top entry.
8. **The evaluation stack is not cleared between expressions.** Leftover values (see 3 and 6) can feed later expressions.
9. **A string assignment does not clear the numeric flag.** Assigning a string result to a parameter keeps a previous `isRealNum = true`.
10. **`INT()` goes through a 32-bit integer.** NaN or out-of-range values become `-2147483648`, the x86 behaviour of the original. Values between −1 and 0 give `+0`, never `-0`, so the output is `0.000000E+000` rather than `-0.000000E+000`.
11. **The small-number threshold is single precision.** It is `0.00001` as a default real (`9.99999974737875E-06`), not double precision.
12. **Some negative results print as asterisks.** A result from −100000 to about −99999.9999995 does not fit the `F13.6` field (`-100000.000000` is 14 characters), so it is written as `*************`.

### Number parsing
13. **Lowercase `e` and Fortran `D` exponents parse as zero.**
    - `StringToReal` accepts only `-0123456789.E+`, so `1e5` and `1D3` used in arithmetic are `0`.
    - The original text is still output if no arithmetic is applied.
    - An empty `SetValueForRun` value counts as the number `0`.

### Reading the IDF file
14. **Spaces inside an embedded expression are copied to the output line.** `=$x * 0` becomes `    -0.000000E+000` with the leading spaces.
15. **An expression interrupted by `!` is not recognised.**
16. **A `;` inside a comment within a `Parametric:*` object adds a duplicate field.**
17. **An object whose last field is an expression is never registered.** For example `Shading:Overhang, B, W, =$h;` cannot be the target of `DISABLE` or `ENABLE`.

### DISABLE / ENABLE
18. **The first object in the file can never be disabled.** `getNextDisabledObjectLines` increments its index before the first test.
19. **Two adjacent disabled objects:** when the second starts on the line right after the first ends, the second one is still written.
20. **Object names must be quoted.** `DISABLE "Name"` works; `DISABLE Name` reads `Name` as an unknown function name.

### Cases and files
21. **The duplicate-suffix check skips only inactive *earlier* cases.** An inactive later case can still be reported as a duplicate.
22. **A `SetValueForRun` with a name and no values** fills every case with the parameter's own name.
23. **`parametric.int` is left in the input file's directory.**

## Verification performed

- **Example inputs.** All three files in `resources/`, plus 29 edge-case and coverage cases
  in `tests/inputs/`, produce identical files and console output from the C++ (MSVC 2022
  and MinGW g++ 8.3) and the Fortran (gfortran 8.3, at `-O0` and `-O3`).
- **No undefined behaviour in those comparisons.** The cases in `tests/inputs/` also pass
  the Fortran built with `-fcheck=all`.
- **Code coverage.** See the next section.
- **Random testing.** 300 randomly generated Parametric:Logic blocks and embedded
  expressions were compared against the Fortran. This found four problems, all now fixed:

  | Finding | Fix | Regression test |
  |---|---|---|
  | `INT` of NaN or an out-of-range value | Return `-2147483648` (item 10) | `tests/inputs/fuzz_int_conversion.idf` |
  | `INT` of a value between −1 and 0 printed `-0.000000E+000` | Convert through a 32-bit integer (item 10) | `tests/inputs/fuzz_int_conversion.idf` |
  | `F13.6` overflow to asterisks | Print 13 asterisks (item 12) | `tests/inputs/fuzz_fixed_format_overflow.idf` |
  | Console output occasionally missing | `main()` now flushes explicitly (see below) | `console_output` CTest |

  The last one was a C++ bug, not a Fortran quirk. When stdout was redirected, leaving
  the flush to process exit lost **all** console output in about 2–8% of runs on Windows,
  with both MSVC and MinGW builds. The output files were always correct.
  `tests/check_console_output.py` runs the program 200 times and fails if any run's
  output is incomplete. Without the fix it failed 8 of 200 runs.

  After the fixes, a final 300-case run had one remaining difference.
  - `COS(TAN(100) + EXP(100))`, the cosine of about 2.7×10⁴³, came out differently.
  - The MinGW C++ build matches the Fortran (both `0.645947`) because they share the
    MinGW math library.
  - The MSVC build gives `-0.042299`, which is the correctly rounded value.
  - The cause is how each compiler's math library handles huge trigonometric arguments,
    not the port. Results can differ between platforms the same way for the Fortran
    program itself.

## Code coverage

`tests/measure_coverage.py` builds both programs with gcov, runs every test case normally
and in verbose (`DeveloperFlag=Y`) mode, and reports line coverage. It needs `gfortran`,
`g++` and `gcov`.

| Source | Lines executed |
|---|---|
| `resources/ParametricPreprocessor.f90` (Fortran) | 1421 / 1443 (98.5%) |
| `ParametricPreprocessor.cc` (C++) | 1561 / 1606 (97.2%) |
| `main.cc` (C++) | 23 / 26 (88.5%) |

Every reachable line in both programs is executed by the tests. The `coverage_*` inputs in
`tests/inputs/` were added to reach these areas:

- Every error and warning message.
- Every misplaced `IF`/`SELECT` statement.
- `ENABLE`/`DISABLE` with an object type, with a duplicate name, and re-enabling an object.
- The remaining tokenizer and comparison branches.
- Files with no objects, no expressions, or no intermediate code.
- An input path containing a directory (`.args` cases).
- Every Fortran array growing past its initial size (`coverage_large_input.idf`, generated by `tests/make_large_input.py`).

The C++ has no equivalent of the Fortran array-growth code, but the same large input
checks that it gives the same results.

The lines that are still not executed cannot be reached by any input:

| Code (both versions unless noted) | Why it is unreachable |
|---|---|
| `tokUnNeg`, `ACOS`, `LEN` and `MOD` evaluation, and the `LEN`/`MOD`/`None` names in `opToken2String` | These tokens are never put into the intermediate code (quirks 2–5), and `tokNone` is never returned by the tokenizer |
| "SetParameterForRun must contain at least one value" | A `SetValueForRun` name always resolves to a symbol at or before the last `SetValueForRun` symbol |
| "Invalid parameter name" in `ReadSetValueObjects` | An invalid name already raised an error, and `ReadSetValueObjects` is skipped after any error |
| "error opening intermediate file - no file found" | The file was written by the first pass; only reachable if another process deletes it |
| "invalid symbol reference", "token not recognized" (`default`) | The intermediate code only contains valid symbol references and tokens |
| Fortran: tab replacement in `SubstituteValues` | Tabs were already replaced when the intermediate file was written |
| Fortran: empty-stack fallbacks in `PopStrucStack`, `CurrentStrucStackTop`, `DoesTopMatch`; `ReplaceTopStrucStack` match argument | The structure stack always has the base entry, and no caller passes a match |
| `IntToStr6` "Invalid" result | Needs a million cases or expressions |
| C++: "Operator stack underflow." in `popOpStack` | Defensive; the caller now checks for an empty stack first |
| C++: `catch` in `main` | Only reached on an internal error (out-of-range access) |
| C++: `default: break;` lines | Defensive defaults in `switch` statements |

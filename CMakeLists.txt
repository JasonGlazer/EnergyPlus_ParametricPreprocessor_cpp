cmake_minimum_required(VERSION 3.10...3.31)

project(ParametricPreprocessorStandalone LANGUAGES CXX)

add_subdirectory(src/ParametricPreprocessor)

include(CTest)

if(BUILD_TESTING)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  set(compare_script "${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_outputs.py")

  # results that match the original Fortran program
  add_test(NAME regression
           COMMAND "${Python3_EXECUTABLE}" "${compare_script}" --exe $<TARGET_FILE:ParametricPreprocessor>
                   --inputs "${CMAKE_CURRENT_SOURCE_DIR}/tests/inputs" --expected "${CMAKE_CURRENT_SOURCE_DIR}/tests/expected")

  # inputs where the Fortran program has undefined behavior
  add_test(NAME regression_cpp_only
           COMMAND "${Python3_EXECUTABLE}" "${compare_script}" --exe $<TARGET_FILE:ParametricPreprocessor>
                   --inputs "${CMAKE_CURRENT_SOURCE_DIR}/tests/inputs_cpp_only"
                   --expected "${CMAKE_CURRENT_SOURCE_DIR}/tests/expected_cpp_only")

  # console output must never be lost when stdout is redirected
  add_test(NAME console_output
           COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_console_output.py"
                   --exe $<TARGET_FILE:ParametricPreprocessor> --idf "${CMAKE_CURRENT_SOURCE_DIR}/tests/inputs/disable_objects.idf")

  # Every case file written for the test inputs must simulate in EnergyPlus without severe
  # errors. Runs when an EnergyPlus executable is found (set ENERGYPLUS_EXE to choose one).
  find_program(ENERGYPLUS_EXE NAMES energyplus PATHS C:/EnergyPlusV26-2-0 /usr/local/EnergyPlus-26-2-0
               /Applications/EnergyPlus-26-2-0 DOC "EnergyPlus executable used to check the generated case files")
  if(ENERGYPLUS_EXE)
    add_test(NAME energyplus_simulation
             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_energyplus.py"
                     --exe $<TARGET_FILE:ParametricPreprocessor> --energyplus "${ENERGYPLUS_EXE}"
                     --inputs "${CMAKE_CURRENT_SOURCE_DIR}/tests/inputs" "${CMAKE_CURRENT_SOURCE_DIR}/tests/inputs_cpp_only")
  endif()

  # Build the original Fortran program and compare outputs directly, including the
  # example files in resources/. Requires a Fortran compiler (e.g. gfortran).
  option(PARAMETRIC_DIFFERENTIAL_TESTS "Compare against the original Fortran ParametricPreprocessor" OFF)
  if(PARAMETRIC_DIFFERENTIAL_TESTS)
    enable_language(Fortran)
    add_executable(ParametricPreprocessorFortran resources/ParametricPreprocessor.f90)
    if(CMAKE_Fortran_COMPILER_ID STREQUAL "GNU")
      target_compile_options(ParametricPreprocessorFortran PRIVATE -ffree-line-length-none)
    endif()
    add_test(NAME differential
             COMMAND "${Python3_EXECUTABLE}" "${compare_script}" --exe $<TARGET_FILE:ParametricPreprocessor>
                     --reference-exe $<TARGET_FILE:ParametricPreprocessorFortran>
                     --inputs "${CMAKE_CURRENT_SOURCE_DIR}/tests/inputs" "${CMAKE_CURRENT_SOURCE_DIR}/resources")
  endif()
endif()

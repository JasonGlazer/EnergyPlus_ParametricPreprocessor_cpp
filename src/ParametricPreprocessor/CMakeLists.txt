# Replacement for the Fortran src/ParametricPreprocessor/CMakeLists.txt in EnergyPlus.
# Also builds standalone (see the top level CMakeLists.txt of this repository).

cmake_minimum_required(VERSION 3.10...3.31)

project(ParametricPreprocessor LANGUAGES CXX)

add_executable(ParametricPreprocessor main.cc ParametricPreprocessor.cc ParametricPreprocessor.hh)
target_compile_features(ParametricPreprocessor PRIVATE cxx_std_17)
set_target_properties(ParametricPreprocessor PROPERTIES FOLDER Auxiliary)
if(MSVC)
  target_compile_definitions(ParametricPreprocessor PRIVATE _CRT_SECURE_NO_WARNINGS) # for std::getenv
endif()

if(TARGET project_options)
  # building as part of EnergyPlus
  target_link_libraries(ParametricPreprocessor PRIVATE project_options project_warnings)
elseif(MSVC)
  target_compile_options(ParametricPreprocessor PRIVATE /W4)
else()
  target_compile_options(ParametricPreprocessor PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

install(TARGETS ParametricPreprocessor DESTINATION PreProcess/ParametricPreprocessor)

if(APPLE
   AND CPACK_CODESIGNING_DEVELOPER_ID_APPLICATION
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/CodeSigning.cmake")
  include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/CodeSigning.cmake")
  register_install_codesign_target(ParametricPreprocessor PreProcess/ParametricPreprocessor Unspecified)
endif()

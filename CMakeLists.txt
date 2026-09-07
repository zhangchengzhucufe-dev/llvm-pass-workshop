cmake_minimum_required(VERSION 3.20)
project(llvm-passes CXX)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION} from ${LLVM_DIR}")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# One pass plugin per directory, no cross-dependencies. Pick whichever
# one you care about -- building the whole tree works too.
foreach(D IN ITEMS
    01-hello-pass
    02-dataflow-liveness
    03-alias-analysis
    04-memoryssa
    06-dominator
  add_subdirectory(${D})
endforeach()


# `ninja check`: run the lit suite in test/. Needs llvm-lit or a pip
# installed lit on PATH; $OPT is derived from LLVM_DIR so the tests run
# against the same llvm we build against.
find_program(LIT_EXE NAMES llvm-lit lit)
if(LIT_EXE)
  get_filename_component(LLVM_BIN_DIR "${LLVM_DIR}/../../../bin" ABSOLUTE)
  add_custom_target(check
    COMMAND ${CMAKE_COMMAND} -E env
            "OPT=${LLVM_BIN_DIR}/opt"
            "BUILD_DIR=${CMAKE_BINARY_DIR}"
            ${LIT_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/test -v
    USES_TERMINAL
    VERBATIM)
endif()

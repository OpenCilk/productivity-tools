# Needed for lit support in standalone builds.
include(AddLLVM)

pythonize_bool(LLVM_ENABLE_EXPENSIVE_CHECKS)
configure_cilktools_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.common.configured.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.common.configured)

set(CILKTOOLS_COMMON_LIT_TEST_DEPS)

# When ANDROID, we build tests with the host compiler (i.e. CMAKE_C_COMPILER),
# and run tests with tools from the host toolchain.
if(NOT ANDROID)
  if(NOT CILKTOOLS_STANDALONE_BUILD AND NOT RUNTIMES_BUILD)
    # Use LLVM utils and Clang from the same build tree.
    list(APPEND CILKTOOLS_COMMON_LIT_TEST_DEPS
      clang FileCheck count not llvm-config llvm-nm llvm-objdump
      llvm-readobj llvm-symbolizer cilktools-headers sancov)
    # list(APPEND CILKTOOLS_COMMON_LIT_TEST_DEPS
    #   clang clang-resource-headers FileCheck count not llvm-config llvm-nm llvm-objdump
    #   llvm-readobj llvm-symbolizer cilktools-headers sancov)
  endif()
endif()

function(cilktools_test_runtime runtime)
  string(TOUPPER ${runtime} runtime_uppercase)
  if(CILKTOOLS_HAS_${runtime_uppercase})
    add_subdirectory(${runtime})
    foreach(directory ${ARGN})
      add_subdirectory(${directory})
    endforeach()
  endif()
endfunction()

# Run tests only if we're sure that clang would produce working
# binaries.
if(CILKTOOLS_CAN_EXECUTE_TESTS)
  cilktools_test_runtime(cilksan)
  # cilktools_test_runtime(cilkscale)
  # cilktools_test_runtime(csi)
endif()

if(CILKTOOLS_STANDALONE_BUILD)
  # Now that we've traversed all the directories and know all the lit testsuites,
  # introduce a rule to run to run all of them.
  get_property(LLVM_LIT_TESTSUITES GLOBAL PROPERTY LLVM_LIT_TESTSUITES)
  get_property(LLVM_LIT_DEPENDS GLOBAL PROPERTY LLVM_LIT_DEPENDS)
  add_lit_target(check-cilktools
    "Running all regression tests"
    ${LLVM_LIT_TESTSUITES}
    DEPENDS ${LLVM_LIT_DEPENDS})
  if(NOT TARGET check-all)
    add_custom_target(check-all)
  endif()
  add_custom_target(cilktools-test-depends DEPENDS ${LLVM_LIT_DEPENDS})
  add_dependencies(check-all check-cilktools)
endif()

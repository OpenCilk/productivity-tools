# Needed for lit support in standalone builds.
include(AddLLVM)

option(COMPILER_RT_TEST_STANDALONE_BUILD_LIBS
  "When set to ON and testing in a standalone build, test the runtime \
  libraries built by this standalone build rather than the runtime libraries \
  shipped with the compiler (used for testing). When set to OFF and testing \
  in a standalone build, test the runtime libraries shipped with the compiler \
  (used for testing). This option has no effect if the compiler and this \
  build are configured to use the same runtime library path."
  ON)
pythonize_bool(COMPILER_RT_TEST_STANDALONE_BUILD_LIBS)

pythonize_bool(LLVM_ENABLE_EXPENSIVE_CHECKS)
configure_cilktools_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.common.configured.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.common.configured)

set(CILKTOOLS_COMMON_LIT_TEST_DEPS)

# When ANDROID, we build tests with the host compiler (i.e. CMAKE_C_COMPILER),
# and run tests with tools from the host toolchain.
if(NOT ANDROID)
  if(NOT CILKTOOLS_STANDALONE_BUILD AND NOT LLVM_RUNTIMES_BUILD)
    # Use LLVM utils and Clang from the same build tree.
    list(APPEND CILKTOOLS_COMMON_LIT_TEST_DEPS
      clang FileCheck count not llvm-config llvm-nm llvm-objdump
      llvm-readelf llvm-readobj llvm-size llvm-symbolizer cilktools-headers sancov split-file)
    if (WIN32)
      list(APPEND SANITIZER_COMMON_LIT_TEST_DEPS KillTheDoctor)
    endif()
  endif()
  # Tests use C++ standard library headers.
  if (TARGET cxx-headers OR HAVE_LIBCXX)
    list(APPEND SANITIZER_COMMON_LIT_TEST_DEPS cxx-headers)
  endif()
endif()

if(CILKTOOLS_STANDALONE_BUILD)
  umbrella_lit_testsuite_begin(check-cilktools)
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
  if(CILKTOOLS_BUILD_CILKSAN)
    cilktools_test_runtime(cilksan)
  endif()
  if(CILKTOOLS_BUILD_CILKSCALE)
    # cilktools_test_runtime(cilkscale)
  endif()
  if(CILKTOOLS_BUILD_CSI)
    # cilktools_test_runtime(csi)
  endif()
endif()

if(CILKTOOLS_STANDALONE_BUILD)
  # Now that we've traversed all the directories and know all the lit testsuites,
  # introduce a rule to run to run all of them.
  add_custom_target(cilktools-test-depends DEPENDS ${LLVM_COMPILER_RT_LIT_DEPENDS})
  umbrella_lit_testsuite_end(check-cilktools)

  if(NOT TARGET check-all)
    add_custom_target(check-all)
  endif()
  add_dependencies(check-all check-cilktools)
endif()

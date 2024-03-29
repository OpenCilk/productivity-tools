# CMake build for Cilktools.
#
# An important constraint of the build is that it only produces libraries
# based on the ability of the host toolchain to target various platforms.

cmake_minimum_required(VERSION 3.13.4)

# Check if cilktools is built as a standalone project.
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR OR CILKTOOLS_STANDALONE_BUILD)
  project(Cilktools CXX C)
  set(CILKTOOLS_STANDALONE_BUILD TRUE)
  set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif()

set(LLVM_COMMON_CMAKE_UTILS_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/../cmake")
set(LLVM_COMMON_CMAKE_UTILS "${LLVM_COMMON_CMAKE_UTILS_DEFAULT}" CACHE PATH "Path to LLVM common cmake utilities")

if (EXISTS "${LLVM_COMMON_CMAKE_UTILS}")
  # Add path for custom cheetah modules.
  list(INSERT CMAKE_MODULE_PATH 0
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules"
    "${LLVM_COMMON_CMAKE_UTILS}"
    "${LLVM_COMMON_CMAKE_UTILS}/Modules"
  )
else()
  message(WARNING "Using local copy of LLVM common cmake utilities. "
    "You can override this behavior path by adding "
    "`-DLLVM_COMMON_CMAKE_UTILS=<path_to_llvm_cmake>` to your CMake invocation "
    "where `<path_to_llvm_cmake>` is the path to the `cmake` directory in "
    "the `opencilk-project` repo.")
  # Add path for custom cheetah modules and local copy of LLVM common modules.
  list(INSERT CMAKE_MODULE_PATH 0
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/LLVMCommonModules"
  )
endif()

if(CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_CFG_RESOLVED_INTDIR "${CMAKE_CFG_INTDIR}/")
else()
  set(CMAKE_CFG_RESOLVED_INTDIR "")
endif()

include(SetPlatformToolchainTools)
include(base-config-ix)
include(CilktoolsUtils)

option(CILKTOOLS_BUILD_CSI "Build csi" ON)
mark_as_advanced(CILKTOOLS_BUILD_CSI)
option(CILKTOOLS_BUILD_CILKSAN "Build Cilksan" ON)
mark_as_advanced(CILKTOOLS_BUILD_CILKSAN)
option(CILKTOOLS_BUILD_CILKSCALE "Build Cilkscale" ON)
mark_as_advanced(CILKTOOLS_BUILD_CILKSCALE)

if (CILKTOOLS_STANDALONE_BUILD)
  set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard to conform to")
  set(CMAKE_CXX_STANDARD_REQUIRED YES)
  set(CMAKE_CXX_EXTENSIONS NO)

  if (NOT LLVM_RUNTIMES_BUILD)
    load_llvm_config()
  endif()
  if (TARGET intrinsics_gen)
    # Loading the llvm config causes this target to be imported so place it
    # under the appropriate folder in an IDE.
    set_target_properties(intrinsics_gen PROPERTIES FOLDER "Cilktools Misc")
  endif()

  find_package(Python3 COMPONENTS Interpreter)
  if(NOT Python3_Interpreter_FOUND)
    message(WARNING "Python3 not found, using python2 as a fallback")
    find_package(Python2 COMPONENTS Interpreter REQUIRED)
    if(Python2_VERSION VERSION_LESS 2.7)
      message(SEND_ERROR "Python 2.7 or newer is required")
    endif()

    # Treat python2 as python3
    add_executable(Python3::Interpreter IMPORTED)
    set_target_properties(Python3::Interpreter PROPERTIES
      IMPORTED_LOCATION ${Python2_EXECUTABLE})
    set(Python3_EXECUTABLE ${Python2_EXECUTABLE})
  endif()

  # Ensure that fat libraries are built correctly on Darwin
  if(APPLE)
    include(UseLibtool)
  endif()

  # Define default arguments to lit.
  set(LIT_ARGS_DEFAULT "-sv")
  if (MSVC OR XCODE)
    set(LIT_ARGS_DEFAULT "${LIT_ARGS_DEFAULT} --no-progress-bar")
  endif()
  set(LLVM_LIT_ARGS "${LIT_ARGS_DEFAULT}" CACHE STRING "Default options for lit")
  set(LLVM_LIT_OUTPUT_DIR "${CILKTOOLS_EXEC_OUTPUT_DIR}")
endif()

# Get llvm-link tool
find_program( LLVM_LINK llvm-link PATHS ${LLVM_TOOLS_BINARY_DIR} NO_DEFAULT_PATH )

construct_cilktools_default_triple()
if ("${CILKTOOLS_DEFAULT_TARGET_TRIPLE}" MATCHES ".*hf$")
  if (${CILKTOOLS_DEFAULT_TARGET_ARCH} MATCHES "^arm")
    set(CILKTOOLS_DEFAULT_TARGET_ARCH "armhf")
  endif()
endif()
if ("${CILKTOOLS_DEFAULT_TARGET_TRIPLE}" MATCHES ".*android.*")
  set(ANDROID 1)
  string(REGEX MATCH "-target(=| +)[^ ]+android[a-z]*([0-9]+)" ANDROID_API_LEVEL "${CMAKE_C_FLAGS}")
  set(ANDROID_API_LEVEL ${CMAKE_MATCH_2})
endif()
pythonize_bool(ANDROID)

set(CILKTOOLS_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(CILKTOOLS_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

pythonize_bool(LLVM_ENABLE_PER_TARGET_RUNTIME_DIR)

# We support running instrumented tests when we're not cross compiling
# and target a UNIX-like system or Windows.
# We can run tests on Android even when we are cross-compiling.
if(("${CMAKE_HOST_SYSTEM_NAME}" STREQUAL "${CMAKE_SYSTEM_NAME}" AND (UNIX OR WIN32))
   OR ANDROID OR CILKTOOLS_EMULATOR)
  option(CILKTOOLS_CAN_EXECUTE_TESTS "Can we execute instrumented tests" ON)
else()
  option(CILKTOOLS_CAN_EXECUTE_TESTS "Can we execute instrumented tests" OFF)
endif()

option(CILKTOOLS_DEBUG "Build runtimes with full debug info" OFF)
option(CILKTOOLS_EXTERNALIZE_DEBUGINFO
  "Generate dSYM files and strip executables and libraries (Darwin Only)" OFF)
# CILKTOOLS_DEBUG_PYBOOL is used by lit.common.configured.in.
pythonize_bool(CILKTOOLS_DEBUG)

option(CILKTOOLS_INTERCEPT_LIBDISPATCH
  "Support interception of libdispatch (GCD). Requires '-fblocks'" OFF)
option(CILKTOOLS_LIBDISPATCH_INSTALL_PATH
  "Specify if libdispatch is installed in a custom location" "")
if (CILKTOOLS_INTERCEPT_LIBDISPATCH AND NOT APPLE)
  set(CILKTOOLS_LIBDISPATCH_CFLAGS -fblocks)
  set(CILKTOOLS_TEST_LIBDISPATCH_CFLAGS)
  if (CILKTOOLS_LIBDISPATCH_INSTALL_PATH)
    list(APPEND CILKTOOLS_TEST_LIBDISPATCH_CFLAGS
        -I${CILKTOOLS_LIBDISPATCH_INSTALL_PATH}/include
        -L${CILKTOOLS_LIBDISPATCH_INSTALL_PATH}/lib
        -Wl,-rpath=${CILKTOOLS_LIBDISPATCH_INSTALL_PATH}/lib)
  endif()
  list(APPEND CILKTOOLS_TEST_LIBDISPATCH_CFLAGS -lBlocksRuntime -ldispatch)
endif()
if (APPLE) # Always enable on Apple platforms.
  set(CILKTOOLS_INTERCEPT_LIBDISPATCH ON)
endif()
pythonize_bool(CILKTOOLS_INTERCEPT_LIBDISPATCH)

include(config-ix)

#================================
# Setup Compiler Flags
#================================

if(MSVC)
  # Override any existing /W flags with /W4. This is what LLVM does.  Failing to
  # remove other /W[0-4] flags will result in a warning about overriding a
  # previous flag.
  if (CILKTOOLS_HAS_W4_FLAG)
    string(REGEX REPLACE " /W[0-4]" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    string(REGEX REPLACE " /W[0-4]" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    append_string_if(CILKTOOLS_HAS_W4_FLAG /W4 CMAKE_C_FLAGS CMAKE_CXX_FLAGS)
  endif()
else()
  append_string_if(CILKTOOLS_HAS_WALL_FLAG -Wall CMAKE_C_FLAGS CMAKE_CXX_FLAGS)
endif()
if(CILKTOOLS_ENABLE_WERROR)
  append_string_if(CILKTOOLS_HAS_WERROR_FLAG -Werror CMAKE_C_FLAGS CMAKE_CXX_FLAGS)
  append_string_if(CILKTOOLS_HAS_WX_FLAG /WX CMAKE_C_FLAGS CMAKE_CXX_FLAGS)
endif()

# Provide some common commmandline flags for Sanitizer runtimes.
append_list_if(CILKTOOLS_HAS_FPIC_FLAG -fPIC SANITIZER_COMMON_CFLAGS)
append_list_if(CILKTOOLS_HAS_FNO_EXCEPTIONS_FLAG -fno-exceptions SANITIZER_COMMON_CFLAGS)
if(NOT CILKTOOLS_DEBUG AND NOT APPLE)
  append_list_if(CILKTOOLS_HAS_FOMIT_FRAME_POINTER_FLAG -fomit-frame-pointer SANITIZER_COMMON_CFLAGS)
endif()
append_list_if(CILKTOOLS_HAS_FUNWIND_TABLES_FLAG -funwind-tables SANITIZER_COMMON_CFLAGS)
append_list_if(CILKTOOLS_HAS_FNO_STACK_PROTECTOR_FLAG -fno-stack-protector SANITIZER_COMMON_CFLAGS)
append_list_if(CILKTOOLS_HAS_FNO_SANITIZE_SAFE_STACK_FLAG -fno-sanitize=safe-stack SANITIZER_COMMON_CFLAGS)
append_list_if(CILKTOOLS_HAS_FVISIBILITY_HIDDEN_FLAG -fvisibility=hidden SANITIZER_COMMON_CFLAGS)
if(NOT CILKTOOLS_HAS_FVISIBILITY_HIDDEN_FLAG)
  append_list_if(CILKTOOLS_HAS_FVISIBILITY_INLINES_HIDDEN_FLAG -fvisibility-inlines-hidden SANITIZER_COMMON_CFLAGS)
endif()
append_list_if(CILKTOOLS_HAS_FNO_LTO_FLAG -fno-lto SANITIZER_COMMON_CFLAGS)

append_list_if(CILKTOOLS_DEBUG -DSANITIZER_DEBUG=1 SANITIZER_COMMON_CFLAGS)

append_list_if(CILKTOOLS_HAS_WTHREAD_SAFETY_FLAG -Wthread-safety THREAD_SAFETY_FLAGS)
append_list_if(CILKTOOLS_HAS_WTHREAD_SAFETY_REFERENCE_FLAG -Wthread-safety-reference THREAD_SAFETY_FLAGS)
append_list_if(CILKTOOLS_HAS_WTHREAD_SAFETY_BETA_FLAG -Wthread-safety-beta THREAD_SAFETY_FLAGS)
list(APPEND SANITIZER_COMMON_CFLAGS ${THREAD_SAFETY_FLAGS})
string(REPLACE ";" " " thread_safety_flags_space_sep "${THREAD_SAFETY_FLAGS}")
string(APPEND CILKTOOLS_TEST_COMPILER_CFLAGS " ${thread_safety_flags_space_sep}")

# Build with optimization, unless we're in debug mode.
if(CILKTOOLS_DEBUG)
  list(APPEND SANITIZER_COMMON_CFLAGS -O0)
else()
  list(APPEND SANITIZER_COMMON_CFLAGS -O3)
endif()

# Determine if we should restrict stack frame sizes.
# Stack frames on PowerPC, Mips, SystemZ and in debug build can be much larger than
# anticipated.
# FIXME: Fix all sanitizers and add -Wframe-larger-than to
# SANITIZER_COMMON_FLAGS
if(CILKTOOLS_HAS_WFRAME_LARGER_THAN_FLAG AND NOT CILKTOOLS_DEBUG
   AND NOT ${CILKTOOLS_DEFAULT_TARGET_ARCH} MATCHES "powerpc|mips|s390x")
  set(SANITIZER_LIMIT_FRAME_SIZE TRUE)
else()
  set(SANITIZER_LIMIT_FRAME_SIZE FALSE)
endif()

if(FUCHSIA OR UNIX)
  set(SANITIZER_USE_SYMBOLS TRUE)
else()
  set(SANITIZER_USE_SYMBOLS FALSE)
endif()

# Build sanitizer runtimes with debug info.
if(CILKTOOLS_HAS_GLINE_TABLES_ONLY_FLAG AND NOT CILKTOOLS_DEBUG)
  list(APPEND SANITIZER_COMMON_CFLAGS -gline-tables-only)
elseif(CILKTOOLS_HAS_G_FLAG)
  list(APPEND SANITIZER_COMMON_CFLAGS -g)
endif()

if(LLVM_ENABLE_MODULES)
  # Sanitizers cannot be built with -fmodules. The interceptors intentionally
  # don't include system headers, which is incompatible with modules.
  list(APPEND SANITIZER_COMMON_CFLAGS -fno-modules)
endif()

# Turn off several warnings.
append_list_if(CILKTOOLS_HAS_WGNU_FLAG -Wno-gnu SANITIZER_COMMON_CFLAGS)
append_list_if(CILKTOOLS_HAS_WVARIADIC_MACROS_FLAG -Wno-variadic-macros SANITIZER_COMMON_CFLAGS)
append_list_if(CILKTOOLS_HAS_WC99_EXTENSIONS_FLAG -Wno-c99-extensions SANITIZER_COMMON_CFLAGS)
append_list_if(CILKTOOLS_HAS_WNON_VIRTUAL_DTOR_FLAG -Wno-non-virtual-dtor SANITIZER_COMMON_CFLAGS)
append_list_if(CILKTOOLS_HAS_WD4146_FLAG /wd4146 SANITIZER_COMMON_CFLAGS)
append_list_if(CILKTOOLS_HAS_WD4291_FLAG /wd4291 SANITIZER_COMMON_CFLAGS)
append_list_if(CILKTOOLS_HAS_WD4391_FLAG /wd4391 SANITIZER_COMMON_CFLAGS)
append_list_if(CILKTOOLS_HAS_WD4722_FLAG /wd4722 SANITIZER_COMMON_CFLAGS)
append_list_if(CILKTOOLS_HAS_WD4800_FLAG /wd4800 SANITIZER_COMMON_CFLAGS)

# Set common link flags.
append_list_if(CILKTOOLS_HAS_NODEFAULTLIBS_FLAG -nodefaultlibs SANITIZER_COMMON_LINK_FLAGS)
append_list_if(CILKTOOLS_HAS_Z_TEXT -Wl,-z,text SANITIZER_COMMON_LINK_FLAGS)

# if (CILKTOOLS_USE_BUILTINS_LIBRARY)
#   string(REPLACE "-Wl,-z,defs" "" CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
# else()
append_list_if(CILKTOOLS_HAS_GCC_S_LIB gcc_s SANITIZER_COMMON_LINK_LIBS)
# endif()

append_list_if(CILKTOOLS_HAS_LIBC c SANITIZER_COMMON_LINK_LIBS)
macro(append_libcxx_libs var)
  if (${var}_INTREE)
    if (SANITIZER_USE_STATIC_LLVM_UNWINDER AND (TARGET unwind_static OR HAVE_LIBUNWIND))
      list(APPEND ${var}_LIBRARIES unwind_static)
    elseif (TARGET unwind_shared OR HAVE_LIBUNWIND)
      list(APPEND ${var}_LIBRARIES unwind_shared)
    endif()

    if (SANITIZER_USE_STATIC_CXX_ABI AND (TARGET cxxabi_static OR HAVE_LIBCXXABI))
      list(APPEND ${var}_LIBRARIES cxxabi_static)
    elseif (TARGET cxxabi_shared OR HAVE_LIBCXXABI)
      list(APPEND ${var}_LIBRARIES cxxabi_shared)
    endif()
  else()
    append_list_if(CILKTOOLS_HAS_LIBCXX c++ ${var}_LIBRARIES)
  endif()
endmacro()

if (SANITIZER_CXX_ABI_LIBNAME STREQUAL "libc++")
  append_libcxx_libs(SANITIZER_CXX_ABI)
elseif (SANITIZER_CXX_ABI_LIBNAME STREQUAL "libcxxabi")
  list(APPEND SANITIZER_CXX_ABI_LIBRARIES "c++abi")
elseif (SANITIZER_CXX_ABI_LIBNAME STREQUAL "libstdc++")
  append_list_if(CILKTOOLS_HAS_LIBSTDCXX stdc++ SANITIZER_CXX_ABI_LIBRARIES)
endif()

if (SANITIZER_TEST_CXX_LIBNAME STREQUAL "libc++")
  append_libcxx_libs(SANITIZER_TEST_CXX)
elseif (SANITIZER_TEST_CXX_LIBNAME STREQUAL "libstdc++")
  append_list_if(CILKTOOLS_HAS_LIBSTDCXX stdc++ SANITIZER_TEST_CXX_LIBRARIES)
endif()

# Warnings to turn off for all libraries, not just sanitizers.
append_string_if(CILKTOOLS_HAS_WUNUSED_PARAMETER_FLAG -Wno-unused-parameter CMAKE_C_FLAGS CMAKE_CXX_FLAGS)

#===============================================================================
# Setup Source Code
#===============================================================================

add_subdirectory(include)

option(CILKTOOLS_USE_LIBCXX
  "Enable compiler-rt to use libc++ from the source tree" ON)
if(CILKTOOLS_USE_LIBCXX)
  if(LLVM_ENABLE_PROJECTS_USED)
    # Don't use libcxx if LLVM_ENABLE_PROJECTS does not enable it.
    set(CILKTOOLS_LIBCXX_PATH ${LLVM_EXTERNAL_LIBCXX_SOURCE_DIR})
    set(CILKTOOLS_LIBCXXABI_PATH ${LLVM_EXTERNAL_LIBCXXABI_SOURCE_DIR})
  else()
    foreach(path IN ITEMS ${LLVM_MAIN_SRC_DIR}/projects/libcxx
                          ${LLVM_MAIN_SRC_DIR}/runtimes/libcxx
                          ${LLVM_MAIN_SRC_DIR}/../libcxx
                          ${LLVM_EXTERNAL_LIBCXX_SOURCE_DIR})
      if(IS_DIRECTORY ${path})
        set(CILKTOOLS_LIBCXX_PATH ${path})
        break()
      endif()
    endforeach()
    foreach(path IN ITEMS ${LLVM_MAIN_SRC_DIR}/projects/libcxxabi
                          ${LLVM_MAIN_SRC_DIR}/runtimes/libcxxabi
                          ${LLVM_MAIN_SRC_DIR}/../libcxxabi
                          ${LLVM_EXTERNAL_LIBCXXABI_SOURCE_DIR})
      if(IS_DIRECTORY ${path})
        set(CILKTOOLS_LIBCXXABI_PATH ${path})
        break()
      endif()
    endforeach()
  endif()
endif()

set(CILKTOOLS_LLD_PATH ${LLVM_MAIN_SRC_DIR}/tools/lld)
if(EXISTS ${CILKTOOLS_LLD_PATH}/ AND LLVM_TOOL_LLD_BUILD)
  set(CILKTOOLS_HAS_LLD TRUE)
else()
  set(CILKTOOLS_LLD_PATH ${LLVM_MAIN_SRC_DIR}/../lld)
  if(EXISTS ${CILKTOOLS_LLD_PATH}/ AND LLVM_TOOL_LLD_BUILD)
    set(CILKTOOLS_HAS_LLD TRUE)
  endif()
endif()
pythonize_bool(CILKTOOLS_HAS_LLD)
pythonize_bool(CILKTOOLS_TEST_USE_LLD)

include(AddCilktools)

set(ALL_CILKTOOLS csi;cilksan;cilkscale)
set(CILKTOOLS_TO_BUILD all CACHE STRING
    "cilktools to build if supported on the target (all;${ALL_CILKTOOLS})")
list_replace(CILKTOOLS_TO_BUILD all "${ALL_CILKTOOLS}")

function(cilktools_build_runtime runtime)
  string(TOUPPER ${runtime} runtime_uppercase)
  if(CILKTOOLS_HAS_${runtime_uppercase})
    add_subdirectory(${runtime})
  endif()
endfunction()

foreach(tool ${CILKTOOLS_TO_BUILD})
  cilktools_build_runtime(${tool})
endforeach()

add_subdirectory(Cilkscale_vis)

if (CILKTOOLS_INCLUDE_TESTS)
  # add_subdirectory(unittests)
  add_subdirectory(test)
endif()

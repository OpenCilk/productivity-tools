include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include(CheckIncludeFiles)
include(CheckLibraryExists)
include(CheckSymbolExists)
include(TestBigEndian)

function(check_linker_flag flag out_var)
  cmake_push_check_state()
  set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} ${flag}")
  check_cxx_compiler_flag("" ${out_var})
  cmake_pop_check_state()
endfunction()

check_library_exists(c fopen "" CILKTOOLS_HAS_LIBC)
check_library_exists(gcc_s __gcc_personality_v0 "" CILKTOOLS_HAS_GCC_S_LIB)

check_c_compiler_flag(-nodefaultlibs CILKTOOLS_HAS_NODEFAULTLIBS_FLAG)
if (CILKTOOLS_HAS_NODEFAULTLIBS_FLAG)
  set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -nodefaultlibs")
  if (CILKTOOLS_HAS_LIBC)
    list(APPEND CMAKE_REQUIRED_LIBRARIES c)
  endif ()
  # if (CILKTOOLS_USE_BUILTINS_LIBRARY)
  #   list(APPEND CMAKE_REQUIRED_LIBRARIES "${CILKTOOLS_BUILTINS_LIBRARY}")
  # elseif (CILKTOOLS_HAS_GCC_S_LIB)
  if (CILKTOOLS_HAS_GCC_S_LIB)
    list(APPEND CMAKE_REQUIRED_LIBRARIES gcc_s)
  elseif (CILKTOOLS_HAS_GCC_LIB)
    list(APPEND CMAKE_REQUIRED_LIBRARIES gcc)
  endif ()
endif ()

# Check compiler flags
check_c_compiler_flag(-std=c11               CILKTOOLS_HAS_STD_C11_FLAG)
check_c_compiler_flag(-fomit-frame-pointer   CILKTOOLS_HAS_FOMIT_FRAME_POINTER_FLAG)
check_cxx_compiler_flag(-fPIC                CILKTOOLS_HAS_FPIC_FLAG)
check_cxx_compiler_flag(-fPIE                CILKTOOLS_HAS_FPIE_FLAG)
check_cxx_compiler_flag(-fno-builtin         CILKTOOLS_HAS_FNO_BUILTIN_FLAG)
check_cxx_compiler_flag(-fno-exceptions      CILKTOOLS_HAS_FNO_EXCEPTIONS_FLAG)
check_cxx_compiler_flag(-fomit-frame-pointer CILKTOOLS_HAS_FOMIT_FRAME_POINTER_FLAG)
check_cxx_compiler_flag(-funwind-tables      CILKTOOLS_HAS_FUNWIND_TABLES_FLAG)
check_cxx_compiler_flag(-fno-stack-protector CILKTOOLS_HAS_FNO_STACK_PROTECTOR_FLAG)
check_cxx_compiler_flag(-fno-sanitize=safe-stack CILKTOOLS_HAS_FNO_SANITIZE_SAFE_STACK_FLAG)
check_cxx_compiler_flag(-fvisibility=hidden  CILKTOOLS_HAS_FVISIBILITY_HIDDEN_FLAG)
check_cxx_compiler_flag(-frtti               CILKTOOLS_HAS_FRTTI_FLAG)
check_cxx_compiler_flag(-fno-rtti            CILKTOOLS_HAS_FNO_RTTI_FLAG)
check_cxx_compiler_flag("-Werror -fno-function-sections" CILKTOOLS_HAS_FNO_FUNCTION_SECTIONS_FLAG)
check_cxx_compiler_flag(-std=c++14           CILKTOOLS_HAS_STD_CXX14_FLAG)
check_cxx_compiler_flag(-ftls-model=initial-exec CILKTOOLS_HAS_FTLS_MODEL_INITIAL_EXEC)
check_cxx_compiler_flag(-fno-lto             CILKTOOLS_HAS_FNO_LTO_FLAG)
check_cxx_compiler_flag("-Werror -msse3"     CILKTOOLS_HAS_MSSE3_FLAG)
check_cxx_compiler_flag("-Werror -msse4.2"   CILKTOOLS_HAS_MSSE4_2_FLAG)
check_cxx_compiler_flag(--sysroot=.          CILKTOOLS_HAS_SYSROOT_FLAG)
check_cxx_compiler_flag("-Werror -mcrc"      CILKTOOLS_HAS_MCRC_FLAG)
check_cxx_compiler_flag(-fno-partial-inlining CILKTOOLS_HAS_FNO_PARTIAL_INLINING_FLAG)
check_cxx_compiler_flag(-fdebug-default-version=4 CILKTOOLS_HAS_FDEBUG_DEFAULT_VERSION_EQ_4_FLAG)
# Check for -fopencilk flag
check_cxx_compiler_flag(-fopencilk CILKTOOLS_HAS_CILK_FLAG)

if (CILKTOOLS_HAS_CILK_FLAG OR TARGET cheetah OR HAVE_CHEETAH)
  set(CILKTOOLS_HAS_CILK 1)
endif()

if(NOT WIN32 AND NOT CYGWIN)
  # MinGW warns if -fvisibility-inlines-hidden is used.
  check_cxx_compiler_flag("-fvisibility-inlines-hidden" CILKTOOLS_HAS_FVISIBILITY_INLINES_HIDDEN_FLAG)
endif()

check_cxx_compiler_flag(/GR CILKTOOLS_HAS_GR_FLAG)
check_cxx_compiler_flag(/GS CILKTOOLS_HAS_GS_FLAG)
check_cxx_compiler_flag(/MT CILKTOOLS_HAS_MT_FLAG)
check_cxx_compiler_flag(/Oy CILKTOOLS_HAS_Oy_FLAG)

# Debug info flags.
check_cxx_compiler_flag(-gline-tables-only CILKTOOLS_HAS_GLINE_TABLES_ONLY_FLAG)
check_cxx_compiler_flag(-g CILKTOOLS_HAS_G_FLAG)
check_cxx_compiler_flag(/Zi CILKTOOLS_HAS_Zi_FLAG)

# Warnings.
check_cxx_compiler_flag(-Wall CILKTOOLS_HAS_WALL_FLAG)
check_cxx_compiler_flag(-Werror CILKTOOLS_HAS_WERROR_FLAG)
check_cxx_compiler_flag("-Werror -Wframe-larger-than=512" CILKTOOLS_HAS_WFRAME_LARGER_THAN_FLAG)
check_cxx_compiler_flag("-Werror -Wglobal-constructors"   CILKTOOLS_HAS_WGLOBAL_CONSTRUCTORS_FLAG)
check_cxx_compiler_flag("-Werror -Wc99-extensions"     CILKTOOLS_HAS_WC99_EXTENSIONS_FLAG)
check_cxx_compiler_flag("-Werror -Wgnu"                CILKTOOLS_HAS_WGNU_FLAG)
check_cxx_compiler_flag("-Werror -Wnon-virtual-dtor"   CILKTOOLS_HAS_WNON_VIRTUAL_DTOR_FLAG)
check_cxx_compiler_flag("-Werror -Wvariadic-macros"    CILKTOOLS_HAS_WVARIADIC_MACROS_FLAG)
check_cxx_compiler_flag("-Werror -Wunused-parameter"   CILKTOOLS_HAS_WUNUSED_PARAMETER_FLAG)
check_cxx_compiler_flag("-Werror -Wcovered-switch-default" CILKTOOLS_HAS_WCOVERED_SWITCH_DEFAULT_FLAG)
check_cxx_compiler_flag(-Wno-pedantic CILKTOOLS_HAS_WNO_PEDANTIC)

check_cxx_compiler_flag(/W4 CILKTOOLS_HAS_W4_FLAG)
check_cxx_compiler_flag(/WX CILKTOOLS_HAS_WX_FLAG)
check_cxx_compiler_flag(/wd4146 CILKTOOLS_HAS_WD4146_FLAG)
check_cxx_compiler_flag(/wd4291 CILKTOOLS_HAS_WD4291_FLAG)
check_cxx_compiler_flag(/wd4221 CILKTOOLS_HAS_WD4221_FLAG)
check_cxx_compiler_flag(/wd4391 CILKTOOLS_HAS_WD4391_FLAG)
check_cxx_compiler_flag(/wd4722 CILKTOOLS_HAS_WD4722_FLAG)
check_cxx_compiler_flag(/wd4800 CILKTOOLS_HAS_WD4800_FLAG)

# Symbols.
check_symbol_exists(__func__ "" CILKTOOLS_HAS_FUNC_SYMBOL)

# Includes.
check_include_files("sys/auxv.h" CILKTOOLS_HAS_AUXV)

# Libraries.
check_library_exists(dl dlopen "" CILKTOOLS_HAS_LIBDL)
check_library_exists(rt shm_open "" CILKTOOLS_HAS_LIBRT)
check_library_exists(m pow "" CILKTOOLS_HAS_LIBM)
check_library_exists(pthread pthread_create "" CILKTOOLS_HAS_LIBPTHREAD)
check_library_exists(execinfo backtrace "" CILKTOOLS_HAS_LIBEXECINFO)

# Look for terminfo library, used in unittests that depend on LLVMSupport.
if(LLVM_ENABLE_TERMINFO)
  foreach(library terminfo tinfo curses ncurses ncursesw)
    string(TOUPPER ${library} library_suffix)
    check_library_exists(
      ${library} setupterm "" CILKTOOLS_HAS_TERMINFO_${library_suffix})
    if(CILKTOOLS_HAS_TERMINFO_${library_suffix})
      set(CILKTOOLS_HAS_TERMINFO TRUE)
      set(CILKTOOLS_TERMINFO_LIB "${library}")
      break()
    endif()
  endforeach()
endif()
check_library_exists(c++ __cxa_throw "" CILKTOOLS_HAS_LIBCXX)
check_library_exists(stdc++ __cxa_throw "" CILKTOOLS_HAS_LIBSTDCXX)

# Linker flags.
check_linker_flag("-Wl,-z,text" CILKTOOLS_HAS_Z_TEXT)
check_linker_flag("-fuse-ld=lld" CILKTOOLS_HAS_FUSE_LD_LLD_FLAG)

# Architectures.

# List of all architectures we can target.
set(CILKTOOLS_SUPPORTED_ARCH)

# Try to compile a very simple source file to ensure we can target the given
# platform. We use the results of these tests to build only the various target
# runtime libraries supported by our current compilers cross-compiling
# abilities.
set(SIMPLE_SOURCE ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/simple.cc)
file(WRITE ${SIMPLE_SOURCE} "#include <stdlib.h>\n#include <stdio.h>\nint main() { printf(\"hello, world\"); }\n")

# Detect whether the current target platform is 32-bit or 64-bit, and setup
# the correct commandline flags needed to attempt to target 32-bit and 64-bit.
if (NOT CMAKE_SIZEOF_VOID_P EQUAL 4 AND
    NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Please use architecture with 4 or 8 byte pointers.")
endif()

test_targets()

# Returns a list of architecture specific target cflags in @out_var list.
function(get_target_flags_for_arch arch out_var)
  list(FIND CILKTOOLS_SUPPORTED_ARCH ${arch} ARCH_INDEX)
  if(ARCH_INDEX EQUAL -1)
    message(FATAL_ERROR "Unsupported architecture: ${arch}")
  else()
    if (NOT APPLE)
      set(${out_var} ${TARGET_${arch}_CFLAGS} PARENT_SCOPE)
    else()
      # This is only called in constructing cflags for tests executing on the
      # host. This will need to all be cleaned up to support building tests
      # for cross-targeted hardware (i.e. iOS).
      set(${out_var} -arch ${arch} PARENT_SCOPE)
    endif()
  endif()
endfunction()

# Returns a compiler and CFLAGS that should be used to run tests for the
# specific architecture.  When cross-compiling, this is controled via
# CILKTOOLS_TEST_COMPILER and CILKTOOLS_TEST_COMPILER_CFLAGS.
macro(get_test_cc_for_arch arch cc_out cflags_out)
  if(ANDROID OR ${arch} MATCHES "arm|aarch64")
    # This is only true if we are cross-compiling.
    # Build all tests with host compiler and use host tools.
    set(${cc_out} ${CILKTOOLS_TEST_COMPILER})
    set(${cflags_out} ${CILKTOOLS_TEST_COMPILER_CFLAGS})
  else()
    get_target_flags_for_arch(${arch} ${cflags_out})
    if(APPLE)
      list(APPEND ${cflags_out} ${DARWIN_osx_CFLAGS})
    endif()
    string(REPLACE ";" " " ${cflags_out} "${${cflags_out}}")
  endif()
endmacro()

# Returns CFLAGS that should be used to run tests for the
# specific apple platform and architecture.
function(get_test_cflags_for_apple_platform platform arch cflags_out)
  is_valid_apple_platform("${platform}" is_valid_platform)
  if (NOT is_valid_platform)
    message(FATAL_ERROR "\"${platform}\" is not a valid apple platform")
  endif()
  set(test_cflags "")
  get_target_flags_for_arch(${arch} test_cflags)
  list(APPEND test_cflags ${DARWIN_${platform}_CFLAGS})
  string(REPLACE ";" " " test_cflags_str "${test_cflags}")
  string(APPEND test_cflags_str "${CILKTOOLS_TEST_COMPILER_CFLAGS}")
  set(${cflags_out} "${test_cflags_str}" PARENT_SCOPE)
endfunction()

function(get_capitalized_apple_platform platform platform_capitalized)
  # TODO(dliew): Remove uses of this function. It exists to preserve needlessly complex
  # directory naming conventions used by the Sanitizer lit test suites.
  is_valid_apple_platform("${platform}" is_valid_platform)
  if (NOT is_valid_platform)
    message(FATAL_ERROR "\"${platform}\" is not a valid apple platform")
  endif()
  string(TOUPPER "${platform}" platform_upper)
  string(REGEX REPLACE "OSSIM$" "OSSim" platform_upper_capitalized "${platform_upper}")
  set(${platform_capitalized} "${platform_upper_capitalized}" PARENT_SCOPE)
endfunction()

function(is_valid_apple_platform platform is_valid_out)
  set(is_valid FALSE)
  if ("${platform}" STREQUAL "")
    message(FATAL_ERROR "platform cannot be empty")
  endif()
  if ("${platform}" MATCHES "^(osx|((ios|watchos|tvos)(sim)?))$")
    set(is_valid TRUE)
  endif()
  set(${is_valid_out} ${is_valid} PARENT_SCOPE)
endfunction()

include(AllSupportedArchDefs)

if(APPLE)
  include(CilktoolsDarwinUtils)

  find_darwin_sdk_dir(DARWIN_osx_SYSROOT macosx)
  find_darwin_sdk_dir(DARWIN_iossim_SYSROOT iphonesimulator)
  find_darwin_sdk_dir(DARWIN_ios_SYSROOT iphoneos)
  find_darwin_sdk_dir(DARWIN_watchossim_SYSROOT watchsimulator)
  find_darwin_sdk_dir(DARWIN_watchos_SYSROOT watchos)
  find_darwin_sdk_dir(DARWIN_tvossim_SYSROOT appletvsimulator)
  find_darwin_sdk_dir(DARWIN_tvos_SYSROOT appletvos)

  if(NOT DARWIN_osx_SYSROOT)
    message(WARNING "Could not determine OS X sysroot, trying /usr/include")
    if(EXISTS /usr/include)
      set(DARWIN_osx_SYSROOT /)
    else()
      message(ERROR "Could not detect OS X Sysroot. Either install Xcode or the Apple Command Line Tools")
    endif()
  endif()

  if(CILKTOOLS_ENABLE_IOS)
    list(APPEND DARWIN_EMBEDDED_PLATFORMS ios)
    set(DARWIN_ios_MIN_VER 9.0)
    set(DARWIN_ios_MIN_VER_FLAG -miphoneos-version-min)
    set(DARWIN_ios_SANITIZER_MIN_VER_FLAG
      ${DARWIN_ios_MIN_VER_FLAG}=${DARWIN_ios_MIN_VER})
    set(DARWIN_iossim_MIN_VER_FLAG -mios-simulator-version-min)
    set(DARWIN_iossim_SANITIZER_MIN_VER_FLAG
      ${DARWIN_iossim_MIN_VER_FLAG}=${DARWIN_ios_MIN_VER})
  endif()
  if(CILKTOOLS_ENABLE_WATCHOS)
    list(APPEND DARWIN_EMBEDDED_PLATFORMS watchos)
    set(DARWIN_watchos_MIN_VER 2.0)
    set(DARWIN_watchos_MIN_VER_FLAG -mwatchos-version-min)
    set(DARWIN_watchos_SANITIZER_MIN_VER_FLAG
      ${DARWIN_watchos_MIN_VER_FLAG}=${DARWIN_watchos_MIN_VER})
    set(DARWIN_watchossim_MIN_VER_FLAG -mwatchos-simulator-version-min)
    set(DARWIN_watchossim_SANITIZER_MIN_VER_FLAG
      ${DARWIN_watchossim_MIN_VER_FLAG}=${DARWIN_watchos_MIN_VER})
  endif()
  if(CILKTOOLS_ENABLE_TVOS)
    list(APPEND DARWIN_EMBEDDED_PLATFORMS tvos)
    set(DARWIN_tvos_MIN_VER 9.0)
    set(DARWIN_tvos_MIN_VER_FLAG -mtvos-version-min)
    set(DARWIN_tvos_SANITIZER_MIN_VER_FLAG
      ${DARWIN_tvos_MIN_VER_FLAG}=${DARWIN_tvos_MIN_VER})
    set(DARWIN_tvossim_MIN_VER_FLAG -mtvos-simulator-version-min)
    set(DARWIN_tvossim_SANITIZER_MIN_VER_FLAG
      ${DARWIN_tvossim_MIN_VER_FLAG}=${DARWIN_tvos_MIN_VER})
  endif()

  set(CILKTOOL_SUPPORTED_OS osx)

  # Note: In order to target x86_64h on OS X the minimum deployment target must
  # be 10.8 or higher.
  set(DEFAULT_SANITIZER_MIN_OSX_VERSION 10.14)
  set(DARWIN_osx_MIN_VER_FLAG "-mmacosx-version-min")
  if(NOT SANITIZER_MIN_OSX_VERSION)
    string(REGEX MATCH "${DARWIN_osx_MIN_VER_FLAG}=([.0-9]+)"
           MACOSX_VERSION_MIN_FLAG "${CMAKE_CXX_FLAGS}")
    if(MACOSX_VERSION_MIN_FLAG)
      set(SANITIZER_MIN_OSX_VERSION "${CMAKE_MATCH_1}")
    elseif(CMAKE_OSX_DEPLOYMENT_TARGET)
      set(SANITIZER_MIN_OSX_VERSION ${CMAKE_OSX_DEPLOYMENT_TARGET})
    else()
      set(SANITIZER_MIN_OSX_VERSION ${DEFAULT_SANITIZER_MIN_OSX_VERSION})
    endif()
    if(SANITIZER_MIN_OSX_VERSION VERSION_LESS "10.7")
      message(FATAL_ERROR "macOS deployment target '${SANITIZER_MIN_OSX_VERSION}' is too old.")
    endif()
  endif()

  # We're setting the flag manually for each target OS
  set(CMAKE_OSX_DEPLOYMENT_TARGET "")

  set(DARWIN_COMMON_CFLAGS -stdlib=libc++)
  set(DARWIN_COMMON_LINK_FLAGS
    -stdlib=libc++
    -lc++
    -lc++abi)

  check_linker_flag("-fapplication-extension" CILKTOOLS_HAS_APP_EXTENSION)
  if(CILKTOOLS_HAS_APP_EXTENSION)
    list(APPEND DARWIN_COMMON_LINK_FLAGS "-fapplication-extension")
  endif()

  set(DARWIN_osx_CFLAGS
    ${DARWIN_COMMON_CFLAGS}
    ${DARWIN_osx_MIN_VER_FLAG}=${SANITIZER_MIN_OSX_VERSION})
  set(DARWIN_osx_LINK_FLAGS
    ${DARWIN_COMMON_LINK_FLAGS}
    ${DARWIN_osx_MIN_VER_FLAG}=${SANITIZER_MIN_OSX_VERSION})

  if(DARWIN_osx_SYSROOT)
    list(APPEND DARWIN_osx_CFLAGS -isysroot ${DARWIN_osx_SYSROOT})
    list(APPEND DARWIN_osx_LINK_FLAGS -isysroot ${DARWIN_osx_SYSROOT})
  endif()

  # Figure out which arches to use for each OS
  darwin_get_toolchain_supported_archs(toolchain_arches)
  message(STATUS "Toolchain supported arches: ${toolchain_arches}")

  if(NOT MACOSX_VERSION_MIN_FLAG)
    darwin_test_archs(osx
      DARWIN_osx_ARCHS
      ${toolchain_arches})
    message(STATUS "OSX supported arches: ${DARWIN_osx_ARCHS}")
    foreach(arch ${DARWIN_osx_ARCHS})
      list(APPEND CILKTOOLS_SUPPORTED_ARCH ${arch})
      set(CAN_TARGET_${arch} 1)
    endforeach()

    foreach(platform ${DARWIN_EMBEDDED_PLATFORMS})
      if(DARWIN_${platform}sim_SYSROOT)
        set(DARWIN_${platform}sim_CFLAGS
          ${DARWIN_COMMON_CFLAGS}
          ${DARWIN_${platform}sim_SANITIZER_MIN_VER_FLAG}
          -isysroot ${DARWIN_${platform}sim_SYSROOT})
        set(DARWIN_${platform}sim_LINK_FLAGS
          ${DARWIN_COMMON_LINK_FLAGS}
          ${DARWIN_${platform}sim_SANITIZER_MIN_VER_FLAG}
          -isysroot ${DARWIN_${platform}sim_SYSROOT})

        set(DARWIN_${platform}sim_SKIP_CC_KEXT On)
        darwin_test_archs(${platform}sim
          DARWIN_${platform}sim_ARCHS
          ${toolchain_arches})
        message(STATUS "${platform} Simulator supported arches: ${DARWIN_${platform}sim_ARCHS}")
        foreach(arch ${DARWIN_${platform}sim_ARCHS})
          list(APPEND CILKTOOLS_SUPPORTED_ARCH ${arch})
          set(CAN_TARGET_${arch} 1)
        endforeach()
      endif()

      if(DARWIN_${platform}_SYSROOT)
        set(DARWIN_${platform}_CFLAGS
          ${DARWIN_COMMON_CFLAGS}
          ${DARWIN_${platform}_SANITIZER_MIN_VER_FLAG}
          -isysroot ${DARWIN_${platform}_SYSROOT})
        set(DARWIN_${platform}_LINK_FLAGS
          ${DARWIN_COMMON_LINK_FLAGS}
          ${DARWIN_${platform}_SANITIZER_MIN_VER_FLAG}
          -isysroot ${DARWIN_${platform}_SYSROOT})

        darwin_test_archs(${platform}
          DARWIN_${platform}_ARCHS
          ${toolchain_arches})
        message(STATUS "${platform} supported arches: ${DARWIN_${platform}_ARCHS}")
        foreach(arch ${DARWIN_${platform}_ARCHS})
          list(APPEND CILKTOOLS_SUPPORTED_ARCH ${arch})
          set(CAN_TARGET_${arch} 1)
        endforeach()
      endif()
    endforeach()
  endif()

  # for list_intersect
  include(CilktoolsUtils)

  list_intersect(CILKTOOLS_COMMON_SUPPORTED_ARCH
    ALL_CILKTOOLS_COMMON_SUPPORTED_ARCH
    CILKTOOLS_SUPPORTED_ARCH
    )
  list_intersect(CSI_SUPPORTED_ARCH
    ALL_CSI_SUPPORTED_ARCH
    CILKTOOLS_COMMON_SUPPORTED_ARCH)
  list_intersect(CILKSAN_SUPPORTED_ARCH
    ALL_CILKSAN_SUPPORTED_ARCH
    CILKTOOLS_COMMON_SUPPORTED_ARCH)
  list_intersect(CILKSCALE_SUPPORTED_ARCH
    ALL_CILKSCALE_SUPPORTED_ARCH
    CILKTOOLS_COMMON_SUPPORTED_ARCH)

else()
  # for filter_available_targets
  include(CilktoolsUtils)

  filter_available_targets(CSI_SUPPORTED_ARCH ${ALL_CSI_SUPPORTED_ARCH})
  filter_available_targets(CILKSAN_SUPPORTED_ARCH ${ALL_CILKSAN_SUPPORTED_ARCH})
  filter_available_targets(CILKSCALE_SUPPORTED_ARCH ${ALL_CILKSCALE_SUPPORTED_ARCH})
endif()

if(CILKTOOLS_SUPPORTED_ARCH)
  list(REMOVE_DUPLICATES CILKTOOLS_SUPPORTED_ARCH)
endif()
message(STATUS "Cilktools supported architectures: ${CILKTOOLS_SUPPORTED_ARCH}")

if (CSI_SUPPORTED_ARCH AND OS_NAME MATCHES "Linux")
  set(CILKTOOLS_HAS_CSI TRUE)
else()
  set(CILKTOOLS_HAS_CSI FALSE)
endif()

if (CILKSAN_SUPPORTED_ARCH AND
    OS_NAME MATCHES "Linux|Darwin|FreeBSD")
  set(CILKTOOLS_HAS_CILKSAN TRUE)
else()
  set(CILKTOOLS_HAS_CILKSAN FALSE)
endif()

if (CILKSCALE_SUPPORTED_ARCH AND
    OS_NAME MATCHES "Linux|Darwin|FreeBSD")
  set(CILKTOOLS_HAS_CILKSCALE TRUE)
else()
  set(CILKTOOLS_HAS_CILKSCALE FALSE)
endif()

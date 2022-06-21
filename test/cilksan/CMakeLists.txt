set(CILKSAN_LIT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(CILKSAN_TESTSUITES)
set(CILKSAN_DYNAMIC_TESTSUITES)

macro(get_bits_for_arch arch bits)
  if (${arch} MATCHES "x86_64|powerpc64|powerpc64le|aarch64|arm64|mips64|mips64el|s390x|sparcv9")
    set(${bits} 64)
  elseif (${arch} MATCHES "i386|arm|mips|mipsel|sparc")
    set(${bits} 32)
  else()
    message(FATAL_ERROR "Unknown target architecture: ${arch}")
  endif()
endmacro()

set(CILKSAN_TEST_DEPS ${CILKTOOLS_COMMON_LIT_TEST_DEPS})
if(NOT CILKTOOLS_STANDALONE_BUILD)
  list(APPEND CILKSAN_TEST_DEPS cilksan)
  if(NOT APPLE AND CILKTOOLS_HAS_LLD AND TARGET lld)
    list(APPEND CILKSAN_TEST_DEPS lld)
  endif()
endif()
set(CILKSAN_DYNAMIC_TEST_DEPS ${CILKSAN_TEST_DEPS})

set(CILKSAN_TEST_ARCH ${CILKSAN_SUPPORTED_ARCH})
if(APPLE)
  darwin_filter_host_archs(CILKSAN_SUPPORTED_ARCH CILKSAN_TEST_ARCH)
endif()

foreach(arch ${CILKSAN_TEST_ARCH})
  set(CILKSAN_TEST_TARGET_ARCH ${arch})
  set(CILKSAN_TEST_APPLE_PLATFORM "osx")
  set(CILKSAN_TEST_MIN_DEPLOYMENT_TARGET_FLAG "${DARWIN_osx_MIN_VER_FLAG}")
  string(TOLOWER "-${arch}-${OS_NAME}" CILKSAN_TEST_CONFIG_SUFFIX)
  get_bits_for_arch(${arch} CILKSAN_TEST_BITS)
  get_test_cc_for_arch(${arch} CILKSAN_TEST_TARGET_CC CILKSAN_TEST_TARGET_CFLAGS)
  if(ANDROID OR APPLE)
    set(CILKSAN_TEST_DYNAMIC True)
  else()
    set(CILKSAN_TEST_DYNAMIC False)
  endif()
  string(TOUPPER ${arch} ARCH_UPPER_CASE)
  set(CONFIG_NAME ${ARCH_UPPER_CASE}${OS_NAME}Config)
  configure_lit_site_cfg(
    ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
    ${CMAKE_CURRENT_BINARY_DIR}/${CONFIG_NAME}/lit.site.cfg.py
    MAIN_CONFIG
    ${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py
    )
  list(APPEND CILKSAN_TESTSUITES ${CMAKE_CURRENT_BINARY_DIR}/${CONFIG_NAME})

  if(CILKTOOLS_CILKSAN_HAS_STATIC_RUNTIME)
    string(TOLOWER "-${arch}-${OS_NAME}-dynamic" CILKSAN_TEST_CONFIG_SUFFIX)
    set(CILKSAN_TEST_DYNAMIC True)
    set(CONFIG_NAME ${ARCH_UPPER_CASE}${OS_NAME}DynamicConfig)
    configure_lit_site_cfg(
      ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
      ${CMAKE_CURRENT_BINARY_DIR}/${CONFIG_NAME}/lit.site.cfg.py
      MAIN_CONFIG
      ${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py
      )
    list(APPEND CILKSAN_DYNAMIC_TESTSUITES
      ${CMAKE_CURRENT_BINARY_DIR}/${CONFIG_NAME})
  endif()
endforeach()

set(exclude_from_check_all.g "")

add_lit_testsuite(check-cilksan "Running the Cilksan tests"
  ${CILKSAN_TESTSUITES}
  ${exclude_from_check_all}
  DEPENDS ${CILKSAN_TEST_DEPS})
set_target_properties(check-cilksan PROPERTIES FOLDER "Cilktools Misc")

if(CILKTOOLS_CILKSAN_HAS_STATIC_RUNTIME)
  add_lit_testsuite(check-cilksan-dynamic
                    "Running the Cilksan tests with dynamic runtime"
                    ${CILKSAN_DYNAMIC_TESTSUITES}
                    ${exclude_from_check_all.g}
                    DEPENDS ${CILKSAN_DYNAMIC_TEST_DEPS})
  set_target_properties(check-cilksan-dynamic
                        PROPERTIES FOLDER "Cilktools Misc")
endif()

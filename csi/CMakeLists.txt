# Build for the ComprehensiveStaticInstrumentation runtime support library.

add_custom_target(csi)

set(CSI_RTL_CFLAGS ${SANITIZER_COMMON_CFLAGS})
append_list_if(CILKTOOLS_HAS_STD_C11_FLAG -std=c11 CSI_RTL_CFLAGS)
append_rtti_flag(OFF CSI_RTL_CFLAGS)

include_directories(${CILKTOOLS_SOURCE_DIR}/include)

set(CSI_SOURCES csirt.c)

foreach (arch ${CSI_SUPPORTED_ARCH})
  add_cilktools_runtime(clang_rt.csi
    SHARED
    ARCHS ${arch}
    SOURCES ${CSI_SOURCES}
    CFLAGS ${CSI_RTL_CFLAGS}
    PARENT_TARGET csi)
  add_cilktools_runtime(clang_rt.csi
    STATIC
    ARCHS ${arch}
    SOURCES ${CSI_SOURCES}
    CFLAGS ${CSI_RTL_CFLAGS}
    PARENT_TARGET csi)
endforeach()

if (CILKTOOLS_INCLUDE_TESTS)
  # TODO: add tests
endif()

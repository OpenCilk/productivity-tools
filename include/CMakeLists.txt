if (CILKTOOLS_BUILD_CSI)
  set(CSI_HEADERS
    csi/csi.h)
endif(CILKTOOLS_BUILD_CSI)

if (CILKTOOLS_BUILD_CILKSAN)
  set(CILKSAN_HEADERS
    cilk/cilksan.h)
endif(CILKTOOLS_BUILD_CILKSAN)

if (CILKTOOLS_BUILD_CILKSCALE)
  set(CILKSCALE_HEADERS
    cilk/cilkscale.h)
endif(CILKTOOLS_BUILD_CILKSCALE)

set(CILKTOOLS_HEADERS
  ${CSI_HEADERS}
  ${CILKSAN_HEADERS}
  ${CILKSCALE_HEADERS})

set(output_dir ${CILKTOOLS_OUTPUT_DIR}/include)

# Copy cilktools headers to the build tree.
set(out_files)
foreach( f ${CILKTOOLS_HEADERS} )
  set( src ${CMAKE_CURRENT_SOURCE_DIR}/${f} )
  set( dst ${output_dir}/${f} )
  add_custom_command(OUTPUT ${dst}
    DEPENDS ${src}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${src} ${dst}
    COMMENT "Copying cilktools's ${f}...")
  list(APPEND out_files ${dst})
endforeach( f )

add_custom_target(cilktools-headers ALL DEPENDS ${out_files})
add_dependencies(cilktools cilktools-headers)
set_target_properties(cilktools-headers PROPERTIES FOLDER "Cilktools Misc")

# Install csi headers.
install(FILES ${CSI_HEADERS}
  COMPONENT cilktools-headers
  PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
  DESTINATION ${CILKTOOLS_INSTALL_PATH}/include/csi)
# Install headers for Cilksan and Cilkscale.
install(FILES ${CILKSAN_HEADERS} ${CILKSCALE_HEADERS}
  COMPONENT cilktools-headers
  PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
  DESTINATION ${CILKTOOLS_INSTALL_PATH}/include/cilk)

if (NOT CMAKE_CONFIGURATION_TYPES) # don't add this for IDEs.
  add_custom_target(install-cilktools-headers
                    DEPENDS cilktools-headers
                    COMMAND "${CMAKE_COMMAND}"
                            -DCMAKE_INSTALL_COMPONENT="cilktools-headers"
                            -P "${CMAKE_BINARY_DIR}/cmake_install.cmake"
                    USES_TERMINAL)
  add_custom_target(install-cilktools-headers-stripped
                    DEPENDS cilktools-headers
                    COMMAND "${CMAKE_COMMAND}"
                            -DCMAKE_INSTALL_COMPONENT="cilktools-headers"
                            -DCMAKE_INSTALL_DO_STRIP=1
                            -P "${CMAKE_BINARY_DIR}/cmake_install.cmake"
                    USES_TERMINAL)
endif()

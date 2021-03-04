set (files
  "cilkscale.py"
  "plotter.py"
  "runner.py")

foreach (file ${files})
  install(PROGRAMS ${file}
    DESTINATION share/Cilkscale_vis
    COMPONENT cilkscale-vis)
endforeach (file)

add_custom_target(cilkscale-vis DEPENDS ${files})
if(NOT LLVM_ENABLE_IDE)
  add_llvm_install_targets("install-cilkscale-vis"
                           DEPENDS cilkscale-vis
                           COMPONENT cilkscale-vis)
endif()

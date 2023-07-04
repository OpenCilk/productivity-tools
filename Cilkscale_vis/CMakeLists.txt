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
add_cilktools_install_targets(cilkscale-vis)

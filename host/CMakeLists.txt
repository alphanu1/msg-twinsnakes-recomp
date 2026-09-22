add_executable(twin-snakes main.c module.c spr.c interrupt.c dvd_pump.c threads.c display.c heaps.c symbols.c ax_dsp.c)
target_link_libraries(twin-snakes PRIVATE mgs_runtime ${CMAKE_DL_LIBS})
set_target_properties(twin-snakes PROPERTIES C_STANDARD 11)
# -rdynamic puts the host's own symbol names in the dynamic table, so the
# backtrace printed when a run has to be killed names functions rather than
# bare addresses. Diagnostics only; it does not change what is generated.
target_link_options(twin-snakes PRIVATE -rdynamic)

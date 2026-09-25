add_executable(twin-snakes main.c module.c spr.c interrupt.c dvd_pump.c threads.c display.c heaps.c symbols.c ax_dsp.c
               sha1.c launcher_core.c)

# THE LAUNCHER'S SCREEN, when Dear ImGui has been fetched (tools/bootstrap.sh;
# pinned in deps.lock). Without it the game still builds and starts straight
# into play, as before.
set(MGS_IMGUI_DIR "${CMAKE_SOURCE_DIR}/extern/imgui")
if(SDL3_FOUND AND EXISTS "${MGS_IMGUI_DIR}/imgui.cpp")
  target_sources(twin-snakes PRIVATE launcher.cpp
    "${MGS_IMGUI_DIR}/imgui.cpp" "${MGS_IMGUI_DIR}/imgui_draw.cpp"
    "${MGS_IMGUI_DIR}/imgui_tables.cpp" "${MGS_IMGUI_DIR}/imgui_widgets.cpp"
    "${MGS_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp"
    "${MGS_IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp")
  target_include_directories(twin-snakes PRIVATE "${MGS_IMGUI_DIR}" "${MGS_IMGUI_DIR}/backends")
  target_compile_definitions(twin-snakes PRIVATE MGS_HAVE_LAUNCHER=1)
else()
  message(STATUS "launcher: extern/imgui not fetched - the game starts without it")
endif()
target_link_libraries(twin-snakes PRIVATE mgs_runtime ${CMAKE_DL_LIBS})
set_target_properties(twin-snakes PROPERTIES C_STANDARD 11)
# -rdynamic puts the host's own symbol names in the dynamic table, so the
# backtrace printed when a run has to be killed names functions rather than
# bare addresses. Diagnostics only; it does not change what is generated.
if(WIN32)
  # Windows: the C, C++ and thread runtimes linked in, so the game needs
  # nothing beside it but SDL3.dll. (-rdynamic is an ELF feature.)
  target_link_options(twin-snakes PRIVATE -static)
else()
  target_link_options(twin-snakes PRIVATE -rdynamic)
endif()

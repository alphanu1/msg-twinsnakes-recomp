# The native SDK runtime.
#
# No game knowledge and no ModernGekko dependency: phase 1 borrowed a
# Dolphin-derived runtime to prove the translated CPU code, and this replaces
# it. Nothing here includes a ModernGekko or Dolphin header.
add_library(mgs_runtime STATIC
  memory/guest.c
  os/os_report.c
  os/card_shims.c
  os/mem_shims.c
  os/os_time.c
  os/boot_info.c
  dsp/aram.c
  gx/efb.c
  gx/fifo.c
  gx/vertex.c
  gx/raster.c
  gx/bp.c
  gx/texture.c
  gx/tev.c
  gfx/gpu.c
  os/os_thread.c
  os/cpu_seam.c
  os/ppc_shims.c
  os/patch_table.c
  platform/jobs.c
  platform/mmio.c
  platform/exi_card.c
  platform/exi_ipl.c
  platform/profile.c
  dvd/fst.c
  dvd/disc.c
  dvd/disc_locate.c
  dvd/dvd.c
  dvd/dol.c
  dvd/dvd_shims.c
  dvd/disc_swap.c
  pad/pad.c)

# SPIR-V, compiled at BUILD time and embedded.
#
# The TEV-to-shader generator will compile GLSL at RUNTIME with shaderc,
# because a shader is generated from the combiner state and cached by hash.
# These base shaders are different: they are fixed, so compiling them here
# keeps shaderc off the runtime's critical path and out of the link for a
# build that never generates one.
find_program(MGS_GLSLC glslc)
if(MGS_GLSLC)
  set(MGS_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
  file(MAKE_DIRECTORY "${MGS_SHADER_DIR}")
  foreach(shader gx.vert gx.frag gxtev.frag)
    if(shader MATCHES "vert$")
      set(stage vertex)
    else()
      set(stage fragment)
    endif()
    add_custom_command(
      OUTPUT "${MGS_SHADER_DIR}/${shader}.inc"
      COMMAND "${MGS_GLSLC}" -mfmt=c -fshader-stage=${stage}
              "${CMAKE_CURRENT_SOURCE_DIR}/gfx/shaders/${shader}"
              -o "${MGS_SHADER_DIR}/${shader}.inc"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/gfx/shaders/${shader}"
      COMMENT "Compiling ${shader} to SPIR-V")
    list(APPEND MGS_SHADER_INCS "${MGS_SHADER_DIR}/${shader}.inc")
  endforeach()
  add_custom_target(mgs_shaders DEPENDS ${MGS_SHADER_INCS})
  add_dependencies(mgs_runtime mgs_shaders)
  target_include_directories(mgs_runtime PRIVATE "${MGS_SHADER_DIR}")
  target_compile_definitions(mgs_runtime PRIVATE MGS_HAVE_SHADERS=1)
else()
  message(STATUS "glslc not found: the GPU backend will have no shaders")
endif()

if(SDL3_FOUND)
  target_sources(mgs_runtime PRIVATE platform/sdl_input.c platform/sdl_video.c platform/sdl_audio.c)
  target_link_libraries(mgs_runtime PUBLIC SDL3::SDL3)
  target_compile_definitions(mgs_runtime PUBLIC MGS_HAVE_SDL3=1)
endif()

target_include_directories(mgs_runtime PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/os")

find_package(Threads REQUIRED)
target_link_libraries(mgs_runtime PUBLIC Threads::Threads)

set_target_properties(mgs_runtime PROPERTIES
  C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)

# -O2, never -O3, and never -ffast-math: paired-single semantics need exact
# rounding, and the design document calls a divergence here a bug in the shim
# layer. Fast-math would make that bug unreachable by inspection.
if(NOT MSVC)
  target_compile_options(mgs_runtime PRIVATE -O2 -fno-fast-math -Wall -Wextra)
endif()

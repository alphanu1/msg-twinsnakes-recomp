# The native SDK runtime.
#
# No game knowledge and no ModernGekko dependency: phase 1 borrowed a
# Dolphin-derived runtime to prove the translated CPU code, and this replaces
# it. Nothing here includes a ModernGekko or Dolphin header.
add_library(mgs_runtime STATIC
  memory/guest.c
  os/os_report.c
  os/os_time.c
  os/os_thread.c
  os/patch_table.c
  platform/jobs.c
  dvd/fst.c
  dvd/disc.c
  dvd/disc_locate.c)

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

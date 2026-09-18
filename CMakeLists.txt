# Twin Snakes native port.
#
# The recompiler runs as a build step on the user's own disc, so this tree
# never contains game code. See README.md.
cmake_minimum_required(VERSION 3.16)
project(twin_snakes_port C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Clang is preferred for the generated code - the design document notes MSVC is
# slow on the multi-MB generated files - but the runtime itself builds with
# anything C11.
add_subdirectory(runtime)

# game/ holds game-specific glue: the module dispatch router, and later the
# asset paths and REL list. Built separately for now, since it needs DolRecomp
# output that only exists once a disc has been supplied.
option(MGS_BUILD_MODULE "Build the recompiled game module (needs DolRecomp output)" OFF)
if(MGS_BUILD_MODULE)
  add_subdirectory(game/module)
endif()

enable_testing()
add_subdirectory(tests)

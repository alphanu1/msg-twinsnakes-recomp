# Combined DOL + REL module for Twin Snakes.
#
# ModernGekko's module-template builds main.dol only. This builds both modules
# into one shared object, joined by the dispatch router in dispatch.c.
#
#   cmake -S game/module -B build/phase1/module -G Ninja \
#         -DDOL_GENERATED=build/phase1/dol/generated \
#         -DREL_GENERATED=build/phase1/rel/generated \
#         -DGXRUNTIME_DIR=<...>/vendor/dolphin/GXRuntime
cmake_minimum_required(VERSION 3.16)
project(mgs_module C)

set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

foreach(V DOL_GENERATED REL_GENERATED GXRUNTIME_DIR)
  if(NOT ${V})
    message(FATAL_ERROR "set -D${V}=<path>")
  endif()
  if(NOT EXISTS "${${V}}")
    message(FATAL_ERROR "${V} does not exist: ${${V}}")
  endif()
endforeach()

file(GLOB DOL_CHUNKS "${DOL_GENERATED}/chunks/*.c")
file(GLOB REL_CHUNKS "${REL_GENERATED}/chunks/*.c")
list(LENGTH DOL_CHUNKS N_DOL)
list(LENGTH REL_CHUNKS N_REL)
message(STATUS "DOL chunks: ${N_DOL}   REL chunks: ${N_REL}")

# Each module's chunks compile against ITS OWN generated.h. The include
# directories differ per source file, so they cannot share one target without
# the wrong generated.h winning - hence a static library per module.
add_library(mgs_dol_code STATIC ${DOL_CHUNKS} dol_bridge.c)
target_include_directories(mgs_dol_code PRIVATE
  "${DOL_GENERATED}/.." "${GXRUNTIME_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(mgs_dol_code PRIVATE DOLRECOMP_ENABLE_REPLACEMENTS=1
  MGS_GENERATED_HEADER="${DOL_GENERATED}/generated.h")

add_library(mgs_rel_code STATIC ${REL_CHUNKS} rel_bridge.c)
target_include_directories(mgs_rel_code PRIVATE
  "${REL_GENERATED}/.." "${GXRUNTIME_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(mgs_rel_code PRIVATE DOLRECOMP_ENABLE_REPLACEMENTS=1
  MGS_GENERATED_HEADER="${REL_GENERATED}/generated.h")

# Position-independent: the result is loaded as a shared object.
set_target_properties(mgs_dol_code mgs_rel_code PROPERTIES
  POSITION_INDEPENDENT_CODE ON)

add_library(mgs_dispatch OBJECT dispatch.c)
target_include_directories(mgs_dispatch PRIVATE
  "${GXRUNTIME_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(mgs_dispatch PROPERTIES POSITION_INDEPENDENT_CODE ON)

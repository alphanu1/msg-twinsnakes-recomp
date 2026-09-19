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

# Where the overlay is recompiled to live. NOT a free choice: DolRecomp's
# --rel-base fixes every absolute address in the generated code, and the game
# loads its overlay at a hard-coded 0x7F008000. Recompiling at any other
# address leaves the code branching correctly and reading its data from the
# wrong place, which nothing downstream can fix. It must match the
# --rel-base the overlay was generated with.
set(REL_BASE "0x7F008000" CACHE STRING "Virtual load address of the overlay")

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

# --- the loadable module ----------------------------------------------------
# ModernGekko dlopen()s this and calls staticrecomp_get_module(). The export
# glue is upstream's module_export.c, unmodified: it builds the descriptor from
# module_tables.inc, which we generate for BOTH modules rather than just the
# DOL.
set(MODULE_TEMPLATE "${GXRUNTIME_DIR}/../module-template" CACHE PATH "upstream module-template")
set(CHASSIS_ABI_DIR "${GXRUNTIME_DIR}/../Source/Core/Core/PowerPC/StaticRecomp" CACHE PATH "")
set(DOL_FILE "" CACHE FILEPATH "main.dol, for chunk hashes")
set(REL_FILE "" CACHE FILEPATH "mgso_pal.rel, for chunk hashes")

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(TABLES "${CMAKE_CURRENT_BINARY_DIR}/module_tables.inc")
add_custom_command(
  OUTPUT "${TABLES}"
  COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/gen_combined_tables.py"
          --dol-generated "${DOL_GENERATED}" --dol "${DOL_FILE}"
          --rel-generated "${REL_GENERATED}" --rel "${REL_FILE}"
          --rel-base "${REL_BASE}"
          --out "${TABLES}"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/gen_combined_tables.py"
          "${DOL_GENERATED}/generated.h" "${REL_GENERATED}/generated.h"
  COMMENT "Generating module tables for main.dol + mgso_pal.rel")
add_custom_target(mgs_tables DEPENDS "${TABLES}")

# The GXRuntime CPU sources come with the module, not the host: they provide
# g_mem_write_journal, the interpreter used for SMC fallback, and the exception
# path. Omitting them links cleanly and then fails at dlopen with an undefined
# symbol, because nothing in the module references them directly.
add_library(mgs_module SHARED
  module_export.c          # ours: upstream's, plus the rel_modules fields
  "${GXRUNTIME_DIR}/src/core/cpu.c"
  "${GXRUNTIME_DIR}/src/core/cpu_exception.c"
  "${GXRUNTIME_DIR}/src/core/cpu_interpreter.c"
  "${GXRUNTIME_DIR}/src/core/cpu_interpreter_table.c"
  "${GXRUNTIME_DIR}/src/core/cpu_interpreter_float.c"
  "${GXRUNTIME_DIR}/src/core/cpu_interpreter_integer.c")
add_dependencies(mgs_module mgs_tables)
target_include_directories(mgs_module PRIVATE
  "${GXRUNTIME_DIR}/../../../include"    # moderngekko/module_abi.h
  "${CMAKE_CURRENT_BINARY_DIR}"      # module_tables.inc, ours not upstream's
  "${DOL_GENERATED}"                 # module_export.c includes generated.h
  "${GXRUNTIME_DIR}/include" "${CHASSIS_ABI_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(mgs_module PRIVATE
  DOLRECOMP_ENABLE_REPLACEMENTS=1 MODULE_GAME_ID="GGSPA4")

# --whole-archive: the chunk functions are only ever reached through the
# dispatch tables, never by a direct reference, so a normal static link would
# drop every one of them as unused.
target_link_libraries(mgs_module PRIVATE
  -Wl,--whole-archive mgs_dol_code mgs_rel_code -Wl,--no-whole-archive
  $<TARGET_OBJECTS:mgs_dispatch>
  m)   # the generated float paths call fmod/sqrt; libm is not implicit here
set_target_properties(mgs_module PROPERTIES
  OUTPUT_NAME "gGGSPA4_recomp" PREFIX ""
  LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/module.exports")

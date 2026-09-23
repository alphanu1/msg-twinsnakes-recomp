add_executable(test_guest_memory test_guest_memory.c)
target_link_libraries(test_guest_memory PRIVATE mgs_runtime)
add_test(NAME guest_memory COMMAND test_guest_memory)

add_executable(test_os_thread test_os_thread.c)
target_link_libraries(test_os_thread PRIVATE mgs_runtime)
add_test(NAME os_thread COMMAND test_os_thread)

add_executable(test_jobs test_jobs.c)
target_link_libraries(test_jobs PRIVATE mgs_runtime)
add_test(NAME jobs COMMAND test_jobs)

add_executable(test_fst test_fst.c)
target_link_libraries(test_fst PRIVATE mgs_runtime)
# Run from the repo root so the test can find an extracted disc if one exists.
add_test(NAME fst COMMAND test_fst WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_disc test_disc.c)
target_link_libraries(test_disc PRIVATE mgs_runtime)
add_test(NAME disc COMMAND test_disc WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_disc_locate test_disc_locate.c)
target_link_libraries(test_disc_locate PRIVATE mgs_runtime)
add_test(NAME disc_locate COMMAND test_disc_locate WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_dvd test_dvd.c)
target_link_libraries(test_dvd PRIVATE mgs_runtime)
add_test(NAME dvd COMMAND test_dvd WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_disc_swap test_disc_swap.c)
target_link_libraries(test_disc_swap PRIVATE mgs_runtime)
add_test(NAME disc_swap COMMAND test_disc_swap WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_pad test_pad.c)
target_link_libraries(test_pad PRIVATE mgs_runtime m)
add_test(NAME pad COMMAND test_pad)

# Not a test: needs hardware. Built so it is always compilable, run by hand.
if(SDL3_FOUND)
  add_executable(pad_probe ${CMAKE_SOURCE_DIR}/tools/probes/pad_probe.c)
  target_link_libraries(pad_probe PRIVATE mgs_runtime m)
endif()

add_executable(test_efb test_efb.c)
target_link_libraries(test_efb PRIVATE mgs_runtime)
add_test(NAME efb COMMAND test_efb)

add_executable(test_gx test_gx.c)
target_link_libraries(test_gx PRIVATE mgs_runtime)
add_test(NAME gx COMMAND test_gx)

add_executable(test_tev test_tev.c)
target_link_libraries(test_tev PRIVATE mgs_runtime)
add_test(NAME tev COMMAND test_tev)

add_executable(test_texture test_texture.c)
target_link_libraries(test_texture PRIVATE mgs_runtime)
add_test(NAME texture COMMAND test_texture)

add_executable(test_mem_shims test_mem_shims.c)
target_link_libraries(test_mem_shims PRIVATE mgs_runtime)
add_test(NAME mem_shims COMMAND test_mem_shims)

add_executable(test_card test_card.c)
target_link_libraries(test_card PRIVATE mgs_runtime)
add_test(NAME card COMMAND test_card)

add_executable(test_dsp_init test_dsp_init.c)
target_link_libraries(test_dsp_init PRIVATE mgs_runtime)
add_test(NAME dsp_init COMMAND test_dsp_init)

# Not a C test: a source check. Two file-scope statics of the same name are
# one object in C, silently, and that cost a boot - see the script.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME no_duplicate_statics
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/../tools/check-duplicate-statics.py)
endif()

add_executable(test_ai_dma test_ai_dma.c)
target_link_libraries(test_ai_dma PRIVATE mgs_runtime)
add_test(NAME ai_dma COMMAND test_ai_dma)

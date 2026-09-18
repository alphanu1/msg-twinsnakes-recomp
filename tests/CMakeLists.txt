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

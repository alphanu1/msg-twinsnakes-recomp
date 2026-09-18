add_executable(test_guest_memory test_guest_memory.c)
target_link_libraries(test_guest_memory PRIVATE mgs_runtime)
add_test(NAME guest_memory COMMAND test_guest_memory)

add_executable(test_os_thread test_os_thread.c)
target_link_libraries(test_os_thread PRIVATE mgs_runtime)
add_test(NAME os_thread COMMAND test_os_thread)

add_executable(test_jobs test_jobs.c)
target_link_libraries(test_jobs PRIVATE mgs_runtime)
add_test(NAME jobs COMMAND test_jobs)

add_executable(twin-snakes main.c)
target_link_libraries(twin-snakes PRIVATE mgs_runtime)
set_target_properties(twin-snakes PROPERTIES C_STANDARD 11)

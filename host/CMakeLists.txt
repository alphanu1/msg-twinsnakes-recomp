add_executable(twin-snakes main.c module.c)
target_link_libraries(twin-snakes PRIVATE mgs_runtime ${CMAKE_DL_LIBS})
set_target_properties(twin-snakes PROPERTIES C_STANDARD 11)

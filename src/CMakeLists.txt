find_package(Threads REQUIRED)
set(COMMON_SRC ipc/socket_implementation.c)

add_executable(server server.c ipc/socket_implementation.c ipc/pipe_implementation.c logic.c)
add_executable(client client.c ipc/socket_implementation.c ipc/pipe_implementation.c)

target_link_libraries(server PRIVATE Threads::Threads)
target_link_libraries(client PRIVATE Threads::Threads)
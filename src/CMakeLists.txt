find_package(Threads REQUIRED)
set(COMMON_SRC src/socket_ipc.c)

add_executable(client src/client.c src/socket_ipc.c src/logic.c)
add_executable(server src/server.c src/socket_ipc.c src/logic.c)

target_link_libraries(server PRIVATE Threads::Threads)
target_link_libraries(client PRIVATE Threads::Threads)
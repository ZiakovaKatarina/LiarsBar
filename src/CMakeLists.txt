find_package(Threads REQUIRED)
set(COMMON_SRC src/socket_ipc.c)

add_executable(client client.c socket_ipc.c logic.c)
add_executable(server server.c socket_ipc.c logic.c game.c)

target_link_libraries(client pthread)
target_link_libraries(server pthread)
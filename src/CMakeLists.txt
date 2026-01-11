find_package(Threads REQUIRED)

add_executable(client
    client.c
    client_state.c
    client_ui.c
    ipc_socket.c
)

add_executable(server
    server.c
    server_player.c
    server_game.c
    ipc_socket.c
)

target_link_libraries(client PRIVATE Threads::Threads)
target_link_libraries(server PRIVATE Threads::Threads)
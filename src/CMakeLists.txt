find_package(Threads REQUIRED)

add_executable(client
    client.c
    client_state.c
    client_ui.c
    socket_ipc.c
    logic.c
)

add_executable(server
    server.c
    socket_ipc.c
    logic.c
    game.c
)

target_link_libraries(client PRIVATE Threads::Threads)
target_link_libraries(server PRIVATE Threads::Threads)
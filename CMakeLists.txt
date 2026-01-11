cmake_minimum_required(VERSION 3.10)
project(LiarsBar C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

find_package(Threads REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(client 
    src/client.c
    src/client_state.c
    src/client_ui.c
    src/ipc_socket.c
)

add_executable(server 
    src/server.c
    src/server_player.c
    src/server_game.c
    src/ipc_socket.c
)

target_link_libraries(client PRIVATE Threads::Threads)
target_link_libraries(server PRIVATE Threads::Threads)
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
    src/socket_ipc.c
    src/logic.c
)

add_executable(server 
    src/server.c
    src/socket_ipc.c
    src/logic.c
    src/game.c
)

target_link_libraries(client PRIVATE Threads::Threads)
target_link_libraries(server PRIVATE Threads::Threads)
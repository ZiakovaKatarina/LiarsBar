cmake_minimum_required(VERSION 3.10)
project(LiarsBar C)

set(CMAKE_C_STANDARD 11)

find_package(Threads REQUIRED)

add_executable(client 
    src/client.c 
    src/socket_ipc.c 
    src/logic.c
)

add_executable(server 
    src/server.c 
    src/socket_ipc.c 
    src/logic.c
)

target_include_directories(client PRIVATE include)
target_include_directories(server PRIVATE include)

target_link_libraries(client PRIVATE Threads::Threads)
target_link_libraries(server PRIVATE Threads::Threads)
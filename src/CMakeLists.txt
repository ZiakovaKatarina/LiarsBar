find_package(Threads REQUIRED)

add_executable(server server.c)
add_executable(client client.c)

target_link_libraries(server PRIVATE Threads::Threads)
target_link_libraries(client PRIVATE Threads::Threads)
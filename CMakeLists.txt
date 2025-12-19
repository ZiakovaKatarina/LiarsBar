cmake_minimum_required(VERSION 3.10)

project(
    LiarsBar
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)

find_package(Threads REQUIRED)

include_directories(include)
add_subdirectory(src)
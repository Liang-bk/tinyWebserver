cmake_minimum_required(VERSION 3.27)
project(webserver)
set(CMAKE_CXX_STANDARD 20)

include_directories(${CMAKE_SOURCE_DIR}/src)

add_subdirectory(buffer)
add_subdirectory(http)
add_subdirectory(logger)
add_subdirectory(pool)
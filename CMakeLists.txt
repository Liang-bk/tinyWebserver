cmake_minimum_required(VERSION 3.27)
project(webserver)

set(CMAKE_CXX_STANDARD 20)
find_package(Threads REQUIRED)


include_directories(${CMAKE_SOURCE_DIR})
add_executable(webserver main.cpp
        buffer/buffer.cpp
        buffer/buffer.h
        logger/blockqueue.h
        logger/logger.cpp
        logger/logger.h)

target_link_libraries(${PROJECT_NAME} Threads::Threads)
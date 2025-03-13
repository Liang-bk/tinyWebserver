cmake_minimum_required(VERSION 3.27)
project(webserver)

set(CMAKE_CXX_STANDARD 20)

add_executable(webserver main.cpp
        buffer/buffer.cpp
        buffer/buffer.h)

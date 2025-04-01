cmake_minimum_required(VERSION 3.27)
project(test)
set(CMAKE_CXX_STANDARD 20)
include_directories(${CMAKE_SOURCE_DIR})
add_executable(test
        test.cpp
)
find_package(Threads REQUIRED)
target_link_libraries(test Threads::Threads)
target_link_libraries(test logger pool buffer http)
cmake_minimum_required(VERSION 3.27)
project(test)
set(CMAKE_CXX_STANDARD 20)
include_directories(${CMAKE_SOURCE_DIR})
add_executable(test1
        test.cpp
)
add_executable(timer_test
        timer_test.cpp
)
find_package(Threads REQUIRED)
target_link_libraries(test1 Threads::Threads)
target_link_libraries(test1 logger pool buffer http timer server)
target_link_libraries(timer_test Threads::Threads)
target_link_libraries(timer_test logger pool buffer http timer server)
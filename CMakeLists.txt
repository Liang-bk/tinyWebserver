cmake_minimum_required(VERSION 3.27)
project(webserver)

set(CMAKE_CXX_STANDARD 20)
include_directories(${CMAKE_SOURCE_DIR}/src)
add_subdirectory(src)
add_subdirectory(test)

#
#
#include_directories(${CMAKE_SOURCE_DIR})
#include_directories(/usr/include/mysql)
#add_subdirectory(src/buffer)
#add_subdirectory(src/logger)
#add_subdirectory(src/pool)
#add_subdirectory(test)
#add_subdirectory(src/http)

add_executable(webserver
        main.cpp
#        buffer/buffer.cpp
#        buffer/buffer.h
#        logger/blockqueue.h
#        logger/logger.cpp
#        logger/logger.h
#        pool/threadpool.h
#        pool/sqlconnpool.cpp
#        pool/sqlconnpool.h
#        pool/sqlconnRAll.h
)
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} mysqlclient)
target_link_libraries(${PROJECT_NAME} Threads::Threads)
target_link_libraries(${PROJECT_NAME} buffer logger pool)
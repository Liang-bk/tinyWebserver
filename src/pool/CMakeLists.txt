cmake_minimum_required(VERSION 3.27)

add_library(pool
        threadpool.h
        sqlconnpool.h
        sqlconnRAll.h
        sqlconnpool.cpp
)

include_directories(/usr/include/mysql)
target_link_libraries(pool mysqlclient)
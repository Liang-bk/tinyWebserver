cmake_minimum_required(VERSION 3.27)

add_library(server
        epoller.h
        epoller.cpp
        webserver.h
        webserver.cpp
)
include_directories(/usr/include/mysql)
target_link_libraries(server buffer logger pool http timer mysqlclient)
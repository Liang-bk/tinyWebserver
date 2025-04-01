cmake_minimum_required(VERSION 3.27)

add_library(http
        http_request.h
        http_request.cpp
        http_response.cpp
        http_response.h
        http_conn.cpp
        http_conn.h
)
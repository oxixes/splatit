cmake_minimum_required(VERSION 3.22)
project(splatoon_server)

set(CMAKE_CXX_STANDARD 17)

add_executable(splatoon_server main.cpp)

target_include_directories(splatoon_server PRIVATE libs/cpp-httplib)
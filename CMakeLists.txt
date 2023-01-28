cmake_minimum_required(VERSION 3.22)
project(splatoon_server)

set(CMAKE_CXX_STANDARD 17)

add_executable(splatoon_server)

add_subdirectory(src)

set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)
target_link_libraries(splatoon_server OpenSSL::SSL)

if(WIN32)
    target_link_libraries(splatoon_server wsock32 ws2_32)
endif()

target_include_directories(splatoon_server PRIVATE libs/cpp-httplib)
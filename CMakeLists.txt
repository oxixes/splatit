cmake_minimum_required(VERSION 3.22)
project(splatoon_server)

set(CMAKE_CXX_STANDARD 17)

add_executable(splatoon_server)

add_subdirectory(src)

# Libraries setup
set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)

set(HTTPLIB_REQUIRE_OPENSSL ON)
add_subdirectory(libs/cpp-httplib)

if(WIN32)
    target_link_libraries(splatoon_server wsock32 ws2_32 OpenSSL::SSL httplib)
else()
    target_link_libraries(splatoon_server OpenSSL::SSL httplib)
endif()
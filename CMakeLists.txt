cmake_minimum_required(VERSION 3.22)
project(splatoon_server)

set(CMAKE_CXX_STANDARD 20)

add_executable(splatoon_server)

# Libraries setup
set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)

set(HTTPLIB_REQUIRE_OPENSSL ON)
add_subdirectory(libs/cpp-httplib)

set(JWT_BUILD_EXAMPLES OFF)
add_subdirectory(libs/jwt-cpp)

target_include_directories(splatoon_server PRIVATE libs/json/single_include)

add_subdirectory(src)

if (WIN32)
    target_sources(splatoon_server PRIVATE ${OPENSSL_INCLUDE_DIR}/openssl/applink.c) # Allow file writing of OpenSSL
endif()

if(WIN32)
    target_link_libraries(splatoon_server PRIVATE wsock32 ws2_32 OpenSSL::SSL httplib jwt-cpp)
else()
    target_link_libraries(splatoon_server PRIVATE OpenSSL::SSL httplib jwt-cpp)
endif()
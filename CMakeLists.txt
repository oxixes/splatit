cmake_minimum_required(VERSION 3.22)
project(splatoon_server)

set(CMAKE_CXX_STANDARD 20)

add_executable(splatoon_server)

# Libraries setup

#set(HTTPLIB_REQUIRE_OPENSSL ON)
#add_subdirectory(libs/cpp-httplib)

#set(JWT_BUILD_EXAMPLES OFF)
#add_subdirectory(libs/jwt-cpp)

#target_include_directories(splatoon_server PRIVATE libs/json/single_include)

# set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)
target_link_libraries(splatoon_server PRIVATE OpenSSL::SSL OpenSSL::Crypto)

find_package(nlohmann_json REQUIRED)
target_link_libraries(splatoon_server PRIVATE nlohmann_json::nlohmann_json)

find_package(SQLite3 REQUIRED)
target_link_libraries(splatoon_server PRIVATE SQLite::SQLite3)

set(HTTPLIB_REQUIRE_OPENSSL ON)
find_package(httplib CONFIG REQUIRED)
target_link_libraries(splatoon_server PRIVATE httplib::httplib)

add_subdirectory(lib/json-schema-validator)
target_link_libraries(splatoon_server PRIVATE nlohmann_json_schema_validator)

add_subdirectory(src)

if (WIN32)
    target_sources(splatoon_server PRIVATE ${OPENSSL_INCLUDE_DIR}/openssl/applink.c) # Allow file writing of OpenSSL
endif()

if(WIN32)
    target_link_libraries(splatoon_server PRIVATE wsock32 ws2_32)
endif()
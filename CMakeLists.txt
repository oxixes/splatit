cmake_minimum_required(VERSION 3.22)
project(splatoon_server)

set(CMAKE_CXX_STANDARD 20)

if(MSVC)
    add_definitions(-D_WIN32_WINNT=0x0A00)
    add_definitions(-DNOMINMAX)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /permissive-")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:__cplusplus")
endif()

add_executable(splatoon_server)

# Libraries setup

# set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)
# target_link_libraries(splatoon_server PRIVATE OpenSSL::SSL OpenSSL::Crypto) Included in gRPC

find_package(nlohmann_json REQUIRED)
target_link_libraries(splatoon_server PRIVATE nlohmann_json::nlohmann_json)

add_subdirectory(lib/pugixml)
target_link_libraries(splatoon_server PRIVATE pugixml::pugixml)

find_package(unofficial-sqlite3 CONFIG REQUIRED)
target_link_libraries(splatoon_server PRIVATE unofficial::sqlite3::sqlite3)

find_package(PostgreSQL REQUIRED)
target_link_libraries(splatoon_server PRIVATE PostgreSQL::PostgreSQL)

find_package(mailio CONFIG REQUIRED)
target_link_libraries(splatoon_server PRIVATE mailio::mailio)

if(MSVC)
find_package(unofficial-getopt-win32 REQUIRED)
target_link_libraries(splatoon_server PRIVATE unofficial::getopt-win32::getopt)
endif()

add_subdirectory(lib/json-schema-validator)
target_link_libraries(splatoon_server PRIVATE nlohmann_json_schema_validator)

# TODO Automate downloading windows zones file on windows
find_package(date CONFIG REQUIRED)
target_link_libraries(splatoon_server PRIVATE date::date date::date-tz)

find_package(Boost REQUIRED COMPONENTS uuid)
target_link_libraries(splatoon_server PRIVATE Boost::uuid)

find_package(hiredis CONFIG REQUIRED)
target_link_libraries(splatoon_server PRIVATE hiredis::hiredis)

find_package(hiredis_ssl CONFIG REQUIRED)
target_link_libraries(splatoon_server PRIVATE hiredis::hiredis_ssl)

# Add STB image library
target_include_directories(splatoon_server PRIVATE lib/stb)

# Setup gRPC and Protobuf compilation
find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)

set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

file(MAKE_DIRECTORY "${GENERATED_DIR}")

file(GLOB_RECURSE PROTO_FILES "${PROTO_DIR}/*.proto")

# Compile .proto to .pb.cc/.pb.h and .grpc.pb.cc/.grpc.pb.h
foreach(PROTO_FILE ${PROTO_FILES})
    get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)

    set(GENERATED_PB_SRC "${GENERATED_DIR}/${PROTO_NAME}.pb.cc")
    set(GENERATED_PB_HDR "${GENERATED_DIR}/${PROTO_NAME}.pb.h")
    set(GENERATED_GRPC_SRC "${GENERATED_DIR}/${PROTO_NAME}.grpc.pb.cc")
    set(GENERATED_GRPC_HDR "${GENERATED_DIR}/${PROTO_NAME}.grpc.pb.h")

    add_custom_command(
        OUTPUT ${GENERATED_PB_SRC} ${GENERATED_PB_HDR}
        ${GENERATED_GRPC_SRC} ${GENERATED_GRPC_HDR}
        COMMAND protobuf::protoc
        ARGS --proto_path=${PROTO_DIR}
        --cpp_out=${GENERATED_DIR}
        --grpc_out=${GENERATED_DIR}
        --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
        ${PROTO_FILE}
        DEPENDS ${PROTO_FILE}
        COMMENT "Generating gRPC and protobuf sources for ${PROTO_FILE}"
    )

    list(APPEND GENERATED_SRCS ${GENERATED_PB_SRC} ${GENERATED_GRPC_SRC})
endforeach()

target_sources(splatoon_server PRIVATE ${GENERATED_SRCS})
target_include_directories(splatoon_server PRIVATE "${GENERATED_DIR}")

target_sources(splatoon_server PRIVATE ${PROTO_SRCS} ${GRPC_SRCS} ${PROTO_HDRS} ${GRPC_HDRS})
target_include_directories(splatoon_server PRIVATE ${PROTO_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(splatoon_server PRIVATE gRPC::grpc++_reflection)

add_subdirectory(src)

if (WIN32)
    target_sources(splatoon_server PRIVATE ${OPENSSL_INCLUDE_DIR}/openssl/applink.c) # Allow file writing of OpenSSL
endif()

if(WIN32)
    target_link_libraries(splatoon_server PRIVATE wsock32 ws2_32)
endif()
add_subdirectory(services)

target_sources(splatoon_server PRIVATE
        server.cpp
        server.hpp)
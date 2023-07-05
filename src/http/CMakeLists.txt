target_sources(splatoon_server PRIVATE
        server.cpp
        server.hpp
        account.cpp
        account.hpp)

add_subdirectory(parser)
target_sources(splatoon_server PRIVATE
        server.cpp
        server.hpp
        account/account.cpp
        account/account.hpp
        boss/boss.cpp
        boss/boss.hpp)

add_subdirectory(parser)
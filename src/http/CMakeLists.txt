target_sources(splatoon_server PRIVATE
        server.cpp
        server.hpp)

add_subdirectory(parser)
add_subdirectory(account)
add_subdirectory(boss)
add_subdirectory(management)
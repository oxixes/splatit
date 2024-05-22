add_subdirectory(byaml)
add_subdirectory(bfres)

target_sources(splatoon_server PRIVATE
    utils.cpp
    utils.hpp)
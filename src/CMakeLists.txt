add_subdirectory(http)
add_subdirectory(nex)

target_sources(splatoon_server PRIVATE
        main.cpp
        settingsManager.cpp
        settingsManager.hpp
        argParser.cpp
        argParser.hpp)
add_subdirectory(ssl)
add_subdirectory(socket)
add_subdirectory(http)
add_subdirectory(nex)
add_subdirectory(util)
add_subdirectory(db)

target_sources(splatoon_server PRIVATE
        main.cpp
        settingsManager.cpp
        settingsManager.hpp
        argParser.cpp
        argParser.hpp
        logger.cpp
        logger.hpp)
add_subdirectory(migrations)

target_sources(splatoon_server PRIVATE
        database.hpp
        database.cpp
        sqlite3Database.cpp
        sqlite3Database.hpp
        dbTypes.hpp)
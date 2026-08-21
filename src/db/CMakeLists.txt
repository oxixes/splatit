add_subdirectory(migrations)

target_sources(splatoon_server PRIVATE
        database.hpp
        database.cpp
        sqlDatabase.hpp
        sqlDatabase.cpp
        sqlite3Database.cpp
        sqlite3Database.hpp
        postgresDatabase.cpp
        postgresDatabase.hpp
        dbTypes.hpp)

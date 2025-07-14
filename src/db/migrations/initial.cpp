#include "migrations.hpp"

namespace db::migrations {

bool migration_initial_accounts(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<Database>& db, DBType type) {
    std::vector<std::string> sqlCmds;
    switch (type) {
        case DBType::SQLITE3:
            sqlCmds.emplace_back("BEGIN TRANSACTION;");
            sqlCmds.emplace_back("CREATE TABLE db_info (version TEXT);");
            sqlCmds.emplace_back("INSERT INTO db_info (version) VALUES ('0.0.1');");
            sqlCmds.emplace_back("CREATE TABLE emails (id INTEGER, address TEXT NOT NULL, parent INTEGER NOT NULL,"
                                 "'primary' INTEGER NOT NULL, reachable INTEGER NOT NULL, type TEXT NOT NULL,"
                                 "updated_by TEXT NOT NULL, validated INTEGER NOT NULL, validated_date TEXT,"
                                 "PRIMARY KEY (id));");
            sqlCmds.emplace_back("CREATE TABLE miis (id INTEGER, hash TEXT NOT NULL, name TEXT NOT NULL,"
                                 "'primary' INTEGER NOT NULL, data TEXT NOT NULL, PRIMARY KEY (id));");
            sqlCmds.emplace_back("CREATE TABLE users (pid INTEGER, username TEXT NOT NULL, password TEXT NOT NULL,"
                                 "email_id INTEGER NOT NULL, mii_id INTEGER NOT NULL, gender INTEGER NOT NULL,"
                                 "region INTEGER NOT NULL, tz TEXT NOT NULL,"
                                 "language TEXT NOT NULL, active INTEGER NOT NULL, marketing INTEGER NOT NULL,"
                                 "off_device INTEGER NOT NULL, birth_date TEXT NOT NULL, country TEXT NOT NULL,"
                                 "create_date TEXT NOT NULL, last_updated TEXT NOT NULL, PRIMARY KEY (pid),"
                                 "FOREIGN KEY (email_id) REFERENCES emails(id) ON UPDATE CASCADE ON DELETE RESTRICT,"
                                 "FOREIGN KEY (mii_id) REFERENCES miis(id) ON UPDATE CASCADE ON DELETE RESTRICT);");
            sqlCmds.emplace_back("CREATE TABLE devices (id INTEGER, language TEXT NOT NULL, platform_id INTEGER NOT NULL,"
                                 "region INTEGER NOT NULL, serial_num TEXT NOT NULL, system_ver TEXT NOT NULL,"
                                 "type TEXT NOT NULL, updated_by TEXT NOT NULL, last_updated TEXT NOT NULL,"
                                 "PRIMARY KEY (id));");
            sqlCmds.emplace_back("CREATE TABLE device_attributes (device_id INTEGER NOT NULL, pid INTEGER NOT NULL,"
                                 "name TEXT NOT NULL, value TEXT NOT NULL, created_date TEXT NOT NULL,"
                                 "PRIMARY KEY (device_id, pid, name), FOREIGN KEY (device_id) REFERENCES devices(id) "
                                 "ON UPDATE CASCADE ON DELETE CASCADE, FOREIGN KEY (pid) REFERENCES users(pid) "
                                 "ON UPDATE CASCADE ON DELETE CASCADE);");
            sqlCmds.emplace_back("CREATE TABLE ownerships (pid INTEGER NOT NULL, device_id INTEGER NOT NULL,"
                                 "status TEXT NOT NULL, last_updated TEXT NOT NULL, PRIMARY KEY (pid, device_id),"
                                 "FOREIGN KEY (pid) REFERENCES users(pid) ON UPDATE CASCADE ON DELETE CASCADE,"
                                 "FOREIGN KEY (device_id) REFERENCES devices(id) ON UPDATE CASCADE ON DELETE CASCADE);");
            sqlCmds.emplace_back("CREATE TABLE agreements (type TEXT NOT NULL, version INTEGER NOT NULL,"
                                   "country TEXT NOT NULL, language TEXT NOT NULL, language_name TEXT NOT NULL,"
                                   "publish_date TEXT NOT NULL, main_title TEXT NOT NULL, sub_title TEXT NOT NULL,"
                                   "agree_text TEXT NOT NULL, non_agree_text TEXT NOT NULL, main_text TEXT NOT NULL,"
                                   "sub_text TEXT NOT NULL, PRIMARY KEY (type, version, country, language));");
            sqlCmds.emplace_back("CREATE TABLE user_agreements (pid INTEGER NOT NULL, type TEXT NOT NULL, version INTEGER NOT NULL, "
                                   "country TEXT NOT NULL, signed_date TEXT NOT NULL, PRIMARY KEY (pid, type, version, country), "
                                   "FOREIGN KEY (pid) REFERENCES users(pid) ON UPDATE CASCADE ON DELETE CASCADE);");
            sqlCmds.emplace_back("COMMIT;");
            break;
    }

    return runVoidCommandsSync(logger, db, sqlCmds, "ROLLBACK;");
}

bool migration_initial_friendsAuth(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<Database>& db, DBType type) {
    std::vector<std::string> sqlCmds;
    switch (type) {
        case DBType::SQLITE3:
            sqlCmds.emplace_back("BEGIN TRANSACTION;");
            sqlCmds.emplace_back("CREATE TABLE db_info (version TEXT);");
            sqlCmds.emplace_back("INSERT INTO db_info (version) VALUES ('0.0.1');");
            sqlCmds.emplace_back("CREATE TABLE game_server_access (pid INTEGER NOT NULL, "
                                 "password TEXT NOT NULL, PRIMARY KEY (pid));");
            sqlCmds.emplace_back("COMMIT;");
            break;
    }

    return runVoidCommandsSync(logger, db, sqlCmds, "ROLLBACK;");
}

bool migration_initial_splatoonAuth(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<Database>& db, DBType type) {
    std::vector<std::string> sqlCmds;
    switch (type) {
        case DBType::SQLITE3:
            sqlCmds.emplace_back("BEGIN TRANSACTION;");
            sqlCmds.emplace_back("CREATE TABLE db_info (version TEXT);");
            sqlCmds.emplace_back("INSERT INTO db_info (version) VALUES ('0.0.1');");
            sqlCmds.emplace_back("CREATE TABLE game_server_access (pid INTEGER NOT NULL, "
                                 "password TEXT NOT NULL, PRIMARY KEY (pid));");
            sqlCmds.emplace_back("COMMIT;");
            break;
    }

    return runVoidCommandsSync(logger, db, sqlCmds, "ROLLBACK;");
}

bool migration_initial_friends(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<Database>& db, DBType type) {
    std::vector<std::string> sqlCmds;
    switch (type) {
        case DBType::SQLITE3:
            sqlCmds.emplace_back("BEGIN TRANSACTION;");
            sqlCmds.emplace_back("CREATE TABLE db_info (version TEXT);");
            sqlCmds.emplace_back("INSERT INTO db_info (version) VALUES ('0.0.1');");
            sqlCmds.emplace_back("CREATE TABLE user_info (pid INTEGER NOT NULL, show_presence INTEGER NOT NULL DEFAULT (1), "
                                 "show_playing INTEGER NOT NULL DEFAULT (1), block_requests INTEGER NOT NULL DEFAULT (0), "
                                 "nna_info BLOB NOT NULL, presence BLOB NOT NULL, comment BLOB NOT NULL, last_online TEXT NOT NULL, "
                                 "PRIMARY KEY (pid));");
            sqlCmds.emplace_back("CREATE TABLE friendships (pid INTEGER NOT NULL, friend_pid INTEGER NOT NULL, became_friends TEXT NOT NULL,"
                                 "PRIMARY KEY (pid, friend_pid), FOREIGN KEY(pid) REFERENCES user_info(pid) ON UPDATE CASCADE ON DELETE CASCADE,"
                                 "FOREIGN KEY(friend_pid) REFERENCES user_info(pid) ON UPDATE CASCADE ON DELETE CASCADE);");
            sqlCmds.emplace_back("COMMIT;");
            break;
    }

    return runVoidCommandsSync(logger, db, sqlCmds, "ROLLBACK;");
}

}
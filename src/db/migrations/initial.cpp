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
                                 "validation_code TEXT, PRIMARY KEY (id));");
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
                                 "type TEXT NOT NULL, updated_by TEXT NOT NULL, status TEXT NOT NULL,"
                                 "banned INTEGER NOT NULL, last_updated TEXT NOT NULL, PRIMARY KEY (id));");
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
            sqlCmds.emplace_back("CREATE TABLE pending_tasks (id INTEGER PRIMARY KEY AUTOINCREMENT, type INTEGER NOT NULL, "
                                   "params TEXT NOT NULL);");
            sqlCmds.emplace_back("CREATE TABLE settings (key TEXT NOT NULL, value TEXT NOT NULL, PRIMARY KEY (key));");

            sqlCmds.emplace_back("CREATE UNIQUE INDEX unique_username ON users(username);");
            sqlCmds.emplace_back("CREATE UNIQUE INDEX unique_active_user ON ownerships(pid) WHERE status = 'ACTIVE';");

            sqlCmds.emplace_back("CREATE TABLE pid_sequence (value INTEGER);");
            sqlCmds.emplace_back("INSERT INTO pid_sequence (value) VALUES (1800000000);");
            sqlCmds.emplace_back("CREATE TRIGGER set_pid AFTER INSERT ON users "
                                 "FOR EACH ROW "
                                 "WHEN NEW.pid = 0 "
                                 "BEGIN "
                                 "UPDATE users SET pid = (SELECT value - 1 FROM pid_sequence) WHERE pid = NEW.pid; "
                                 "UPDATE pid_sequence SET value = value - 1; "
                                 "END;");

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

// TODO Add trigger to prevent blocks if requests are sent, or viceversa, and the same with friendships
// TODO Add trigger to prevent more than 100 requests received and friendships
// TODO Add trigger to prevent a friend request if the user is blocked or if it's a friend
bool migration_initial_friends(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<Database>& db, DBType type) {
    std::vector<std::string> sqlCmds;
    switch (type) {
        case DBType::SQLITE3:
            sqlCmds.emplace_back("BEGIN TRANSACTION;");
            sqlCmds.emplace_back("CREATE TABLE db_info (version TEXT);");
            sqlCmds.emplace_back("INSERT INTO db_info (version) VALUES ('0.0.1');");
            sqlCmds.emplace_back("CREATE TABLE user_info (pid INTEGER NOT NULL, username TEXT NOT NULL, show_presence INTEGER NOT NULL DEFAULT (1), "
                                 "show_playing INTEGER NOT NULL DEFAULT (1), block_requests INTEGER NOT NULL DEFAULT (0), "
                                 "nna_info BLOB NOT NULL, presence BLOB NOT NULL, comment BLOB NOT NULL, last_online TEXT NOT NULL, "
                                 "PRIMARY KEY (pid));");
            sqlCmds.emplace_back("CREATE TABLE friendships (pid INTEGER NOT NULL, friend_pid INTEGER NOT NULL, became_friends TEXT NOT NULL,"
                                 "uidx_u1 INTEGER NOT NULL, uidx_u2 INTEGER NOT NULL, "
                                 "PRIMARY KEY (pid, friend_pid), FOREIGN KEY(pid) REFERENCES user_info(pid) ON UPDATE CASCADE ON DELETE CASCADE,"
                                 "FOREIGN KEY(friend_pid) REFERENCES user_info(pid) ON UPDATE CASCADE ON DELETE CASCADE);");
            sqlCmds.emplace_back("CREATE TABLE notifications (id INTEGER PRIMARY KEY AUTOINCREMENT, for INTEGER NOT NULL,"
                                 "value1 INTEGER NOT NULL, value2 INTEGER NOT NULL, value3 INTEGER NOT NULL, value4 INTEGER NOT NULL, "
                                 "text TEXT NOT NULL, FOREIGN KEY(for) REFERENCES user_info(pid) ON UPDATE CASCADE ON DELETE CASCADE);");
            sqlCmds.emplace_back("CREATE TABLE friend_requests (id INTEGER PRIMARY KEY AUTOINCREMENT, from_pid INTEGER NOT NULL,"
                                 "to_pid INTEGER NOT NULL, expiration TEXT NOT NULL, created_at TEXT NOT NULL, data BLOB NOT NULL,"
                                 "uidx_u1 INTEGER NOT NULL, uidx_u2 INTEGER NOT NULL, "
                                 "FOREIGN KEY(from_pid) REFERENCES user_info(pid) ON UPDATE CASCADE ON DELETE CASCADE,"
                                 "FOREIGN KEY(to_pid) REFERENCES user_info(pid) ON UPDATE CASCADE ON DELETE CASCADE);");
            sqlCmds.emplace_back("CREATE TABLE blocks (pid INTEGER NOT NULL, blocked_pid INTEGER NOT NULL, created_at TEXT NOT NULL, "
                                 "game_key BLOB NOT NULL, PRIMARY KEY (pid, blocked_pid), "
                                 "FOREIGN KEY(pid) REFERENCES user_info(pid) ON UPDATE CASCADE ON DELETE CASCADE,"
                                 "FOREIGN KEY(blocked_pid) REFERENCES user_info(pid) ON UPDATE CASCADE ON DELETE CASCADE);");

            sqlCmds.emplace_back("CREATE UNIQUE INDEX unique_username ON user_info(username);");
            sqlCmds.emplace_back("CREATE UNIQUE INDEX unique_friendship ON friendships(uidx_u1, uidx_u2);");
            sqlCmds.emplace_back("CREATE UNIQUE INDEX unique_friend_request ON friend_requests(uidx_u1, uidx_u2);");

            sqlCmds.emplace_back("INSERT INTO sqlite_sequence (seq, name) VALUES (79999999, 'friend_requests');");
            sqlCmds.emplace_back("COMMIT;");
            break;
    }

    return runVoidCommandsSync(logger, db, sqlCmds, "ROLLBACK;");
}

bool migration_initial_boss(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<Database>& db, DBType type) {
    std::vector<std::string> sqlCmds;
    switch (type) {
        case DBType::SQLITE3:
            sqlCmds.emplace_back("BEGIN TRANSACTION;");
            sqlCmds.emplace_back("CREATE TABLE db_info (version TEXT);");
            sqlCmds.emplace_back("INSERT INTO db_info (version) VALUES ('0.0.1');");
            sqlCmds.emplace_back("CREATE TABLE settings (key TEXT NOT NULL, value TEXT NOT NULL, PRIMARY KEY (key));");
            sqlCmds.emplace_back("CREATE TABLE files (hash TEXT NOT NULL, data BLOB NOT NULL, PRIMARY KEY (hash));");
            sqlCmds.emplace_back("COMMIT;");
            break;
    }

    return runVoidCommandsSync(logger, db, sqlCmds, "ROLLBACK;");
}

bool migration_initial_management(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<Database>& db, DBType type) {
    std::vector<std::string> sqlCmds;
    switch (type) {
        case DBType::SQLITE3:
            sqlCmds.emplace_back("BEGIN TRANSACTION;");
            sqlCmds.emplace_back("CREATE TABLE db_info (version TEXT);");
            sqlCmds.emplace_back("INSERT INTO db_info (version) VALUES ('0.0.1');");
            sqlCmds.emplace_back("CREATE TABLE settings (key TEXT NOT NULL, value TEXT NOT NULL, PRIMARY KEY (key));");
            sqlCmds.emplace_back("CREATE TABLE pending_tasks (id INTEGER PRIMARY KEY AUTOINCREMENT, type INTEGER NOT NULL, "
                                 "params TEXT NOT NULL);");
            sqlCmds.emplace_back("COMMIT;");
            break;
    }

    return runVoidCommandsSync(logger, db, sqlCmds, "ROLLBACK;");
}

} // namespace db::migrations
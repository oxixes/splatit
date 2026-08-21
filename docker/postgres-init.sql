-- Each subsystem needs its own database. The initial migrations create tables
-- with the same names in several subsystems (db_info, settings, pending_tasks
-- and game_server_access), so pointing two subsystems at one database makes the
-- second migration fail.

CREATE DATABASE accounts        OWNER splatit;
CREATE DATABASE boss            OWNER splatit;
CREATE DATABASE management      OWNER splatit;
CREATE DATABASE friends_auth    OWNER splatit;
CREATE DATABASE friends_secure  OWNER splatit;
CREATE DATABASE splatoon_auth   OWNER splatit;
CREATE DATABASE splatoon_secure OWNER splatit;

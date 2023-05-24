#include "migrations.hpp"

namespace db::migrations {

bool migration_initial(Logger::Logger* logger, Database* db, type type) {
    return true;
}

}
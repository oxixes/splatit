#include "migrations.hpp"

namespace db::migrations {

bool migration_initial(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<Database>& db, type type) {
    return true;
}

}
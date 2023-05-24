#include "database.hpp"

#include <utility>

namespace db {

Database::Database(std::shared_ptr<Logger::Logger> logger) {
    this->logger = std::move(logger);
}

} // namespace db
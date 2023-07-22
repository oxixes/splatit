#ifndef SPLATOON_SERVER_BOSS_HPP
#define SPLATOON_SERVER_BOSS_HPP

#include <memory>
#include <filesystem>

#include <nlohmann/json.hpp>
#include <nlohmann/json-schema.hpp>

#include "../../logger.hpp"
#include "../../settingsManager.hpp"
#include "../parser/response.hpp"
#include "../parser/request.hpp"
#include "../server.hpp"

using json = nlohmann::json;
using json_validator = nlohmann::json_schema::json_validator;
namespace fs = std::filesystem;

namespace boss {

bool init(const std::shared_ptr<Logger::Logger>& logger, const std::shared_ptr<SettingsManager>& settingsMgr);

http::Response p01_tasksheet(const std::shared_ptr<Logger::Logger>& logger, const http::Request& req,
                             const std::string& titleId, const std::string& tasksheetId,
                             const std::shared_ptr<SettingsManager>& settingsMgr);
http::Response p01_data(const http::Request& req, const std::string& titleId, const std::string& tasksheetId,
                        const std::string& fileHash, const std::shared_ptr<SettingsManager>& settingsMgr);

http::Response getError(int status, http::Version version);

void registerRoutes(const std::shared_ptr<HTTP_Server>& server, const std::shared_ptr<SettingsManager>& settingsMgr);

} // namespace boss

#endif //SPLATOON_SERVER_BOSS_HPP

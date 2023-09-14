#ifndef SPLATOON_SERVER_TOOLS_HPP
#define SPLATOON_SERVER_TOOLS_HPP

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include <openssl/evp.h>

using json = nlohmann::json;

namespace crypto {

std::string base64Encode(const std::vector<uint8_t>& data);
std::string base64UrlEncode(const std::vector<uint8_t>& data);
std::vector<uint8_t> base64Decode(const std::string& data);
std::vector<uint8_t> base64UrlDecode(const std::string& data);

std::vector<uint8_t> HMAC_SHA256(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data);
std::vector<uint8_t> HMAC_MD5(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data);
std::vector<uint8_t> MD5(const std::vector<uint8_t>& data);
std::string signJWT(const std::string& base64Key, const json& payload);
bool verifyJWT(const std::string& base64Key, const std::string& jwt);

std::vector<uint8_t> genKey(size_t size = 32);

std::string genNintendoPasswordHash(uint32_t pid, const std::string& password);
std::string hashPassword(const std::string& password, const std::string& salt = "");
bool verifyPassword(const std::string& password, const std::string& hash);

bool verifyECDSASignature(const std::vector<uint8_t>& signature, const std::vector<uint8_t>& message, EVP_PKEY* publicKey);
EVP_PKEY* loadPublicKey(const std::string& publicKey);

} // namespace crypto

#endif //SPLATOON_SERVER_TOOLS_HPP

#ifndef SPLATOON_SERVER_TOOLS_HPP
#define SPLATOON_SERVER_TOOLS_HPP

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include <openssl/evp.h>

using json = nlohmann::json;

namespace crypto {

std::string base64Encode(const std::vector<unsigned char>& data);
std::string base64UrlEncode(const std::vector<unsigned char>& data);
std::vector<unsigned char> base64Decode(const std::string& data);
std::vector<unsigned char> base64UrlDecode(const std::string& data);

std::vector<unsigned char> HMAC_SHA256(const std::vector<unsigned char>& key, const std::vector<unsigned char>& data);
std::string signJWT(const std::string& base64Key, const json& payload);
bool verifyJWT(const std::string& base64Key, const std::string& jwt);

std::vector<unsigned char> genSHA256Key();

std::string genNintendoPasswordHash(uint32_t pid, const std::string& password);
std::string hashPassword(const std::string& password, const std::string& salt = "");
bool verifyPassword(const std::string& password, const std::string& hash);

bool verifyECDSASignature(const std::vector<unsigned char>& signature, const std::vector<unsigned char>& message, EVP_PKEY* publicKey);
bool verifyECDSASignature(const std::vector<unsigned char>& signature, const std::vector<unsigned char>& message,
                          const std::string& publicKey);

} // namespace crypto

#endif //SPLATOON_SERVER_TOOLS_HPP

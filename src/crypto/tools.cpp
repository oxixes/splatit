#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/kdf.h>
#include <openssl/pem.h>

#include "tools.hpp"
#include "../util/util.hpp"

namespace crypto {

std::string base64Encode(const std::vector<uint8_t>& data) {
    BIO* bmem = BIO_new(BIO_s_mem());
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bmem = BIO_push(b64, bmem);
    int writeResult = BIO_write(bmem, data.data(), (int) data.size());
    if (writeResult < 0) {
        BIO_free_all(bmem);
        throw std::runtime_error("Failed to encode base64");
    }
    BIO_flush(bmem);
    char* buffer;
    size_t length = BIO_get_mem_data(bmem, &buffer);
    std::string result(buffer, length);
    BIO_free_all(bmem);

    return result;
}

std::string base64UrlEncode(const std::vector<uint8_t>& data) {
    std::string result = base64Encode(data);
    int i = 0;
    for (char& c : result) {
        if (c == '+') {
            c = '-';
        } else if (c == '/') {
            c = '_';
        } else if (c == '=') {
            // Remove padding
            result.resize(i);
            break;
        }

        i++;
    }

    return result;
}

std::vector<uint8_t> base64Decode(const std::string& data) {
    BIO* bmem = BIO_new_mem_buf(data.data(), (int) data.size());
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bmem = BIO_push(b64, bmem);
    std::vector<uint8_t> result(data.size());
    int length = BIO_read(bmem, result.data(), (int) data.size());
    if (length < 0) {
        BIO_free_all(bmem);
        throw std::runtime_error("Failed to decode base64");
    }
    BIO_free_all(bmem);
    result.resize(length);

    return result;
}

std::vector<uint8_t> base64UrlDecode(const std::string& data) {
    std::string result = data;
    for (char& c : result) {
        if (c == '-') {
            c = '+';
        } else if (c == '_') {
            c = '/';
        }
    }

    // Add padding
    while (result.size() % 4 != 0) {
        result += '=';
    }

    return base64Decode(result);
}

std::vector<uint8_t> HMAC_SHA256(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> result(EVP_MAX_MD_SIZE);
    unsigned int length = 0;
    if (HMAC(EVP_sha256(), key.data(), (int) key.size(), data.data(), (int) data.size(), result.data(), &length) == nullptr)
        throw std::runtime_error("Failed to calculate HMAC: " + util::getOpenSSLError());
    result.resize(length);

    return result;
}

std::vector<uint8_t> HMAC_MD5(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> result(EVP_MAX_MD_SIZE);
    unsigned int length = 0;
    if (HMAC(EVP_md5(), key.data(), (int) key.size(), data.data(), (int) data.size(), result.data(), &length) == nullptr)
        throw std::runtime_error("Failed to calculate HMAC: " + util::getOpenSSLError());
    result.resize(length);

    return result;
}

std::vector<uint8_t> MD5(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> result(EVP_MAX_MD_SIZE);
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();

    if (!mdctx)
        throw std::runtime_error("Failed to create EVP_MD_CTX: " + util::getOpenSSLError());

    unsigned int length = 0;
    EVP_DigestInit_ex(mdctx, EVP_md5(), nullptr);
    EVP_DigestUpdate(mdctx, data.data(), data.size());
    if (EVP_DigestFinal_ex(mdctx, result.data(), &length) != 1) {
        EVP_MD_CTX_free(mdctx);
        throw std::runtime_error("Failed to calculate MD5: " + util::getOpenSSLError());
    }

    EVP_MD_CTX_free(mdctx);
    result.resize(length);

    return result;
}

std::string signJWT(const std::string& base64Key, const json& payload) {
    json header = {
        {"alg", "HS256"},
        {"typ", "JWT"}
    };

    std::string headerStr = header.dump();
    std::string payloadStr = payload.dump();

    std::vector<uint8_t> headerVec(headerStr.begin(), headerStr.end());
    std::vector<uint8_t> payloadVec(payloadStr.begin(), payloadStr.end());

    headerStr = base64UrlEncode(headerVec);
    payloadStr = base64UrlEncode(payloadVec);

    std::vector<uint8_t> jwt;
    jwt.insert(jwt.end(), headerStr.begin(), headerStr.end());
    jwt.push_back('.');
    jwt.insert(jwt.end(), payloadStr.begin(), payloadStr.end());

    std::vector<uint8_t> signature = HMAC_SHA256(base64Decode(base64Key), jwt);
    std::string signatureStr = base64UrlEncode(signature);
    jwt.push_back('.');
    jwt.insert(jwt.end(), signatureStr.begin(), signatureStr.end());

    std::string jwtStr(jwt.begin(), jwt.end());
    return jwtStr;
}

bool verifyJWT(const std::string& base64Key, const std::string& jwt) {
    // We will just sign with HS256 for now, so we ignore the header
    // and just check the signature, assuming it is HS256.

    if (jwt.find('.') == std::string::npos) {
        return false;
    }

    std::vector<uint8_t> signedData(jwt.begin(), jwt.begin() + (ssize_t) jwt.find_last_of('.'));
    std::vector<uint8_t> signature = base64UrlDecode(jwt.substr((ssize_t) jwt.find_last_of('.') + 1));

    std::vector<uint8_t> expectedSignature = HMAC_SHA256(base64Decode(base64Key), signedData);

    return signature == expectedSignature;
}

std::vector<uint8_t> genSHA256Key() {
    std::vector<uint8_t> key(32);
    int result = RAND_priv_bytes(key.data(), (int) key.size());
    if (result != 1) {
        throw std::runtime_error("Failed to generate SHA256 key: " + util::getOpenSSLError());
    }

    return key;
}

std::string genNintendoPasswordHash(uint32_t pid, const std::string& password) {
    util::getu32Little(pid);
    std::vector<uint8_t> data(4);
    // Get the bytes of pid and append them to data
    memcpy(data.data(), &pid, 4);

    std::vector<uint8_t> constant {0x02, 0x65, 0x43, 0x46};
    data.insert(data.end(), constant.begin(), constant.end());
    data.insert(data.end(), password.begin(), password.end());

    std::vector<uint8_t> hash(SHA256_DIGEST_LENGTH);
    SHA256(data.data(), data.size(), hash.data());

    // Convert to hex
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (uint8_t c : hash) {
        ss << std::setw(2) << (int) c;
    }

    return ss.str();
}

std::string genSalt() {
    std::vector<uint8_t> salt(32);
    int result = RAND_priv_bytes(salt.data(), (int) salt.size());
    if (result != 1) {
        throw std::runtime_error("Failed to generate salt: " + util::getOpenSSLError());
    }

    return base64Encode(salt);
}

std::string hashPassword(const std::string& password, const std::string& salt) {
    EVP_PKEY_CTX* pctx;
    pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_SCRYPT, nullptr);
    if (!pctx) {
        throw std::runtime_error("Failed to create EVP_PKEY_CTX: " + util::getOpenSSLError());
    }

    if (EVP_PKEY_derive_init(pctx) <= 0) {
        EVP_PKEY_CTX_free(pctx);
        throw std::runtime_error("Failed to initialize EVP_PKEY_CTX: " + util::getOpenSSLError());
    }

    if (EVP_PKEY_CTX_set1_pbe_pass(pctx, password.data(), (int) password.size()) <= 0) {
        EVP_PKEY_CTX_free(pctx);
        throw std::runtime_error("Failed to set password for EVP_PKEY_CTX: " + util::getOpenSSLError());
    }

    if (EVP_PKEY_CTX_set_scrypt_N(pctx, 16384) <= 0) {
        EVP_PKEY_CTX_free(pctx);
        throw std::runtime_error("Failed to set N for EVP_PKEY_CTX: " + util::getOpenSSLError());
    }

    if (EVP_PKEY_CTX_set_scrypt_r(pctx, 8) <= 0) {
        EVP_PKEY_CTX_free(pctx);
        throw std::runtime_error("Failed to set r for EVP_PKEY_CTX: " + util::getOpenSSLError());
    }

    if (EVP_PKEY_CTX_set_scrypt_p(pctx, 1) <= 0) {
        EVP_PKEY_CTX_free(pctx);
        throw std::runtime_error("Failed to set p for EVP_PKEY_CTX: " + util::getOpenSSLError());
    }

    std::string saltStr;
    if (salt.empty()) {
        saltStr = genSalt();
    } else {
        saltStr = salt;
    }

    if (EVP_PKEY_CTX_set1_scrypt_salt(pctx, (uint8_t*) saltStr.data(), (int) saltStr.size()) <= 0) {
        EVP_PKEY_CTX_free(pctx);
        throw std::runtime_error("Failed to set salt for EVP_PKEY_CTX: " + util::getOpenSSLError());
    }

    std::vector<uint8_t> key(64);
    size_t keySize = key.size();
    if (EVP_PKEY_derive(pctx, key.data(), &keySize) <= 0) {
        EVP_PKEY_CTX_free(pctx);
        throw std::runtime_error("Failed to derive key for EVP_PKEY_CTX: " + util::getOpenSSLError());
    }

    EVP_PKEY_CTX_free(pctx);
    key.resize(keySize);

    // Convert to hex
    std::stringstream ss;
    ss << saltStr << ';';
    ss << std::hex << std::setfill('0');
    for (uint8_t c : key) {
        ss << std::setw(2) << (int) c;
    }

    return ss.str();
}

bool verifyPassword(const std::string& password, const std::string& hash) {
    if (hash.find(';') == std::string::npos) {
        return false;
    }

    std::string salt = hash.substr(0, hash.find(';'));
    std::string newHash = hashPassword(password, salt);

    return newHash == hash;
}

bool verifyECDSASignature(const std::vector<uint8_t>& signature, const std::vector<uint8_t>& message, EVP_PKEY* publicKey) {
    ECDSA_SIG* sig = ECDSA_SIG_new();
    if (!sig) {
        throw std::runtime_error("Failed to create ECDSA_SIG: " + util::getOpenSSLError());
    }

    // OpenSSL expects the signature to be in DER format
    // but the signature is in raw format, so we need to
    // convert it
    BIGNUM* r = BN_bin2bn(signature.data(), 30, nullptr);
    BIGNUM* s = BN_bin2bn(signature.data() + 30, 30, nullptr);

    if (!r || !s) {
        ECDSA_SIG_free(sig);
        if (r) BN_free(r);
        if (s) BN_free(s);
        throw std::runtime_error("Failed to convert signature to BIGNUM: " + util::getOpenSSLError());
    }

    if (ECDSA_SIG_set0(sig, r, s) != 1) {
        ECDSA_SIG_free(sig);
        throw std::runtime_error("Failed to set ECDSA_SIG: " + util::getOpenSSLError());
    }

    uint8_t* derSignatureData = nullptr;
    int derSignatureLength = i2d_ECDSA_SIG(sig, &derSignatureData);
    if (derSignatureLength < 0) {
        ECDSA_SIG_free(sig);
        throw std::runtime_error("Failed to convert ECDSA_SIG to DER: " + util::getOpenSSLError());
    }

    ECDSA_SIG_free(sig);

    std::vector<uint8_t> derSignature(derSignatureData, derSignatureData + derSignatureLength);

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create EVP_MD_CTX: " + util::getOpenSSLError());
    }

    if (EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, publicKey) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize EVP_MD_CTX: " + util::getOpenSSLError());
    }

    int result = EVP_DigestVerify(ctx, derSignature.data(), derSignature.size(),
                                  message.data(), message.size());

    EVP_MD_CTX_free(ctx);

    return result == 1;
}

EVP_PKEY* loadPublicKey(const std::string& publicKey) {
    EVP_PKEY* pkey;
    BIO* bio = BIO_new_mem_buf((void*) publicKey.data(), (int) publicKey.size());
    if (!bio) {
        throw std::runtime_error("Failed to create BIO: " + util::getOpenSSLError());
    }

    pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);

    if (!pkey) {
        throw std::runtime_error("Failed to read public key: " + util::getOpenSSLError());
    }

    return pkey;
}

} // namespace crypto
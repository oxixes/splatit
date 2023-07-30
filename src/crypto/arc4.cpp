#include "arc4.hpp"

ARC4::ARC4(const std::vector<uint8_t>& key) : state(256), i(0), j(0) {
    for (int idx = 0; idx < 256; idx++) {
        state[idx] = idx;
    }

    int a = 0;
    for (int idx = 0; idx < 256; idx++) {
        a = (a + state[idx] + key[idx % key.size()]) % 256;
        std::swap(state[idx], state[a]);
    }
}

std::vector<uint8_t> ARC4::encDec(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> result(data.size());

    for (int idx = 0; idx < data.size(); idx++) {
        i = (i + 1) % 256;
        j = (j + state[i]) % 256;
        std::swap(state[i], state[j]);
        result[idx] = data[idx] ^ state[(state[i] + state[j]) % 256];
    }

    return result;
}

std::vector<uint8_t> ARC4::decrypt(const std::vector<uint8_t>& data) {
    return encDec(data);
}

std::vector<uint8_t> ARC4::encrypt(const std::vector<uint8_t>& data) {
    return encDec(data);
}
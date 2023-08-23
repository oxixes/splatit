#ifndef SPLATOON_SERVER_ARC4_HPP
#define SPLATOON_SERVER_ARC4_HPP

#include <vector>
#include <cstdint>

class ARC4 {
public:
    explicit ARC4(const std::vector<uint8_t>& key);
    ARC4(const ARC4& other) = default;
    ~ARC4() = default;

    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& data);
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& data);

private:
    std::vector<uint8_t> encDec(const std::vector<uint8_t>& data);

    std::vector<uint8_t> state;
    uint8_t i;
    uint8_t j;
};

#endif //SPLATOON_SERVER_ARC4_HPP

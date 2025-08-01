#ifndef SPLATOON_SERVER_STRING_HPP
#define SPLATOON_SERVER_STRING_HPP

#include <utility>

#include "../../rmc/types.hpp"
#include "../../../exceptions.hpp"

namespace nex::rmc {

    class String : public Type {
    public:
        explicit String(uint8_t minorVersion = 0, std::string value = "") : Type(minorVersion), value(std::move(value)) {};
        explicit String(std::string value) : Type(0), value(std::move(value)) {};
        String(const String& other) = default;
        String(String&& other) noexcept = default;

        ~String() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> data(value.size() + 3);

            auto length = (uint16_t) (value.size() + 1);
            data[0] = (uint8_t) length & 0xFF;
            data[1] = (uint8_t) (length >> 8) & 0xFF;
            for (int i = 0; i < value.size(); i++) data[i + 2] = (uint8_t) value.at(i);
            data[value.size() + 2] = 0x00;

            return data;
        }

        size_t decode(std::span<const uint8_t> data) override {
            if (data.size() < 2) throw MalformedException("Not enough data to decode String");

            auto length = (uint16_t) (data[0] | (data[1] << 8));

            if (data.size() < length + 2) throw MalformedException("Invalid String length");

            value.resize(length - 1);
            for (int i = 0; i < length - 1; i++) value.at(i) = (char) data[i + 2];

            return length + 2;
        }

        [[nodiscard]] std::string toString(int indentation = 0) const override { return "\"" + value + "\""; } // NOLINT(*-default-arguments)
        [[nodiscard]] std::string getName() const override { return "String"; }

        operator std::string&() { return value; }
        operator const std::string&() const { return value; }
        String& operator=(const std::string& other) { value = other; return *this; }
        String operator+(const std::string& other) const { return String(value + other); }
        String& operator+=(const std::string& other) { value += other; return *this; }
        bool operator==(const String& other) const { return value == other.value; }
        std::ostream& operator<<(std::ostream &os) const { os << value; return os; }
        auto operator<=>(const String& other) const { return value <=> other.value; };

        std::string::iterator begin() { return value.begin(); }
        std::string::iterator end() { return value.end(); }
        bool empty() const { return value.empty(); }

        String& operator=(const String& other) = default;
        String& operator=(String&& other) noexcept = default;

    private:
        std::string value;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_STRING_HPP
#pragma clang diagnostic push
#pragma ide diagnostic ignored "google-explicit-constructor"

#ifndef SPLATOON_SERVER_INTS_HPP
#define SPLATOON_SERVER_INTS_HPP

#include "../../rmc/types.hpp"
#include "../../../exceptions.hpp"

namespace nex::rmc {

    template <typename T>
    class Int : public Type {
    public:
        explicit Int(uint8_t minorVersion = 0, T value = 0) : Type(minorVersion), value(value) {};
        ~Int() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> data;

            union {
                T value;
                uint8_t bytes[sizeof(T)];
            } u;

            u.value = value;

            // Add the bytes in the correct order depending on the endianness of the system
            if constexpr (std::endian::native == std::endian::big) {
                for (int i = sizeof(T) - 1; i >= 0; i--) {
                    data.push_back(u.bytes[i]);
                }
            } else {
                for (int i = 0; i < sizeof(T); i++) {
                    data.push_back(u.bytes[i]);
                }
            }

            return data;
        }

        size_t decode(std::span<const uint8_t> data) override {
            if (data.size() < sizeof(T)) throw MalformedException("Not enough data to decode Int");

            union {
                T value;
                uint8_t bytes[sizeof(T)];
            } u;

            // Add the bytes in the correct order depending on the endianness of the system
            if constexpr (std::endian::native == std::endian::big) {
                for (int i = sizeof(T) - 1; i >= 0; i--) {
                    u.bytes[i] = data[sizeof(T) - 1 - i];
                }
            } else {
                for (int i = 0; i < sizeof(T); i++) {
                    u.bytes[i] = data[i];
                }
            }

            value = u.value;

            return sizeof(T);
        }

        // We define these operators so that we can use Int<T> as if it was a T
        operator T& () { return value; }
        operator const T& () const { return value; }
        Int<T>& operator=(const T& other) { value = other; return *this; }
        Int<T>& operator++() { value++; return *this; }
        Int<T> operator+(const T& other) const { return Int<T>(0, value + other); }
        Int<T> operator-(const T& other) const { return Int<T>(0, value - other); }
        Int<T> operator*(const T& other) const { return Int<T>(0, value * other); }
        Int<T> operator/(const T& other) const { return Int<T>(0, value / other); }
        Int<T> operator%(const T& other) const { return Int<T>(0, value % other); }
        Int<T> operator|(const T& other) const { return Int<T>(0, value | other); }
        Int<T> operator&(const T& other) const { return Int<T>(0, value & other); }
        Int<T> operator<<(const T& other) const { return Int<T>(0, value << other); }
        Int<T> operator>>(const T& other) const { return Int<T>(0, value >> other); }
        Int<T> operator~() const { return Int<T>(0, ~value); }
        Int<T> operator-() const { return Int<T>(0, -value); }
        bool operator==(const T& other) const { return value == other; }
        bool operator!=(const T& other) const { return value != other; }
        bool operator<(const T& other) const { return value < other; }
        bool operator>(const T& other) const { return value > other; }
        bool operator<=(const T& other) const { return value <= other; }
        bool operator>=(const T& other) const { return value >= other; }
        Int<T>& operator+=(const T& other) { value += other; return *this; }
        Int<T>& operator-=(const T& other) { value -= other; return *this; }
        Int<T>& operator*=(const T& other) { value *= other; return *this; }
        Int<T>& operator/=(const T& other) { value /= other; return *this; }
        Int<T>& operator%=(const T& other) { value %= other; return *this; }
        Int<T>& operator|=(const T& other) { value |= other; return *this; }
        Int<T>& operator&=(const T& other) { value &= other; return *this; }
        Int<T>& operator<<=(const T& other) { value <<= other; return *this; }
        Int<T>& operator>>=(const T& other) { value >>= other; return *this; }
        std::ostream& operator<<(std::ostream &os) const { return os << value; }

    private:
        T value;
    };

    typedef Int<int8_t> Int8;
    typedef Int<int16_t> Int16;
    typedef Int<int32_t> Int32;
    typedef Int<int64_t> Int64;
    typedef Int<uint8_t> UInt8;
    typedef Int<uint16_t> UInt16;
    typedef Int<uint32_t> UInt32;
    typedef Int<uint64_t> UInt64;

    typedef Int<bool> Bool;
    typedef Int<double> Double;

    typedef Int<uint32_t> PID;

} // namespace nex::rmc

#endif //SPLATOON_SERVER_INTS_HPP

#pragma clang diagnostic pop
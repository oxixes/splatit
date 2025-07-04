#ifndef SPLATOON_SERVER_VARIANT_HPP
#define SPLATOON_SERVER_VARIANT_HPP

#include <any>

#include "../../rmc/types.hpp"
#include "ints.hpp"
#include "string.hpp"
#include "datetime.hpp"

namespace nex::rmc {

    enum class VariantType {
        NONE,
        INT64,
        DOUBLE,
        BOOLEAN,
        STRING,
        DATETIME,
        UINT64
    };

    class Variant : public Type {
    public:
        explicit Variant(uint8_t minorVersion = 0) : Type(minorVersion) {}
        Variant(const Variant& other) = default;
        Variant(Variant&& other) noexcept = default;
        ~Variant() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> data(1);
            if (!value.has_value()) {
                data[0] = 0;
            } else if (value.type() == typeid(Int64)) {
                data[0] = 1;
                auto encoded = std::any_cast<Int64>(value).encode();
                data.insert(data.end(), encoded.begin(), encoded.end());
            } else if (value.type() == typeid(Double)) {
                data[0] = 2;
                auto encoded = std::any_cast<Double>(value).encode();
                data.insert(data.end(), encoded.begin(), encoded.end());
            } else if (value.type() == typeid(Bool)) {
                data[0] = 3;
                auto encoded = std::any_cast<Bool>(value).encode();
                data.insert(data.end(), encoded.begin(), encoded.end());
            } else if (value.type() == typeid(String)) {
                data[0] = 4;
                auto encoded = std::any_cast<String>(value).encode();
                data.insert(data.end(), encoded.begin(), encoded.end());
            } else if (value.type() == typeid(Datetime)) {
                data[0] = 5;
                auto encoded = std::any_cast<Datetime>(value).encode();
                data.insert(data.end(), encoded.begin(), encoded.end());
            } else if (value.type() == typeid(UInt64)) {
                data[0] = 6;
                auto encoded = std::any_cast<UInt64>(value).encode();
                data.insert(data.end(), encoded.begin(), encoded.end());
            } else {
                throw std::runtime_error("Invalid variant type");
            }

            return data;
        }

        size_t decode(std::span<const uint8_t> data) override {
            if (data.empty()) throw MalformedException("Not enough data to decode Variant");

            switch (data[0]) {
                case 0:
                    value = std::any();
                    return 1;
                case 1: {
                    Int64 i;
                    auto decoded_s = i.decode(data.subspan(1));
                    value = i;
                    return decoded_s + 1;
                }
                case 2: {
                    Double d;
                    auto decoded_s = d.decode(data.subspan(1));
                    value = d;
                    return decoded_s + 1;
                }
                case 3: {
                    Bool b;
                    auto decoded_s = b.decode(data.subspan(1));
                    value = b;
                    return decoded_s + 1;
                }
                case 4: {
                    String s;
                    auto decoded_s = s.decode(data.subspan(1));
                    value = s;
                    return decoded_s + 1;
                }
                case 5: {
                    Datetime d;
                    auto decoded_s = d.decode(data.subspan(1));
                    value = d;
                    return decoded_s + 1;
                }
                case 6: {
                    UInt64 u;
                    auto decoded_s = u.decode(data.subspan(1));
                    value = u;
                    return decoded_s + 1;
                }

                default:
                    throw MalformedException("Invalid Variant type");
            }
        }

        [[nodiscard]] std::string toString(int indentation = 0) const override { // NOLINT(*-default-arguments)
            std::string str = "Variant<";
            switch (getType()) {
                case VariantType::NONE:
                    str += "NONE";
                    break;
                case VariantType::INT64:
                    str += "Int64>(" + std::any_cast<Int64>(value).toString() + ")";
                    break;
                case VariantType::DOUBLE:
                    str += "Double>(" + std::any_cast<Double>(value).toString() + ")";
                    break;
                case VariantType::BOOLEAN:
                    str += "Bool>(" + std::any_cast<Bool>(value).toString() + ")";
                    break;
                case VariantType::STRING:
                    str += "String>(" + std::any_cast<String>(value).toString() + ")";
                    break;
                case VariantType::DATETIME:
                    str += "Datetime>(" + std::any_cast<Datetime>(value).toString() + ")";
                    break;
                case VariantType::UINT64:
                    str += "UInt64>(" + std::any_cast<UInt64>(value).toString() + ")";
                    break;
                default:
                    throw std::runtime_error("Invalid variant type");
            }
            return str;
        }

        [[nodiscard]] std::string getName() const override { return "Variant"; }

        template<typename T> requires std::derived_from<T, Type>
        void set(T v) {
            this->value = v;
        }

        template<typename T> requires std::derived_from<T, Type>
        T get() {
            return std::any_cast<T>(value);
        }

        [[nodiscard]] VariantType getType() const {
            if (!value.has_value()) return VariantType::NONE;
            if (value.type() == typeid(Int64)) return VariantType::INT64;
            if (value.type() == typeid(Double)) return VariantType::DOUBLE;
            if (value.type() == typeid(Bool)) return VariantType::BOOLEAN;
            if (value.type() == typeid(String)) return VariantType::STRING;
            if (value.type() == typeid(Datetime)) return VariantType::DATETIME;
            if (value.type() == typeid(UInt64)) return VariantType::UINT64;
            throw std::runtime_error("Invalid variant type");
        }

        bool operator== (const Variant& other) const {
            if (getType() != other.getType()) return false;
            switch (getType()) {
                case VariantType::NONE:
                    return true;
                case VariantType::INT64:
                    return std::any_cast<Int64>(value) == std::any_cast<Int64>(other.value);
                case VariantType::DOUBLE:
                    return std::any_cast<Double>(value) == std::any_cast<Double>(other.value);
                case VariantType::BOOLEAN:
                    return std::any_cast<Bool>(value) == std::any_cast<Bool>(other.value);
                case VariantType::STRING:
                    return std::any_cast<String>(value) == std::any_cast<String>(other.value);
                case VariantType::DATETIME:
                    return std::any_cast<Datetime>(value) == std::any_cast<Datetime>(other.value);
                case VariantType::UINT64:
                    return std::any_cast<UInt64>(value) == std::any_cast<UInt64>(other.value);
                default:
                    throw std::runtime_error("Invalid variant type");
            }
        }

        Variant& operator=(const Variant& other) = default;
        Variant& operator=(Variant&& other) noexcept = default;

    private:
        std::any value;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_VARIANT_HPP

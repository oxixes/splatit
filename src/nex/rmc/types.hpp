#ifndef SPLATOON_SERVER_TYPES_HPP
#define SPLATOON_SERVER_TYPES_HPP

#include <memory>
#include <cstdint>
#include <vector>
#include <iostream>
#include <span>

#include <iomanip>

namespace nex::rmc {

#define INDENTATION_SPACES 4

class Type {
public:
    explicit Type(uint8_t minorVersion) : minorVersion(minorVersion) {};
    virtual ~Type() = default;

    [[nodiscard]] virtual std::vector<uint8_t> encode() const = 0;
    virtual size_t decode(std::span<const uint8_t> data) = 0;

    [[nodiscard]] virtual std::string toString(int indentation = 0) const { // NOLINT(*-default-arguments)
        return "TYPE TO STRING NOT IMPLEMENTED";
    }
    [[nodiscard]] virtual std::string toString(bool includeTypeName, int indentation = 0) const { // NOLINT(*-default-arguments)
        return "TYPE TO STRING NOT IMPLEMENTED";
    }
    [[nodiscard]] virtual std::string getName() const {
        return "TYPE TO STRING NOT IMPLEMENTED";
    }

protected:
    uint8_t minorVersion = 0;
};

typedef std::shared_ptr<Type> T_ptr;
//template <typename A> requires std::derived_from<A, Type> using T = std::shared_ptr<A>;

// We define a class that, given the types that we expect to receive, will parse the data
// and return a vector of the types that we expect.
template<typename... Types> requires (std::is_base_of_v<Type, Types> && ...)
class ParamParser {
public:
    ParamParser() = delete;
    ~ParamParser() = delete;

    static std::vector<T_ptr> decode(uint8_t minorVersion, std::vector<uint8_t> data) {
        std::vector<T_ptr> params;

        parse(std::make_index_sequence<sizeof...(Types)>{}, minorVersion, data, params);

        return params;
    }

    static std::vector<uint8_t> encode(const std::vector<T_ptr>& params) {
        std::vector<uint8_t> data;

        for (const auto& param : params) {
            auto encoded = param->encode();
            data.insert(data.end(), encoded.begin(), encoded.end());
        }

        return data;
    }

private:
    template<std::size_t... I>
    static void parse(std::index_sequence<I...>, uint8_t minorVersion, std::vector<uint8_t>& data, std::vector<T_ptr>& params) {
        (parseAndErase<std::tuple_element_t<I, std::tuple<Types...>>>(minorVersion, data, params), ...);
    }

    template<typename T> requires std::derived_from<T, Type>
    static size_t decodeParam(uint8_t minorVersion, const std::vector<uint8_t>& data, std::vector<T_ptr>& params) {
        auto param = std::make_shared<T>(minorVersion);
        size_t size = param->decode(data);
        params.push_back(std::move(param));
        return size;
    }

    template<typename T> requires std::derived_from<T, Type>
    static void parseAndErase(uint8_t minorVersion, std::vector<uint8_t>& data, std::vector<T_ptr>& params) {
        size_t size = decodeParam<T>(minorVersion, data, params);
        data.erase(data.begin(), data.begin() + (ssize_t) size);
    }
};

} // namespace nex::rmc

#endif //SPLATOON_SERVER_TYPES_HPP

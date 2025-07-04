#ifndef RESULTRANGE_H
#define RESULTRANGE_H

#include "../common/structure.hpp"

namespace nex::rmc {

    class ResultRange : public Structure {
    public:
        explicit ResultRange(uint8_t minorVersion) : Structure(minorVersion) {};
        ResultRange(const ResultRange& other) = default;
        ResultRange(ResultRange&& other) noexcept = default;
        ~ResultRange() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::vector<uint8_t> data;

            auto offsetData = offset.encode();
            data.insert(data.end(), offsetData.begin(), offsetData.end());
            auto lengthData = length.encode();
            data.insert(data.end(), lengthData.begin(), lengthData.end());

            auto result = encodeHeader(RESULT_RANGE_VERSION, data.size());
            result.insert(result.end(), data.begin(), data.end());

            return result;
        }

        size_t decode(std::span<const uint8_t> data) override {
            size_t size = 0;

            size += decodeHeader(data, RESULT_RANGE_VERSION);
            size += offset.decode(data.subspan(size));
            size += length.decode(data.subspan(size));

            return size;
        }

        [[nodiscard]] std::string toString(int indentation = 0) const override { // NOLINT(*-default-arguments)
            std::string indent = std::string((indentation + 1) * INDENTATION_SPACES, ' ');
            std::string last_indent = std::string(indentation * INDENTATION_SPACES, ' ');
            std::string result = "ResultRange {\n";
            result += indent + "offset: " + offset.toString(true, indentation + 1) + "\n";
            result += indent + "length: " + length.toString(true, indentation + 1) + "\n";
            result += last_indent + "}";
            return result;
        }

        [[nodiscard]] std::string getName() const override { return "ResultRange"; }

        ResultRange& operator=(const ResultRange& other) = default;
        ResultRange& operator=(ResultRange&& other) noexcept = default;

        UInt32 offset;
        UInt32 length;

    private:
        const inline static uint8_t RESULT_RANGE_VERSION = 0;
    };

} // namespace nex::rmc

#endif //RESULTRANGE_H

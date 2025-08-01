#ifndef SPLATOON_SERVER_DATETIME_HPP
#define SPLATOON_SERVER_DATETIME_HPP

#include <chrono>

#include "../../rmc/types.hpp"
#include "../../../util/util.hpp"
#include "../../../exceptions.hpp"

using time_point = std::chrono::system_clock::time_point;

namespace nex::rmc {

    class Datetime : public Type {
    public:
        explicit Datetime(uint8_t minorVersion = 0, time_point t = std::chrono::system_clock::now()) :
            Type(minorVersion), value(t) {}
        explicit Datetime(time_point t) : Type(0), value(t) {}
        Datetime(const Datetime& other) = default;
        Datetime(Datetime&& other) noexcept = default;
        ~Datetime() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            uint64_t datetime = 0;
            auto ymd = std::chrono::year_month_day(floor<std::chrono::days>(value));
            auto hhmmss = std::chrono::hh_mm_ss(floor<std::chrono::milliseconds>(value - floor<std::chrono::days>(value)));
            // Bits 63-26 are the year
            datetime |= (uint64_t) ((int) ymd.year()) << 26;
            // Bits 25-22 are the month
            datetime |= (uint64_t) ((unsigned int) ymd.month()) << 22;
            // Bits 21-17 are the day
            datetime |= (uint64_t) ((unsigned int) ymd.day()) << 17;
            // Bits 16-12 are the hour
            datetime |= (uint64_t) ((unsigned int) hhmmss.hours().count()) << 12;
            // Bits 11-6 are the minute
            datetime |= (uint64_t) ((unsigned int) hhmmss.minutes().count()) << 6;
            // Bits 5-0 are the second
            datetime |= (uint64_t) ((unsigned int) hhmmss.seconds().count());

            std::vector<uint8_t> data;
            util::getu64Little(datetime);
            data.insert(data.end(), (uint8_t*) &datetime, (uint8_t*) &datetime + sizeof(uint64_t));

            return data;
        }

        size_t decode(std::span<const uint8_t> data) override {
            if (data.size() < sizeof(uint64_t)) throw MalformedException("Not enough data to decode Datetime");

            uint64_t datetime;
            memcpy(&datetime, data.data(), sizeof(uint64_t));
            util::getu64Little(datetime);

            auto year = (int) (datetime >> 26);
            auto month = (unsigned int) ((datetime >> 22) & 0b1111);
            auto day = (unsigned int) ((datetime >> 17) & 0b11111);
            auto hour = (int) ((datetime >> 12) & 0b11111);
            auto minute = (int) ((datetime >> 6) & 0b111111);
            auto second = (int) (datetime & 0b111111);

            // Create a time_point with the decoded time in UTC
            std::chrono::year_month_day date{std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}};
            auto time = std::chrono::hours{hour} + std::chrono::minutes{minute} + std::chrono::seconds{second};
            value = std::chrono::sys_days{date} + time;

            return sizeof(uint64_t);
        }

        [[nodiscard]] std::string toString(int indentation = 0) const override { // NOLINT(*-default-arguments)
            // Convert the time_point to a readable string
            if (value.time_since_epoch().count() < 0) return "N/A";

            auto t = std::chrono::system_clock::to_time_t(value);
            auto tm = *std::gmtime(&t);
            char buf[80];
            strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M:%S", &tm);
            return buf;
        }

        [[nodiscard]] std::string getName() const override { return "Datetime"; }

        operator time_point&() { return value; }
        operator const time_point&() const { return value; }
        Datetime& operator=(const time_point& other) { value = other; return *this; }
        bool operator==(const time_point& other) const { return value == other; }
        bool operator!=(const time_point& other) const { return value != other; }
        bool operator<(const time_point& other) const { return value < other; }
        bool operator>(const time_point& other) const { return value > other; }
        bool operator<=(const time_point& other) const { return value <= other; }
        bool operator>=(const time_point& other) const { return value >= other; }

        Datetime& operator=(const Datetime& other) = default;
        Datetime& operator=(Datetime&& other) noexcept = default;

    private:
        time_point value;
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_DATETIME_HPP
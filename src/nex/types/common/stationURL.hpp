#ifndef SPLATOON_SERVER_STATIONURL_HPP
#define SPLATOON_SERVER_STATIONURL_HPP

#include <optional>

#include "../../../socket/socket.hpp"
#include "../../../util/util.hpp"
#include "../../rmc/types.hpp"
#include "string.hpp"

namespace nex::rmc {

    enum class Protocol {
        UDP,
        PRUDP,
        PRUDPS
    };

    class StationURL : public Type {
    public:
        explicit StationURL(uint8_t minorVersion = 0) : Type(minorVersion) {};
        ~StationURL() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            std::string url;

            if (empty) {
                url = "";
            } else {
                if (proto == Protocol::UDP) {
                    url = "udp:/";
                } else if (proto == Protocol::PRUDP) {
                    url = "prudp:/";
                } else if (proto == Protocol::PRUDPS) {
                    url = "prudps:/";
                }

                url += "address=" + util::ipv4ToString(ip) + ";";
                url += "port=" + std::to_string(ip.port) + ";";

                if (Pl.has_value()) url += "Pl=" + std::to_string(Pl.value()) + ";";
                if (stream.has_value()) url += "stream=" + std::to_string(stream.value()) + ";";
                if (sid.has_value()) url += "sid=" + std::to_string(sid.value()) + ";";
                if (CID.has_value()) url += "CID=" + std::to_string(CID.value()) + ";";
                if (PID.has_value()) url += "PID=" + std::to_string(PID.value()) + ";";
                if (type.has_value()) url += "type=" + std::to_string(type.value()) + ";";
                if (RVCID.has_value()) url += "RVCID=" + std::to_string(RVCID.value()) + ";";
                if (natm.has_value()) url += "natm=" + std::to_string(natm.value()) + ";";
                if (natf.has_value()) url += "natf=" + std::to_string(natf.value()) + ";";
                if (upnp.has_value()) url += "upnp=" + std::to_string(upnp.value()) + ";";
                if (pmp.has_value()) url += "pmp=" + std::to_string(pmp.value()) + ";";

                url.pop_back(); // Remove the last semicolon
            }

            String str(minorVersion, url);
            return str.encode();
        }

        size_t decode(std::span<const uint8_t> data) override {
            String str;
            size_t size = str.decode(data);

            std::string url = std::move(str);

            if (url.empty()) {
                empty = true;
                return size;
            }

            if (url.find("udp:/") == 0) {
                proto = Protocol::UDP;
                url = url.substr(5);
            } else if (url.find("prudp:/") == 0) {
                proto = Protocol::PRUDP;
                url = url.substr(7);
            } else if (url.find("prudps:/") == 0) {
                proto = Protocol::PRUDPS;
                url = url.substr(8);
            } else {
                throw MalformedException("Invalid protocol");
            }

            std::string_view urlView(url);

            bool hasAddress = false;
            bool hasPort = false;
            while (!urlView.empty()) {
                auto pos = urlView.find(';');
                if (pos == std::string::npos) pos = urlView.size();

                auto param = urlView.substr(0, pos);
                urlView = urlView.substr(pos + 1);

                auto eqPos = param.find('=');
                if (eqPos == std::string::npos) throw MalformedException("Invalid parameter");

                auto key = param.substr(0, eqPos);
                auto value = param.substr(eqPos + 1);

                if (key == "address") {
                    struct sockaddr_in addr{};
                    inet_pton(AF_INET, std::string(value).c_str(), &addr.sin_addr);

                    ip.a = addr.sin_addr.S_un.S_un_b.s_b1;
                    ip.b = addr.sin_addr.S_un.S_un_b.s_b2;
                    ip.c = addr.sin_addr.S_un.S_un_b.s_b3;
                    ip.d = addr.sin_addr.S_un.S_un_b.s_b4;

                    hasAddress = true;
                } else if (key == "port") {
                    ip.port = std::stoi(std::string(value));
                    hasPort = true;
                } else if (key == "stream") {
                    stream = std::stoi(std::string(value));
                } else if (key == "sid") {
                    sid = std::stoi(std::string(value));
                } else if (key == "CID") {
                    CID = std::stoi(std::string(value));
                } else if (key == "PID") {
                    PID = std::stoi(std::string(value));
                } else if (key == "type") {
                    type = std::stoi(std::string(value));
                } else if (key == "RVCID") {
                    RVCID = std::stoi(std::string(value));
                } else if (key == "natm") {
                    natm = std::stoi(std::string(value));
                } else if (key == "natf") {
                    natf = std::stoi(std::string(value));
                } else if (key == "upnp") {
                    upnp = std::stoi(std::string(value));
                } else if (key == "pmp") {
                    pmp = std::stoi(std::string(value));
                } else if (key == "Pl") {
                    Pl = std::stoi(std::string(value));
                } else {
                    throw MalformedException("Invalid parameter in StationURL: " + std::string(key));
                }
            }

            if (!hasAddress || !hasPort) throw MalformedException("Missing address or port in StationURL");

            return size;
        }

        Protocol proto = Protocol::PRUDP;
        sock::IPv4Addr ip{};
        std::optional<uint8_t> stream;
        std::optional<uint8_t> sid;
        std::optional<uint32_t> CID;
        std::optional<uint32_t> PID;
        std::optional<uint8_t> type;
        std::optional<uint32_t> RVCID;
        std::optional<uint8_t> natm;
        std::optional<uint8_t> natf;
        std::optional<bool> upnp;
        std::optional<bool> pmp;
        std::optional<uint8_t> Pl;

        bool empty = false;

        /* There are more parameters, but they haven't been seen, so I'm unsure if
         * they will ever appear on this game. */
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_STATIONURL_HPP

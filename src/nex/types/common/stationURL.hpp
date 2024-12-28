#ifndef SPLATOON_SERVER_STATIONURL_HPP
#define SPLATOON_SERVER_STATIONURL_HPP

#include <optional>

#include "../../../socket/socket.hpp"
#include "../../../util/util.hpp"
#include "../../rmc/types.hpp"
#include "string.hpp"

namespace nex::rmc {

    enum class Protocol {
        NONE,
        UDP,
        PRUDP,
        PRUDPS
    };

    class StationURL : public Type {
    public:
        explicit StationURL(uint8_t minorVersion = 0) : Type(minorVersion) {};
        ~StationURL() override = default;

        [[nodiscard]] std::vector<uint8_t> encode() const override {
            String str(minorVersion, encodeString());
            return str.encode();
        }

        size_t decode(std::span<const uint8_t> data) override {
            String str;
            size_t size = str.decode(data);

            std::string url = std::move(str);

            if (url.empty()) {
                proto = Protocol::NONE;
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
                throw MalformedException("Invalid protocol: " + url);
            }

            std::string_view urlView(url);

            while (!urlView.empty()) {
                auto pos = urlView.find(';'); // It may return npos, which is fine

                auto param = urlView.substr(0, pos);
                if (pos != std::string::npos) urlView = urlView.substr(pos + 1);
                else urlView = "";

                auto eqPos = param.find('=');
                if (eqPos == std::string::npos) throw MalformedException("Invalid parameter");

                auto key = param.substr(0, eqPos);
                auto value = param.substr(eqPos + 1);

                try {
                    if (key == "address") {
                        if (!ip.has_value()) ip = sock::IPv4Addr();
                        struct sockaddr_in addr{};
                        if (inet_pton(AF_INET, std::string(value).c_str(), &addr.sin_addr) != 1)
                            throw MalformedException("Invalid address in StationURL");

#ifdef _WIN32
                        ip->a = addr.sin_addr.S_un.S_un_b.s_b1;
                        ip->b = addr.sin_addr.S_un.S_un_b.s_b2;
                        ip->c = addr.sin_addr.S_un.S_un_b.s_b3;
                        ip->d = addr.sin_addr.S_un.S_un_b.s_b4;
#else
                        ip->a = addr.sin_addr.s_addr & 0xFF;
                        ip->b = (addr.sin_addr.s_addr >> 8) & 0xFF;
                        ip->c = (addr.sin_addr.s_addr >> 16) & 0xFF;
                        ip->d = (addr.sin_addr.s_addr >> 24) & 0xFF;
#endif
                    } else if (key == "port") {
                        port = std::stoi(std::string(value));
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
                    } else if (key == "probeinit") {
                        probeinit = std::stoi(std::string(value));
                    } else if (key == "upnp") {
                        upnp = std::stoi(std::string(value));
                    } else if (key == "pmp") {
                        pmp = std::stoi(std::string(value));
                    } else if (key == "Pl") {
                        Pl = std::stoi(std::string(value));
                    } else {
                        throw MalformedException("Invalid parameter in StationURL: " + std::string(key));
                    }
                } catch (std::invalid_argument& e) {
                    throw MalformedException("Invalid parameter value in StationURL: " + std::string(key));
                } catch (std::out_of_range& e) {
                    throw MalformedException("Parameter value out of range in StationURL: " + std::string(key));
                }
            }

            return size;
        }

        Protocol proto = Protocol::NONE;
        std::optional<sock::IPv4Addr> ip;
        std::optional<uint16_t> port;
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
        std::optional<bool> probeinit;
        std::optional<uint8_t> Pl;

        /* There are more parameters, but they haven't been seen, so I'm unsure if
         * they will ever appear on this game. */

        bool operator== (const StationURL& other) const { return encodeString() == other.encodeString(); }

    private:
        std::string encodeString() const {
            std::string url;

            if (proto == Protocol::NONE) {
                url = "";
            } else {
                if (proto == Protocol::UDP) {
                    url = "udp:/";
                } else if (proto == Protocol::PRUDP) {
                    url = "prudp:/";
                } else if (proto == Protocol::PRUDPS) {
                    url = "prudps:/";
                }

                if (ip.has_value()) url += "address=" + util::ipv4ToString(ip.value()) + ";";
                if (port.has_value()) url += "port=" + std::to_string(port.value()) + ";";
                if (Pl.has_value()) url += "Pl=" + std::to_string(Pl.value()) + ";";
                if (stream.has_value()) url += "stream=" + std::to_string(stream.value()) + ";";
                if (sid.has_value()) url += "sid=" + std::to_string(sid.value()) + ";";
                if (CID.has_value()) url += "CID=" + std::to_string(CID.value()) + ";";
                if (PID.has_value()) url += "PID=" + std::to_string(PID.value()) + ";";
                if (type.has_value()) url += "type=" + std::to_string(type.value()) + ";";
                if (RVCID.has_value()) url += "RVCID=" + std::to_string(RVCID.value()) + ";";
                if (natm.has_value()) url += "natm=" + std::to_string(natm.value()) + ";";
                if (natf.has_value()) url += "natf=" + std::to_string(natf.value()) + ";";
                if (probeinit.has_value()) url += "probeinit=" + std::to_string(probeinit.value()) + ";";
                if (upnp.has_value()) url += "upnp=" + std::to_string(upnp.value()) + ";";
                if (pmp.has_value()) url += "pmp=" + std::to_string(pmp.value()) + ";";

                url.pop_back(); // Remove the last semicolon
            }

            return url;
        }
    };

} // namespace nex::rmc

#endif //SPLATOON_SERVER_STATIONURL_HPP

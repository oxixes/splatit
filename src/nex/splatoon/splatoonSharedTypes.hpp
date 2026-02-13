#ifndef SPLATOON_SERVER_SPLATOONSHAREDTYPES_HPP
#define SPLATOON_SERVER_SPLATOONSHAREDTYPES_HPP

#include <memory>
#include <set>
#include <vector>

#include "../rmc/server.hpp"
#include "../types/common/stationURL.hpp"
#include "../types/splatoonSecure/gathering.hpp"
#include "../types/splatoonSecure/matchmakeSession.hpp"

namespace nex::rmc {

struct NATProperties {
    uint32_t mapping = 0;
    uint32_t filtering = 0;
    uint32_t rtt = 0;
};

struct SplatoonRegisteredClientInfo {
    ClientInfo client;
    std::vector<StationURL> urls;
    StationURL publicUrl;
    uint32_t rvConnId = 0;
    std::vector<std::shared_ptr<Gathering>> joinedGatherings = {};
    NATProperties lastReportedNATProperties;
};

struct SessionInfo {
    std::shared_ptr<MatchmakeSession> session;
    std::set<uint32_t> players;
};

} // namespace nex::rmc

#endif //SPLATOON_SERVER_SPLATOONSHAREDTYPES_HPP

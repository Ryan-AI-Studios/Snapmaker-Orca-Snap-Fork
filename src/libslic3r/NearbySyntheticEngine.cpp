#include "NearbySyntheticEngine.hpp"

namespace Slic3r {

namespace {
constexpr const char *kSyntheticEnginePrefix = "nearby-";
} // namespace

std::string nearby_synthetic_engine_id(uint64_t serial)
{
    return std::string(kSyntheticEnginePrefix) + std::to_string(serial);
}

bool nearby_is_synthetic_engine_id(const std::string &id)
{
    const std::string prefix(kSyntheticEnginePrefix);
    if (id.size() <= prefix.size() || id.compare(0, prefix.size(), prefix) != 0)
        return false;
    for (size_t i = prefix.size(); i < id.size(); ++i) {
        if (id[i] < '0' || id[i] > '9')
            return false;
    }
    return true;
}

nlohmann::json nearby_synthetic_device_entry(const std::string &identity, const std::string &ip,
                                             const std::string &name, double nozzle_diameter,
                                             int protocol)
{
    // Only the fields the row and the bind actually determine. Everything else
    // (preset_name, img, port, auth) is filled by the page's writer, exactly as
    // it does for a device the page itself reported.
    nlohmann::json device;
    device["dev_id"]       = identity;
    device["sn"]           = identity;
    device["ip"]           = ip;
    device["dev_name"]     = name;
    device["model_name"]   = name;
    device["connected"]    = true;
    device["link_mode"]    = "lan";
    device["nozzle_sizes"] = nlohmann::json::array({nozzle_diameter});
    device["protocol"]     = protocol;
    // Present and empty on purpose: the merge only overwrites keys it is given,
    // so a stale session handle in an existing record would otherwise survive.
    device["id"] = "";
    return device;
}

} // namespace Slic3r

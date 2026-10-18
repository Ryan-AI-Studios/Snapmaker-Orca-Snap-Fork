#include "libslic3r/NearbyDiscovery.hpp"

#include <cstddef>
#include <utility>

namespace Slic3r {

namespace {

/// TXT value for `key`, or `fallback` when the key is absent or empty.
std::string txt_or(const NearbyReply &reply, const char *key, const std::string &fallback)
{
    const auto it = reply.txt.find(key);
    return (it != reply.txt.end() && !it->second.empty()) ? it->second : fallback;
}

/// SRV target without the mDNS ".local" suffix (and without the trailing dot).
std::string host_without_local(const std::string &hostname)
{
    const size_t pos = hostname.find(".local");
    return pos == std::string::npos ? hostname : hostname.substr(0, pos);
}

/// Cover URL for `machine_type`, or empty when there is nothing to point at.
/// Mirrors the firmware convention "<base>/profiles/<vendor>/<machine_type>_cover.png",
/// where the vendor is the first word of the machine type.
std::string cover_url(const std::string &base_url, const std::string &machine_type)
{
    if (base_url.empty())
        return std::string();

    const size_t vendor_pos = machine_type.find_first_of(' ');
    if (vendor_pos == std::string::npos)
        return std::string();

    const std::string vendor = machine_type.substr(0, vendor_pos);
    return base_url + "/profiles/" + vendor + "/" + machine_type + "_cover.png";
}

} // namespace

std::string nearby_identity(const NearbyReply &reply, const std::string &unique_key)
{
    const std::string unique = txt_or(reply, unique_key.c_str(), std::string());
    if (!unique.empty())
        return unique;

    if (!reply.instance_name.empty())
        return reply.instance_name;

    return reply.address;
}

NearbyRow nearby_row_from_reply(const NearbyReply &reply, const NearbyRowPolicy &policy,
                                const std::string &cover_base_url)
{
    NearbyRow row;
    row.list_key         = nearby_identity(reply, policy.unique_key);
    row.derived_identity = txt_or(reply, policy.unique_key.c_str(), std::string()).empty();

    // Display name. The SRV target wins over the instance name so that every
    // `_snapmaker._tcp` row keeps the label it has today; the instance name and
    // then the IP are fallbacks for services whose target is missing.
    std::string name = host_without_local(reply.hostname);
    if (name.empty())
        name = reply.instance_name;
    if (name.empty())
        name = reply.address;

    nlohmann::json data;
    data["name"] = name;
    data["ip"]   = reply.address;
    data["port"] = reply.port;

    // `txt_ip` is the address the row displays in preference to `ip`. The
    // `snapmaker` reply keeps the key unconditionally, empty when not advertised;
    // a service with a minimal TXT set reports the responder address instead.
    data["txt_ip"] = txt_or(reply, "ip", policy.supply_missing_fields ? reply.address : std::string());

    // A reply that arrived over mDNS is a LAN device even when the service says
    // nothing about its link mode.
    if (const std::string link_mode = txt_or(reply, "link_mode", std::string()); !link_mode.empty())
        data["link_mode"] = link_mode;
    else if (policy.supply_missing_fields)
        data["link_mode"] = "lan";

    // `protocol` is not in any engine's current allow-list; the mapping is kept
    // because it is part of the row vocabulary the firmware TXT set describes.
    if (const std::string protocol = txt_or(reply, "protocol", std::string()); !protocol.empty())
        data["protocol"] = protocol;
    if (const std::string version = txt_or(reply, "version", std::string()); !version.empty())
        data["pro_version"] = version;
    if (const std::string userid = txt_or(reply, "userid", std::string()); !userid.empty())
        data["userid"] = userid;

    if (const std::string device_name = txt_or(reply, "device_name", std::string()); !device_name.empty())
        data["device_name"] = device_name;
    else if (policy.supply_missing_fields)
        data["device_name"] = name;

    if (const std::string region = txt_or(reply, "region", std::string()); !region.empty())
        data["region"] = region;

    // The identity is written to the engine's own field as well as `sn`, because
    // the list insert keys on `sn` while the engine field names the service.
    if (!policy.unique_key.empty()) {
        data["unique_key"]           = policy.unique_key;
        data["unique_value"]         = row.list_key;
        data[policy.unique_key]      = row.list_key;
    }
    data["sn"] = row.list_key;

    // Services such as Moonraker advertise no model label; the engine supplies
    // one so the row is identifiable and the cover convention has something to
    // work with.
    const std::string machine_type = txt_or(reply, "machine_type", policy.default_machine_type);
    if (!machine_type.empty()) {
        data["machine_type"] = machine_type;
        if (const std::string cover = cover_url(cover_base_url, machine_type); !cover.empty())
            data["cover"] = cover;
    }

    row.data = std::move(data);
    return row;
}

namespace {

/// Dotted IPv4 -> 4 octets. Returns false for anything that is not exactly
/// four decimal octets in 0..255 (no leading-space or suffix tolerance).
bool parse_ipv4(const std::string &ip, unsigned char out[4])
{
    int      part       = 0;
    unsigned value      = 0;
    bool     digit_seen = false;
    for (const char c : ip) {
        if (c == '.') {
            if (!digit_seen || part >= 3 || value > 255)
                return false;
            out[part++] = static_cast<unsigned char>(value);
            value       = 0;
            digit_seen  = false;
        } else if (c >= '0' && c <= '9') {
            value = value * 10 + static_cast<unsigned>(c - '0');
            if (value > 255)
                return false;
            digit_seen = true;
        } else {
            return false;
        }
    }
    if (!digit_seen || part != 3 || value > 255)
        return false;
    out[part] = static_cast<unsigned char>(value);
    return true;
}

} // namespace

bool nearby_is_usable_lan_ipv4(const std::string &ip)
{
    unsigned char o[4];
    if (!parse_ipv4(ip, o))
        return false;
    if (o[0] == 127)                                      // loopback
        return false;
    if (o[0] == 0 && o[1] == 0 && o[2] == 0 && o[3] == 0) // unspecified
        return false;
    if (o[0] == 169 && o[1] == 254)                       // link-local
        return false;
    return true;
}

std::string nearby_choose_address(const std::string &a_record, const std::string &packet_source)
{
    if (nearby_is_usable_lan_ipv4(a_record))
        return a_record;
    if (nearby_is_usable_lan_ipv4(packet_source))
        return packet_source;
    return a_record.empty() ? packet_source : a_record;
}

} // namespace Slic3r

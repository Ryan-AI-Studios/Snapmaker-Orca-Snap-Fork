#include "libslic3r/NearbyDiscovery.hpp"

#include <cstddef>
#include <mutex>
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

    // The model the engine knows for this service, if any. Computed here because
    // it can also stand in for the display name (see below).
    const std::string machine_type = txt_or(reply, "machine_type", policy.default_machine_type);

    // Display name. The SRV target wins over the instance name so that every
    // `_snapmaker._tcp` row keeps the label it has today; the instance name and
    // then the IP are fallbacks for services whose target is missing. An engine
    // whose service advertises no human label (Moonraker: the SRV target is the
    // printer's hostname) opts in to being named after the model instead.
    std::string name = host_without_local(reply.hostname);
    if (name.empty())
        name = reply.instance_name;
    if (name.empty())
        name = reply.address;
    if (policy.name_from_machine_type && !machine_type.empty())
        name = machine_type;

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
    // work with. (`machine_type` itself was resolved above, where it may also
    // have supplied the display name.)
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

namespace {

/// Registry of the Moonraker rows remembered this session, keyed by the
/// identity the page echoes back as `sn`. Function-local statics give
/// thread-safe initialisation; access is always taken through the accessors.
std::map<std::string, NearbyMoonrakerTarget> &moonraker_registry()
{
    static std::map<std::string, NearbyMoonrakerTarget> registry;
    return registry;
}

std::mutex &moonraker_registry_mutex()
{
    static std::mutex mtx;
    return mtx;
}

/// True for a syntactically valid `scheme` token (RFC 3986): ALPHA followed by
/// ALPHA / DIGIT / "+" / "-" / ".".
bool is_scheme_token(const std::string &scheme)
{
    if (scheme.empty())
        return false;
    const char first = scheme.front();
    if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z')))
        return false;
    for (const char c : scheme) {
        const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!alnum && c != '+' && c != '-' && c != '.')
            return false;
    }
    return true;
}

/// True for a decimal port in 1..65535.
bool is_valid_port(const std::string &port)
{
    if (port.empty() || port.size() > 5)
        return false;
    unsigned value = 0;
    for (const char c : port) {
        if (c < '0' || c > '9')
            return false;
        value = value * 10 + static_cast<unsigned>(c - '0');
    }
    return value >= 1 && value <= 65535;
}

/// True when the text after a host is either empty or a `:`-introduced valid port.
bool port_suffix_ok(const std::string &suffix)
{
    return suffix.empty() || (suffix.size() > 1 && suffix[0] == ':' && is_valid_port(suffix.substr(1)));
}

/// Host part of an MQTT `server_address`, or empty when the address is not a
/// well-formed `[scheme://]host[:port]` authority.
///
/// A malformed address (empty or non-token scheme, empty or out-of-range port,
/// a path/query after the authority, an unbalanced bracket) yields an empty host
/// so a malformed create request can never match a remembered row. Plain string
/// work: never a throw.
std::string server_address_host(const std::string &server_address)
{
    std::string rest = server_address;

    // Anything before "://" is a scheme (`mqtt://`, `mqtts://`, other), and it
    // has to be a real scheme token - "://host" is garbage, not a scheme.
    if (const size_t scheme_pos = rest.find("://"); scheme_pos != std::string::npos) {
        if (!is_scheme_token(rest.substr(0, scheme_pos)))
            return std::string();
        rest = rest.substr(scheme_pos + 3);
    }

    if (rest.empty())
        return std::string();

    if (rest.front() == '[') {
        const size_t close = rest.find(']');
        if (close == std::string::npos || !port_suffix_ok(rest.substr(close + 1)))
            return std::string();
        const std::string bracketed = rest.substr(1, close - 1);
        // Only an IPv6-style literal belongs in brackets; Nearby only ever
        // remembers IPv4 targets, so anything else cannot be a match anyway.
        if (bracketed.find(':') == std::string::npos)
            return std::string();
        return bracketed;
    }

    if (const size_t colon = rest.find(':'); colon != std::string::npos) {
        if (!port_suffix_ok(rest.substr(colon)))
            return std::string();
        rest = rest.substr(0, colon);
    }

    // A path, query or fragment where the host should be means this is not the
    // `host[:port]` authority the create request is documented to carry.
    if (rest.empty() || rest.find_first_of("/?#") != std::string::npos)
        return std::string();

    return rest;
}

} // namespace

void nearby_remember_moonraker(const std::string &uuid, const NearbyMoonrakerTarget &target)
{
    if (uuid.empty())
        return;

    std::lock_guard<std::mutex> lock(moonraker_registry_mutex());
    moonraker_registry()[uuid] = target;
}

std::optional<NearbyMoonrakerTarget> nearby_moonraker_target_for(const nlohmann::json &create_params)
{
    if (!create_params.is_object())
        return std::nullopt;

    const auto server_it = create_params.find("server_address");
    if (server_it == create_params.end() || !server_it->is_string())
        return std::nullopt;

    const auto sn_it = create_params.find("sn");
    if (sn_it == create_params.end() || !sn_it->is_string())
        return std::nullopt;
    const std::string sn = sn_it->get<std::string>();
    if (sn.empty())
        return std::nullopt;

    const auto link_it = create_params.find("link_mode");
    if (link_it == create_params.end() || !link_it->is_string())
        return std::nullopt;
    if (link_it->get<std::string>() != "lan")
        return std::nullopt;

    const std::string host = server_address_host(server_it->get<std::string>());
    if (host.empty())
        return std::nullopt;

    std::lock_guard<std::mutex> lock(moonraker_registry_mutex());
    const auto it = moonraker_registry().find(sn);
    if (it == moonraker_registry().end() || it->second.ip != host)
        return std::nullopt;

    return it->second;
}

std::string nearby_moonraker_print_host(const NearbyMoonrakerTarget &target)
{
    return target.ip;
}

uint16_t nearby_moonraker_port(const NearbyMoonrakerTarget &target)
{
    return target.port;
}

} // namespace Slic3r

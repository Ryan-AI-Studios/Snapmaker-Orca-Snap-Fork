#ifndef slic3r_NearbyDiscovery_hpp_
#define slic3r_NearbyDiscovery_hpp_

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <string>

namespace Slic3r {

/// One decoded mDNS reply, in wx-free terms (mirrors Slic3r::BonjourReply).
/// Only IPv4 replies reach the mapper, so `address` is a dotted quad.
struct NearbyReply
{
    std::string address;                     ///< dotted IPv4 of the responder
    uint16_t    port {0};                    ///< SRV port
    std::string instance_name;               ///< DNS-SD instance (BonjourReply::service_name)
    std::string hostname;                    ///< SRV target   (BonjourReply::hostname)
    std::map<std::string, std::string> txt;  ///< allow-listed TXT key/values
};

/// How one mDNS engine's replies map onto a Nearby row.
///
/// `_snapmaker._tcp` and `_moonraker._tcp` advertise very different TXT sets, so
/// the differences are declared per engine instead of guessed inside the mapper.
struct NearbyRowPolicy
{
    /// TXT key that identifies the device: "sn" for `_snapmaker._tcp`,
    /// "uuid" for `_moonraker._tcp`.
    std::string unique_key;

    /// Fill the row fields the service did not advertise (`txt_ip`, `link_mode`,
    /// `device_name`). `_moonraker._tcp` advertises a minimal TXT set and needs
    /// them; `_snapmaker._tcp` must keep this off so its rows stay exactly as the
    /// firmware describes them.
    bool supply_missing_fields {false};

    /// Model label to use when the service advertises no `machine_type`.
    std::string default_machine_type;
};

/// One row of the Devices -> Nearby list.
struct NearbyRow
{
    nlohmann::json data;                      ///< value stored under `list_key`
    std::string    list_key;                  ///< key the reply is filed under in the response
    bool           derived_identity {false};  ///< `list_key` was NOT the engine's own TXT value
};

/// Stable identity of a reply: the engine's own TXT key, else the mDNS instance
/// name, else the IPv4. This is a *derived* value, never a fabricated serial.
/// Empty only when the reply carries nothing to key on.
std::string nearby_identity(const NearbyReply &reply, const std::string &unique_key);

/// True for a dotted IPv4 that can carry a LAN connection: not loopback,
/// not the unspecified address, and not link-local (169.254.0.0/16).
/// Pure string work: malformed input returns false.
bool nearby_is_usable_lan_ipv4(const std::string &ip);

/// Choose the address shown for a reply.
///
/// mDNS responders often advertise several A records, and `DnsMessage::parse_rr`
/// keeps the last one — which for the ZR Ultra S is `eth0`'s link-local address.
/// Prefer a routable IPv4; when the only A record is link-local and the packet
/// came from a routable IPv4, the packet source wins. Falls back to whichever
/// value is non-empty, so a reply is never dropped for lack of a choice.
std::string nearby_choose_address(const std::string &a_record, const std::string &packet_source);

/// Map one decoded reply to a Nearby row.
///
/// `cover_base_url` is the slicer's local page server root
/// ("http://127.0.0.1:<port>", no trailing slash); pass an empty string to omit
/// the cover URL. Pure string work: no wxWidgets, no I/O.
NearbyRow nearby_row_from_reply(const NearbyReply &reply, const NearbyRowPolicy &policy,
                                const std::string &cover_base_url);

} // namespace Slic3r

#endif // slic3r_NearbyDiscovery_hpp_

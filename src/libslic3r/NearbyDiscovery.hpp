#ifndef slic3r_NearbyDiscovery_hpp_
#define slic3r_NearbyDiscovery_hpp_

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <optional>
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

    /// Name the row after `machine_type` instead of the responder's hostname.
    /// `_moonraker._tcp` advertises no human label and its SRV target is the
    /// printer's own hostname, so the model reads better in the list;
    /// `_snapmaker._tcp` must keep the label the firmware advertises.
    bool name_from_machine_type {false};
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

/// A discovered `_moonraker._tcp` device that Nearby can bind as a print host.
struct NearbyMoonrakerTarget
{
    std::string ip;       ///< routable IPv4 the row was listed with
    uint16_t    port {0}; ///< SRV port (kept separate: print_host stays host-only)
    std::string name;     ///< display name of the row
};

/// Remember a discovered Moonraker device. `uuid` is the identity the page
/// echoes back as `sn` (the advertised `uuid` TXT value; the mapper's derived
/// identity is used when the service advertises none). Overwrite by uuid;
/// never cleared; mutex-guarded.
void nearby_remember_moonraker(const std::string &uuid, const NearbyMoonrakerTarget &target);

/// Match a `sw_create_mqtt_client` request against the remembered devices.
/// Returns a target only when `link_mode == "lan"`, `sn` is a remembered uuid
/// and the host parsed from `server_address` equals that row's ip. Every key is
/// type-checked: a wrong/missing type yields nullopt and never throws.
std::optional<NearbyMoonrakerTarget> nearby_moonraker_target_for(const nlohmann::json &create_params);

/// Match the client-create request the page emits for a **device record** it has
/// just learned about (the record the Nearby bind / `sw_UpdateDeviceInfo`
/// writes). That request carries the record's fields, and a Moonraker row's
/// record has `port` 0 - its 7125 is an HTTP port, not an MQTT broker - so the
/// strict matcher above rejects the authority and the page would get a real,
/// never-connected `MqttClient`. This entry point is the same match with the
/// port deliberately ignored (`sn` must still be a row remembered this session,
/// the host must still be that row's address, `link_mode` must still be "lan").
std::optional<NearbyMoonrakerTarget> nearby_moonraker_record_target_for(const nlohmann::json &create_params);

/// Host-only print_host for the target (matches the owner-proven preset).
std::string nearby_moonraker_print_host(const NearbyMoonrakerTarget &target);

/// SRV port of the target, kept separate from print_host.
uint16_t nearby_moonraker_port(const NearbyMoonrakerTarget &target);

} // namespace Slic3r

#endif // slic3r_NearbyDiscovery_hpp_

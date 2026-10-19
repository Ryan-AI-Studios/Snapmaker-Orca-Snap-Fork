#ifndef slic3r_NearbySyntheticEngine_hpp_
#define slic3r_NearbySyntheticEngine_hpp_

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

namespace Slic3r {

/// A stand-in for an MqttClient handle, handed to the page in the
/// `sw_create_mqtt_client` reply when the request matches a Nearby row that
/// this build binds over the OctoPrint-compatible REST API instead of MQTT.
///
/// The page treats the id as opaque and echoes it back as `id` / `engine_id`;
/// nothing in SSWCP parses it (a real engine id is the engine pointer as a
/// decimal string), so the `nearby-` prefix is enough to tell the two apart.
///
/// Kept out of the real engine map: every consumer of that map dereferences a
/// live MqttClient, and a synthetic handle has none.
struct NearbySyntheticEngine
{
    std::string id;       ///< the opaque id the page echoes back
    std::string identity; ///< Nearby row identity (the request's `sn`) - the bind key
    std::string ip;       ///< row ip, for logs and diagnostics
};

std::string nearby_synthetic_engine_id(uint64_t serial);

/// True only for the exact shape `nearby_synthetic_engine_id()` produces:
/// "nearby-" followed by one or more decimal digits, nothing else.
bool nearby_is_synthetic_engine_id(const std::string &id);

/// One `devices:[…]` array entry for the connected printer, in the shape the
/// page's own writer (`sw_UpdateDeviceInfo`) consumes.
///
/// The record is merged, type-checked and saved by that existing path - never
/// by a second writer, and never from a worker thread. `identity` is the row's
/// `sn` / `dev_id`; `name` is the model label 0012 names the row with;
/// `protocol` is the `PrintHostType` the bind uses (`htOctoPrint`).
/// `id` is emitted **empty** (not omitted): a session engine handle must never
/// persist, and the page's merge only overwrites the keys it is given.
nlohmann::json nearby_synthetic_device_entry(const std::string &identity, const std::string &ip,
                                             const std::string &name, double nozzle_diameter,
                                             int protocol);

} // namespace Slic3r

#endif // slic3r_NearbySyntheticEngine_hpp_

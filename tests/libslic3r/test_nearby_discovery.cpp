#include <catch2/catch_test_macros.hpp>

#include "libslic3r/NearbyDiscovery.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

using json = nlohmann::json;
using namespace Slic3r;

namespace {

/// Sorted JSON keys, to compare a row's exact shape.
std::vector<std::string> keys_of(const json &object)
{
    std::vector<std::string> keys;
    for (const auto &item : object.items())
        keys.push_back(item.key());
    return keys;
}

/// `_snapmaker._tcp`: every key the row needs is advertised, so the mapper must
/// not supply anything of its own.
NearbyRowPolicy snapmaker_policy() { return {"sn", false, ""}; }

/// `_moonraker._tcp`: minimal TXT set, no model label.
NearbyRowPolicy moonraker_policy() { return {"uuid", true, "WonderMaker ZR Ultra S"}; }

/// A reply shaped like the ZR Ultra S' Moonraker advertisement: the systemd unit
/// name capitalised as the instance, the host as the SRV target, HTTP port 7125,
/// and only the four TXT keys Moonraker registers.
NearbyReply moonraker_reply()
{
    NearbyReply reply;
    reply.address       = "10.0.0.186";
    reply.port          = 7125;
    reply.instance_name = "Moonraker @ TM-T1";
    reply.hostname      = "TM-T1.local";
    reply.txt           = {
        {"uuid", "5f2b0f7c-1111-2222-3333-444455556666"},
        {"version", "1.4.0"},
        {"route_prefix", "/"},
    };
    return reply;
}

/// A fully populated Snapmaker U1 advertisement - the shape the old inline
/// mapping in SSWCP.cpp was written against.
NearbyReply snapmaker_reply()
{
    NearbyReply reply;
    reply.address       = "192.168.1.20";
    reply.port          = 8080;
    reply.instance_name = "Snapmaker U1";
    reply.hostname      = "snapmaker-u1-abcd.local";
    reply.txt           = {
        {"sn", "U1ABCD1234"},
        {"machine_type", "Snapmaker U1"},
        {"link_mode", "lan"},
        {"device_name", "Workshop"},
        {"userid", "user-42"},
        {"region", "EU"},
        {"version", "1.2.3"},
        {"ip", "192.168.1.20"},
    };
    return reply;
}

} // namespace

TEST_CASE("Nearby maps a Moonraker advertisement into a listable row", "[NearbyMoonraker]")
{
    const NearbyReply reply = moonraker_reply();
    const NearbyRow   row   = nearby_row_from_reply(reply, moonraker_policy(), "http://127.0.0.1:51234");

    // A reply with no `sn` is still recorded: `uuid` identifies it, and the
    // synthesized value is written to `sn`, to the engine field and as markers.
    CHECK(row.list_key == "5f2b0f7c-1111-2222-3333-444455556666");
    CHECK_FALSE(row.derived_identity);
    CHECK(row.data.at("sn") == "5f2b0f7c-1111-2222-3333-444455556666");
    CHECK(row.data.at("uuid") == "5f2b0f7c-1111-2222-3333-444455556666");
    CHECK(row.data.at("unique_key") == "uuid");
    CHECK(row.data.at("unique_value") == "5f2b0f7c-1111-2222-3333-444455556666");

    CHECK(row.data.at("name") == "TM-T1");
    CHECK(row.data.at("ip") == "10.0.0.186");
    CHECK(row.data.at("port") == 7125);
    CHECK(row.data.at("pro_version") == "1.4.0");

    // Moonraker advertises no `ip`, `link_mode` or `device_name` TXT key. This
    // engine supplies all three, so the row carries every field the UI reads and
    // does not hide its own address behind an empty `txt_ip`.
    CHECK(row.data.at("txt_ip") == "10.0.0.186");
    CHECK(row.data.at("link_mode") == "lan");
    CHECK(row.data.at("device_name") == "TM-T1");

    // Moonraker advertises no model label: the engine supplies one for this
    // printer, and the cover convention is built from it.
    CHECK(row.data.at("machine_type") == "WonderMaker ZR Ultra S");
    CHECK(row.data.at("cover") ==
          "http://127.0.0.1:51234/profiles/WonderMaker/WonderMaker ZR Ultra S_cover.png");

    CHECK(keys_of(row.data) ==
          std::vector<std::string>{"cover", "device_name", "ip", "link_mode", "machine_type", "name",
                                   "port", "pro_version", "sn", "txt_ip", "unique_key", "unique_value",
                                   "uuid"});
}

TEST_CASE("Nearby identity falls back uuid then instance name then IPv4", "[NearbyMoonraker]")
{
    NearbyReply reply = moonraker_reply();
    CHECK(nearby_identity(reply, "uuid") == "5f2b0f7c-1111-2222-3333-444455556666");

    // No identity TXT key at all -> the mDNS instance name, still written to the
    // engine's own field so the row satisfies the dual-field contract.
    reply.txt.erase("uuid");
    CHECK(nearby_identity(reply, "uuid") == "Moonraker @ TM-T1");

    const NearbyRow from_instance = nearby_row_from_reply(reply, moonraker_policy(), "");
    CHECK(from_instance.derived_identity);
    CHECK(from_instance.data.at("sn") == "Moonraker @ TM-T1");
    CHECK(from_instance.data.at("uuid") == "Moonraker @ TM-T1");
    CHECK(from_instance.data.at("unique_value") == "Moonraker @ TM-T1");

    // Neither -> the IPv4, so the row is still listable.
    reply.instance_name.clear();
    CHECK(nearby_identity(reply, "uuid") == "10.0.0.186");

    const NearbyRow from_ip = nearby_row_from_reply(reply, moonraker_policy(), "");
    CHECK(from_ip.list_key == "10.0.0.186");
    CHECK(from_ip.data.at("sn") == "10.0.0.186");
    CHECK(from_ip.data.at("uuid") == "10.0.0.186");
}

TEST_CASE("Nearby display name falls back from the SRV host to the instance name and the IP",
          "[NearbyMoonraker]")
{
    NearbyReply reply = moonraker_reply();

    CHECK(nearby_row_from_reply(reply, moonraker_policy(), "").data.at("name") == "TM-T1");

    reply.hostname.clear();
    CHECK(nearby_row_from_reply(reply, moonraker_policy(), "").data.at("name") == "Moonraker @ TM-T1");

    reply.instance_name.clear();
    CHECK(nearby_row_from_reply(reply, moonraker_policy(), "").data.at("name") == "10.0.0.186");
}

TEST_CASE("Nearby prefers an advertised IP TXT key over the responder address", "[NearbyMoonraker]")
{
    NearbyReply reply = moonraker_reply();
    reply.txt["ip"]   = "192.168.1.50";

    const NearbyRow row = nearby_row_from_reply(reply, moonraker_policy(), "");
    CHECK(row.data.at("txt_ip") == "192.168.1.50");
    CHECK(row.data.at("ip") == "10.0.0.186");
}

TEST_CASE("Nearby tolerates a Moonraker advertisement without route_prefix or https_port",
          "[NearbyMoonraker]")
{
    NearbyReply reply = moonraker_reply();
    reply.txt.erase("route_prefix");

    const NearbyRow row = nearby_row_from_reply(reply, moonraker_policy(), "");
    CHECK(row.data.at("sn") == "5f2b0f7c-1111-2222-3333-444455556666");
    CHECK(row.data.at("port") == 7125);
    CHECK_FALSE(row.data.contains("route_prefix"));

    // TLS on: Moonraker advertises `https_port` instead of an empty value. The
    // row still reports the SRV (HTTP) port and carries no https field.
    reply.txt["https_port"] = "7126";
    const NearbyRow tls     = nearby_row_from_reply(reply, moonraker_policy(), "");
    CHECK(tls.data.at("port") == 7125);
    CHECK_FALSE(tls.data.contains("https_port"));
}

TEST_CASE("Nearby maps a populated Snapmaker advertisement exactly as the old inline code did",
          "[NearbyMoonraker]")
{
    const NearbyRow row = nearby_row_from_reply(snapmaker_reply(), snapmaker_policy(), "http://127.0.0.1:51234");

    CHECK(row.list_key == "U1ABCD1234");
    CHECK_FALSE(row.derived_identity);
    CHECK(row.data.at("sn") == "U1ABCD1234");
    CHECK(row.data.at("unique_key") == "sn");
    CHECK(row.data.at("unique_value") == "U1ABCD1234");
    CHECK(row.data.at("name") == "snapmaker-u1-abcd");
    CHECK(row.data.at("ip") == "192.168.1.20");
    CHECK(row.data.at("txt_ip") == "192.168.1.20");
    CHECK(row.data.at("port") == 8080);
    CHECK(row.data.at("link_mode") == "lan");
    CHECK(row.data.at("device_name") == "Workshop");
    CHECK(row.data.at("userid") == "user-42");
    CHECK(row.data.at("region") == "EU");
    CHECK(row.data.at("pro_version") == "1.2.3");
    CHECK(row.data.at("machine_type") == "Snapmaker U1");
    CHECK(row.data.at("cover") ==
          "http://127.0.0.1:51234/profiles/Snapmaker/Snapmaker U1_cover.png");

    // Exact row shape: nothing removed and nothing added for a Snapmaker reply.
    CHECK(keys_of(row.data) ==
          std::vector<std::string>{"cover", "device_name", "ip", "link_mode", "machine_type", "name",
                                   "port", "pro_version", "region", "sn", "txt_ip", "unique_key",
                                   "unique_value", "userid"});
}

TEST_CASE("Nearby leaves a service that supplies its own fields untouched", "[NearbyMoonraker]")
{
    // The regression oracle for `_snapmaker._tcp`: a reply that advertises less
    // must keep the shape the old inline mapping produced - no invented link
    // mode, no address substituted into `txt_ip`, no invented model label.
    NearbyReply reply;
    reply.address       = "192.168.1.20";
    reply.port          = 8080;
    reply.instance_name = "Snapmaker U1";
    reply.hostname      = "snapmaker-u1-abcd.local";
    reply.txt           = {{"sn", "U1ABCD1234"}};

    const NearbyRow row = nearby_row_from_reply(reply, snapmaker_policy(), "http://127.0.0.1:51234");
    CHECK(row.data.at("txt_ip") == "");
    CHECK_FALSE(row.data.contains("link_mode"));
    CHECK_FALSE(row.data.contains("device_name"));
    CHECK_FALSE(row.data.contains("machine_type"));
    CHECK_FALSE(row.data.contains("cover"));

    CHECK(keys_of(row.data) ==
          std::vector<std::string>{"ip", "name", "port", "sn", "txt_ip", "unique_key", "unique_value"});

    // An advertised link mode is still taken verbatim.
    NearbyReply wan = reply;
    wan.txt["link_mode"] = "wan";
    CHECK(nearby_row_from_reply(wan, snapmaker_policy(), "").data.at("link_mode") == "wan");
}

TEST_CASE("Nearby flags an identity it had to derive", "[NearbyMoonraker]")
{
    NearbyReply reply = moonraker_reply();
    CHECK_FALSE(nearby_row_from_reply(reply, moonraker_policy(), "").derived_identity);

    reply.txt.erase("uuid");
    CHECK(nearby_row_from_reply(reply, moonraker_policy(), "").derived_identity);

    // Nothing to key on at all: empty identity, still flagged as derived.
    NearbyReply empty;
    const NearbyRow nothing = nearby_row_from_reply(empty, moonraker_policy(), "");
    CHECK(nothing.derived_identity);
    CHECK(nothing.list_key.empty());
    CHECK(nothing.data.at("sn") == "");
}

TEST_CASE("Nearby uses the engine model label only when the service advertises none",
          "[NearbyMoonraker]")
{
    // An advertised label always wins over the engine default.
    NearbyReply labelled = moonraker_reply();
    labelled.txt["machine_type"] = "Moonraker";
    const NearbyRow from_txt = nearby_row_from_reply(labelled, moonraker_policy(), "");
    CHECK(from_txt.data.at("machine_type") == "Moonraker");
    CHECK_FALSE(from_txt.data.contains("cover")); // single word -> no cover path

    // No label and no default -> neither key is materialised.
    const NearbyRow bare = nearby_row_from_reply(moonraker_reply(), snapmaker_policy(), "");
    CHECK_FALSE(bare.data.contains("machine_type"));
    CHECK_FALSE(bare.data.contains("cover"));
}

TEST_CASE("Nearby cover URL needs both a machine type and a base URL", "[NearbyMoonraker]")
{
    CHECK(nearby_row_from_reply(moonraker_reply(), moonraker_policy(), "")
              .data.count("cover") == 0);

    NearbyReply single = moonraker_reply();
    single.txt["machine_type"] = "Moonraker";
    CHECK_FALSE(nearby_row_from_reply(single, moonraker_policy(), "http://127.0.0.1:51234")
                    .data.contains("cover"));
}

TEST_CASE("Nearby carries the optional protocol and version fields the row vocabulary allows",
          "[NearbyMoonraker]")
{
    NearbyReply reply = moonraker_reply();
    reply.txt["protocol"] = "mdns";

    const NearbyRow row = nearby_row_from_reply(reply, moonraker_policy(), "");
    CHECK(row.data.at("protocol") == "mdns");
    CHECK(row.data.at("pro_version") == "1.4.0");

    // Absent optional keys are not materialised.
    NearbyReply bare = moonraker_reply();
    bare.txt.erase("version");
    const NearbyRow bare_row = nearby_row_from_reply(bare, moonraker_policy(), "");
    CHECK_FALSE(bare_row.data.contains("protocol"));
    CHECK_FALSE(bare_row.data.contains("pro_version"));
}

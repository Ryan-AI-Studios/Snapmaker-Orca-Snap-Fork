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
/// not supply anything of its own. Naming is off, so rows keep the advertised
/// label.
NearbyRowPolicy snapmaker_policy() { return {"sn", false, "", false}; }

/// `_moonraker._tcp`: minimal TXT set, no model label, and the engine names the
/// row after the model it supplies (the SRV target is only the hostname).
NearbyRowPolicy moonraker_policy() { return {"uuid", true, "WonderMaker ZR Ultra S", true}; }

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

    // The engine names this row after the model it supplies (the Moondraker SRV
    // target is only the printer's hostname, "TM-T1").
    CHECK(row.data.at("name") == "WonderMaker ZR Ultra S");
    CHECK(row.data.at("ip") == "10.0.0.186");
    CHECK(row.data.at("port") == 7125);
    CHECK(row.data.at("pro_version") == "1.4.0");

    // Moonraker advertises no `ip`, `link_mode` or `device_name` TXT key. This
    // engine supplies all three, so the row carries every field the UI reads and
    // does not hide its own address behind an empty `txt_ip`.
    CHECK(row.data.at("txt_ip") == "10.0.0.186");
    CHECK(row.data.at("link_mode") == "lan");
    CHECK(row.data.at("device_name") == "WonderMaker ZR Ultra S");

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

    // A policy that does not opt in to model naming keeps the hostname chain, so
    // the fallbacks stay covered; the engine's own policy is exercised above.
    const NearbyRowPolicy no_model_name{"uuid", true, "WonderMaker ZR Ultra S", false};

    CHECK(nearby_row_from_reply(reply, no_model_name, "").data.at("name") == "TM-T1");

    reply.hostname.clear();
    CHECK(nearby_row_from_reply(reply, no_model_name, "").data.at("name") == "Moonraker @ TM-T1");

    reply.instance_name.clear();
    CHECK(nearby_row_from_reply(reply, no_model_name, "").data.at("name") == "10.0.0.186");
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
    // Track 0012: the Snapmaker engine does not opt in to model naming, so a
    // reply that advertises a `machine_type` still keeps the advertised label.
    CHECK(row.data.at("machine_type") == "Snapmaker U1");
    CHECK(row.data.at("name") != row.data.at("machine_type"));
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

// ---------------------------------------------------------------------------
// Address selection (track 0010): a responder that advertises a link-local A
// record (the ZR Ultra S does, on eth0) must not drag the row onto 169.254.x.
// ---------------------------------------------------------------------------

TEST_CASE("Nearby rejects addresses that cannot carry a LAN connection", "[MdnsInterfaceFilter]")
{
    CHECK_FALSE(nearby_is_usable_lan_ipv4("127.0.0.1"));
    CHECK_FALSE(nearby_is_usable_lan_ipv4("169.254.65.116"));
    CHECK_FALSE(nearby_is_usable_lan_ipv4("0.0.0.0"));
    CHECK_FALSE(nearby_is_usable_lan_ipv4(""));
    CHECK_FALSE(nearby_is_usable_lan_ipv4("10.0.0"));
    CHECK_FALSE(nearby_is_usable_lan_ipv4("10.0.0.256"));
    CHECK_FALSE(nearby_is_usable_lan_ipv4("10.0.0.245 "));
    CHECK_FALSE(nearby_is_usable_lan_ipv4("::1"));

    CHECK(nearby_is_usable_lan_ipv4("10.0.0.245"));
    CHECK(nearby_is_usable_lan_ipv4("192.168.1.20"));
    CHECK(nearby_is_usable_lan_ipv4("172.16.5.4"));
}

TEST_CASE("Nearby prefers a routable A record over a link-local one", "[MdnsInterfaceFilter]")
{
    // The real ZR Ultra S case: two A records, parse_rr keeps the last (link-local).
    CHECK(nearby_choose_address("169.254.65.116", "10.0.0.186") == "10.0.0.186");
    // A routable A record wins even when the packet came from elsewhere.
    CHECK(nearby_choose_address("10.0.0.186", "10.0.0.245") == "10.0.0.186");
    // Nothing routable anywhere: keep what the advertisement said.
    CHECK(nearby_choose_address("169.254.65.116", "169.254.1.1") == "169.254.65.116");
    // No A record at all: use the responder address.
    CHECK(nearby_choose_address("", "10.0.0.186") == "10.0.0.186");
    CHECK(nearby_choose_address("", "") == "");
}

// ---------------------------------------------------------------------------
// Nearby Connect binding (track 0011): the matcher that decides whether a
// `sw_create_mqtt_client` request belongs to a remembered Moonraker row, plus
// the host-only print_host / separate SRV port split. The registry is global
// and never cleared, so every case uses its own uuid.
// ---------------------------------------------------------------------------

TEST_CASE("Nearby binds a remembered Moonraker row from its create-mqtt-client request", "[NearbyConnect]")
{
    const std::string uuid = "0011-connect-0001-a";
    nearby_remember_moonraker(uuid, {"10.0.0.186", 7125, "TM-T1"});

    const auto target = nearby_moonraker_target_for(json{
        {"server_address", "mqtt://10.0.0.186:1884"},
        {"sn", uuid},
        {"link_mode", "lan"},
        {"clientId", "nearby-connect-0001"},
    });
    REQUIRE(target.has_value());
    CHECK(target->ip == "10.0.0.186");
    CHECK(target->port == 7125);
    CHECK(target->name == "TM-T1");

    // print_host is host-only (the owner-proven preset shape); the SRV port is
    // carried separately and never joined into the host.
    CHECK(nearby_moonraker_print_host(*target) == "10.0.0.186");
    CHECK(nearby_moonraker_port(*target) == 7125);
}

TEST_CASE("Nearby refuses a remembered row when the request names a different host", "[NearbyConnect]")
{
    const std::string uuid = "0011-connect-0002-a";
    nearby_remember_moonraker(uuid, {"10.0.0.186", 7125, "TM-T1"});

    CHECK_FALSE(nearby_moonraker_target_for(json{
        {"server_address", "mqtt://10.0.0.187:1884"},
        {"sn", uuid},
        {"link_mode", "lan"},
    }).has_value());
}

TEST_CASE("Nearby refuses an unknown identity", "[NearbyConnect]")
{
    CHECK_FALSE(nearby_moonraker_target_for(json{
        {"server_address", "mqtt://10.0.0.186:1884"},
        {"sn", "0011-connect-0003-not-registered"},
        {"link_mode", "lan"},
    }).has_value());

    // An empty uuid is never remembered, so it can never match either.
    nearby_remember_moonraker("", {"10.0.0.186", 7125, "TM-T1"});
    CHECK_FALSE(nearby_moonraker_target_for(json{
        {"server_address", "mqtt://10.0.0.186:1884"},
        {"sn", ""},
        {"link_mode", "lan"},
    }).has_value());
}

TEST_CASE("Nearby requires the LAN link mode", "[NearbyConnect]")
{
    const std::string uuid = "0011-connect-0004-a";
    nearby_remember_moonraker(uuid, {"10.0.0.186", 7125, "TM-T1"});

    // Missing.
    CHECK_FALSE(nearby_moonraker_target_for(json{
        {"server_address", "mqtt://10.0.0.186:1884"},
        {"sn", uuid},
    }).has_value());

    // Present but not LAN.
    CHECK_FALSE(nearby_moonraker_target_for(json{
        {"server_address", "mqtt://10.0.0.186:1884"},
        {"sn", uuid},
        {"link_mode", "wan"},
    }).has_value());
}

TEST_CASE("Nearby tolerates a missing, empty, malformed or non-string server_address", "[NearbyConnect]")
{
    const std::string uuid = "0011-connect-0005-a";
    nearby_remember_moonraker(uuid, {"10.0.0.186", 7125, "TM-T1"});

    json missing;
    missing["sn"]        = uuid;
    missing["link_mode"] = "lan";
    CHECK_FALSE(nearby_moonraker_target_for(missing).has_value());

    CHECK_FALSE(nearby_moonraker_target_for(json{{"server_address", ""}, {"sn", uuid}, {"link_mode", "lan"}})
                    .has_value());
    CHECK_FALSE(nearby_moonraker_target_for(json{{"server_address", "mqtt://"}, {"sn", uuid}, {"link_mode", "lan"}})
                    .has_value());
    CHECK_FALSE(nearby_moonraker_target_for(json{{"server_address", "mqtt://:1884"}, {"sn", uuid}, {"link_mode", "lan"}})
                    .has_value());
    CHECK_FALSE(nearby_moonraker_target_for(json{{"server_address", "[::1"}, {"sn", uuid}, {"link_mode", "lan"}})
                    .has_value());
    CHECK_FALSE(nearby_moonraker_target_for(json{{"server_address", 1884}, {"sn", uuid}, {"link_mode", "lan"}})
                    .has_value());
    CHECK_FALSE(nearby_moonraker_target_for(json{{"server_address", nullptr}, {"sn", uuid}, {"link_mode", "lan"}})
                    .has_value());

    // A non-object document (and a bare array) must not throw either.
    CHECK_FALSE(nearby_moonraker_target_for(json("not-an-object")).has_value());
    CHECK_FALSE(nearby_moonraker_target_for(json::array()).has_value());
}

TEST_CASE("Nearby parses the host out of every server_address shape", "[NearbyConnect]")
{
    const std::string uuid = "0011-connect-0006-a";
    nearby_remember_moonraker(uuid, {"10.0.0.186", 7125, "TM-T1"});

    const auto matches = [&uuid](const std::string &server) {
        const auto target =
            nearby_moonraker_target_for(json{{"server_address", server}, {"sn", uuid}, {"link_mode", "lan"}});
        return target.has_value() && target->ip == "10.0.0.186";
    };

    CHECK(matches("mqtt://10.0.0.186:1884"));  // scheme + port (the live Connect payload)
    CHECK(matches("mqtts://10.0.0.186:8883")); // TLS scheme
    CHECK(matches("10.0.0.186"));              // bare host
    CHECK(matches("10.0.0.186:1234"));         // host + port, no scheme
    CHECK(matches("tcp://10.0.0.186"));        // any other scheme, no port
}

TEST_CASE("Nearby remembers the newest address for a uuid", "[NearbyConnect]")
{
    const std::string uuid = "0011-connect-0007-a";
    nearby_remember_moonraker(uuid, {"10.0.0.186", 7125, "TM-T1"});
    nearby_remember_moonraker(uuid, {"10.0.0.222", 7100, "TM-T1"});

    // The old address no longer matches; the new one does.
    CHECK_FALSE(nearby_moonraker_target_for(json{
        {"server_address", "mqtt://10.0.0.186:1884"},
        {"sn", uuid},
        {"link_mode", "lan"},
    }).has_value());

    const auto target = nearby_moonraker_target_for(json{
        {"server_address", "mqtt://10.0.0.222:1884"},
        {"sn", uuid},
        {"link_mode", "lan"},
    });
    REQUIRE(target.has_value());
    CHECK(target->ip == "10.0.0.222");
    CHECK(nearby_moonraker_port(*target) == 7100);
}

TEST_CASE("Nearby type-checks sn and link_mode without throwing", "[NearbyConnect]")
{
    const std::string uuid = "0011-connect-0008-a";
    nearby_remember_moonraker(uuid, {"10.0.0.186", 7125, "TM-T1"});

    CHECK_FALSE(nearby_moonraker_target_for(json{
        {"server_address", "mqtt://10.0.0.186:1884"}, {"sn", 42}, {"link_mode", "lan"},
    }).has_value());
    CHECK_FALSE(nearby_moonraker_target_for(json{
        {"server_address", "mqtt://10.0.0.186:1884"}, {"sn", json::array()}, {"link_mode", "lan"},
    }).has_value());
    CHECK_FALSE(nearby_moonraker_target_for(json{
        {"server_address", "mqtt://10.0.0.186:1884"}, {"sn", uuid}, {"link_mode", 1},
    }).has_value());
    CHECK_FALSE(nearby_moonraker_target_for(json{
        {"server_address", "mqtt://10.0.0.186:1884"}, {"sn", uuid}, {"link_mode", nullptr},
    }).has_value());
    CHECK_FALSE(nearby_moonraker_target_for(json{
        {"server_address", "mqtt://10.0.0.186:1884"}, {"sn", uuid}, {"link_mode", json::object()},
    }).has_value());
}

TEST_CASE("Nearby binds the identity the request names", "[NearbyConnect]")
{
    // Two remembered rows can share one address. The request's `sn` alone picks
    // the target, so neither row can be bound by a click meant for the other.
    const std::string first  = "0011-connect-0009-a";
    const std::string second = "0011-connect-0009-b";
    nearby_remember_moonraker(first, {"10.0.0.186", 7125, "first"});
    nearby_remember_moonraker(second, {"10.0.0.186", 7126, "second"});

    const auto a = nearby_moonraker_target_for(json{
        {"server_address", "mqtt://10.0.0.186:1884"}, {"sn", first}, {"link_mode", "lan"},
    });
    REQUIRE(a.has_value());
    CHECK(a->port == 7125);
    CHECK(a->name == "first");

    const auto b = nearby_moonraker_target_for(json{
        {"server_address", "mqtt://10.0.0.186:1884"}, {"sn", second}, {"link_mode", "lan"},
    });
    REQUIRE(b.has_value());
    CHECK(b->port == 7126);
    CHECK(b->name == "second");
}

TEST_CASE("Nearby rejects a malformed server_address authority", "[NearbyConnect]")
{
    // Plan Phase 3 case 5: a malformed address must never be taken as a match
    // just because its text happens to contain a remembered IP.
    const std::string uuid = "0011-connect-0010-a";
    nearby_remember_moonraker(uuid, {"10.0.0.186", 7125, "TM-T1"});

    const auto rejects = [&uuid](const std::string &server) {
        return !nearby_moonraker_target_for(json{{"server_address", server}, {"sn", uuid}, {"link_mode", "lan"}})
                    .has_value();
    };

    CHECK(rejects("://10.0.0.186:1884"));           // empty scheme
    CHECK(rejects("1://10.0.0.186:1884"));          // scheme must start with a letter
    CHECK(rejects("-://10.0.0.186"));               // scheme must start with a letter
    CHECK(rejects("+://10.0.0.186"));               // scheme must start with a letter
    CHECK(rejects("mqtt://10.0.0.186:"));           // empty port
    CHECK(rejects("mqtt://10.0.0.186:not-a-port")); // non-numeric port
    CHECK(rejects("mqtt://10.0.0.186:0"));          // port out of range
    CHECK(rejects("mqtt://10.0.0.186:65536"));      // port out of range
    CHECK(rejects("mqtt://10.0.0.186:1884/path"));  // authority has a path
    CHECK(rejects("10.0.0.186/path"));              // path with no port at all
    CHECK(rejects("10.0.0.186#frag"));              // fragment
    CHECK(rejects("mqtt://10.0.0.186?x=1"));        // authority has a query
    CHECK(rejects("mqtt://[10.0.0.186]:1884"));     // brackets around a non-IPv6 host
    CHECK(rejects("mqtt://[10.0.0.186:1884"));      // unbalanced bracket

    // The well-formed shapes still match, including a scheme with no port.
    const auto accepts = [&uuid](const std::string &server) {
        return nearby_moonraker_target_for(json{{"server_address", server}, {"sn", uuid}, {"link_mode", "lan"}})
            .has_value();
    };
    CHECK(accepts("mqtt://10.0.0.186"));
    CHECK(accepts("mqtt://10.0.0.186:1884"));
    CHECK(accepts("10.0.0.186:65535"));
}
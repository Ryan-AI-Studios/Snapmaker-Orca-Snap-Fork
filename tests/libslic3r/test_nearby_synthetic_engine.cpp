#include <catch2/catch_test_macros.hpp>

#include "libslic3r/NearbySyntheticEngine.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <vector>

using json = nlohmann::json;
using namespace Slic3r;

namespace {

std::vector<std::string> sorted_keys_of(const json &object)
{
    std::vector<std::string> keys;
    for (const auto &item : object.items())
        keys.push_back(item.key());
    std::sort(keys.begin(), keys.end());
    return keys;
}

} // namespace

TEST_CASE("Synthetic engine ids are opaque, prefixed and unambiguous", "[NearbySyntheticEngine]")
{
    // A real engine id is the client pointer as a decimal string. The synthetic
    // form must never be mistakable for one.
    CHECK(nearby_synthetic_engine_id(0) == "nearby-0");
    CHECK(nearby_synthetic_engine_id(1) == "nearby-1");
    CHECK(nearby_synthetic_engine_id(42) == "nearby-42");

    // Generous range: the serial is only an allocation counter.
    for (uint64_t serial : {0ull, 1ull, 7ull, 999ull, 123456789ull, UINT64_MAX})
        CHECK(nearby_is_synthetic_engine_id(nearby_synthetic_engine_id(serial)));
}

TEST_CASE("Only the exact synthetic shape is recognised", "[NearbySyntheticEngine]")
{
    // Rejected: a real id, a prefix with no serial, non-digits, trailing junk,
    // a different case, and the bare prefix.
    CHECK_FALSE(nearby_is_synthetic_engine_id(""));
    CHECK_FALSE(nearby_is_synthetic_engine_id("1234567890"));
    CHECK_FALSE(nearby_is_synthetic_engine_id("0"));
    CHECK_FALSE(nearby_is_synthetic_engine_id("nearby-"));
    CHECK_FALSE(nearby_is_synthetic_engine_id("nearby-x"));
    CHECK_FALSE(nearby_is_synthetic_engine_id("nearby-1x"));
    CHECK_FALSE(nearby_is_synthetic_engine_id("nearby-1 "));
    CHECK_FALSE(nearby_is_synthetic_engine_id("nearby--1"));
    CHECK_FALSE(nearby_is_synthetic_engine_id("Nearby-1"));
    CHECK_FALSE(nearby_is_synthetic_engine_id(" nearby-1"));

    // Accepted: digits only after the prefix (leading zeros are still digits).
    CHECK(nearby_is_synthetic_engine_id("nearby-0"));
    CHECK(nearby_is_synthetic_engine_id("nearby-007"));
}

TEST_CASE("The connected device entry is the page-writer shape", "[NearbySyntheticEngine]")
{
    const std::string identity = "bd2c70a74fdd4186afd887e86008dc63";
    const json        entry    = nearby_synthetic_device_entry(identity, "10.0.0.186",
                                                               "WonderMaker ZR Ultra S", 0.6, 2);

    // Exactly the keys the row and the bind determine - and `id` present but
    // empty: the merge only overwrites keys it is given, so emitting it is what
    // guarantees a session handle never persists into the record.
    CHECK(sorted_keys_of(entry) ==
          std::vector<std::string>{"connected", "dev_id", "dev_name", "id", "ip", "link_mode", "model_name",
                                   "nozzle_sizes", "protocol", "sn"});
    CHECK(entry["id"].get<std::string>().empty());

    CHECK(entry["dev_id"].get<std::string>() == identity);
    CHECK(entry["sn"].get<std::string>() == identity);
    CHECK(entry["ip"].get<std::string>() == "10.0.0.186");
    CHECK(entry["dev_name"].get<std::string>() == "WonderMaker ZR Ultra S");
    CHECK(entry["model_name"].get<std::string>() == "WonderMaker ZR Ultra S");
    CHECK(entry["connected"].get<bool>());
    CHECK(entry["link_mode"].get<std::string>() == "lan");
    CHECK(entry["protocol"].get<int>() == 2); // htOctoPrint

    // A number, not a string: the page's writer canonicalises accepted nozzle
    // diameters exactly as it does for a device the page reported itself.
    REQUIRE(entry["nozzle_sizes"].is_array());
    REQUIRE(entry["nozzle_sizes"].size() == 1);
    CHECK(entry["nozzle_sizes"][0].is_number());
    CHECK(entry["nozzle_sizes"][0].get<double>() == 0.6);
}

TEST_CASE("The connected device entry round-trips through JSON", "[NearbySyntheticEngine]")
{
    const json entry = nearby_synthetic_device_entry("sn-1", "192.168.1.50", "WonderMaker ZR Ultra S",
                                                     0.4, 2);
    const json parsed = json::parse(entry.dump());

    CHECK(parsed["sn"].get<std::string>() == "sn-1");
    CHECK(parsed["connected"].get<bool>());
    CHECK(parsed["nozzle_sizes"][0].get<double>() == 0.4);
    REQUIRE(parsed.contains("id"));
    CHECK(parsed["id"].get<std::string>().empty());
}

#include <catch2/catch_test_macros.hpp>

#include "libslic3r/OfdCatalog.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <string>
#include <vector>

using namespace Slic3r;

namespace {

const char *kTwoVariantSeed = R"({
  "variants": [
    {"brand": "Acme", "filament": "PLA Basic", "variant": "Red", "material": "PLA", "color_hex": "#ff0000"},
    {"brand": "Acme", "filament": "PLA Basic", "variant": "Blue", "material": "PLA", "color_hex": "#0000ff"}
  ]
})";

void write_file(const boost::filesystem::path &path, const std::string &text)
{
    boost::filesystem::create_directories(path.parent_path());
    boost::nowide::ofstream ofs(path.string(), std::ios::binary | std::ios::trunc);
    ofs << text;
}

} // namespace

TEST_CASE("OFD seed object parses two variants and lookup finds both", "[Ofd]")
{
    const auto rows = ofd_parse(kTwoVariantSeed);
    REQUIRE(rows.size() == 2);
    const auto found = ofd_lookup(rows, "", "pla");
    REQUIRE(found.size() == 2);
    const auto red = ofd_lookup(rows, "acme", "RED");
    REQUIRE(red.size() == 1);
    CHECK(red.front().variant == "Red");
}

TEST_CASE("OFD color_hex string vs array normalizes to uppercase #RRGGBB", "[Ofd]")
{
    const auto str_rows = ofd_parse(R"({"brand":"A","filament":"F","variant":"V","color_hex":"#aabbcc"})");
    REQUIRE(str_rows.size() == 1);
    REQUIRE(str_rows.front().color_hexes.size() == 1);
    CHECK(str_rows.front().color_hexes.front() == "#AABBCC");

    const auto arr_rows = ofd_parse(
        R"({"brand":"A","filament":"F","variant":"Dual","color_hex":["#ff0000","#00ff00"]})");
    REQUIRE(arr_rows.size() == 1);
    REQUIRE(arr_rows.front().color_hexes.size() == 2);
    CHECK(arr_rows.front().color_hexes[0] == "#FF0000");
    CHECK(arr_rows.front().color_hexes[1] == "#00FF00");
}

TEST_CASE("OFD NDJSON _type join skips unresolved and no-hex rows", "[Ofd]")
{
    const char *ndjson =
        "{\"_type\":\"brand\",\"id\":\"b1\",\"name\":\"Acme\"}\n"
        "{\"_type\":\"filament\",\"id\":\"f1\",\"name\":\"PLA Basic\",\"brand_id\":\"b1\",\"material\":\"PLA\"}\n"
        "{\"_type\":\"variant\",\"id\":\"v1\",\"name\":\"Red\",\"filament_id\":\"f1\",\"color_hex\":\"#ff0000\"}\n"
        "{\"_type\":\"variant\",\"id\":\"v2\",\"name\":\"NoHex\",\"filament_id\":\"f1\"}\n"
        "{\"_type\":\"variant\",\"id\":\"v3\",\"name\":\"Orphan\",\"filament_id\":\"missing\",\"color_hex\":\"#00ff00\"}\n";
    const auto rows = ofd_parse(ndjson);
    REQUIRE(rows.size() == 1);
    CHECK(rows.front().brand == "Acme");
    CHECK(rows.front().filament == "PLA Basic");
    CHECK(rows.front().variant == "Red");
    CHECK(rows.front().material == "PLA");
    CHECK(rows.front().color_hexes.front() == "#FF0000");
}

TEST_CASE("OFD bad JSON and truncated lines do not throw", "[Ofd]")
{
    CHECK(ofd_parse("{ not json").empty());
    CHECK(ofd_parse("").empty());
    CHECK(ofd_parse("{\"_type\":\"variant\"").empty());
    CHECK_NOTHROW(ofd_parse("[\n{\"color_hex\":\"#zzzzzz\"}\n]"));
}

TEST_CASE("OFD overlay duplicate key keeps the seed row", "[Ofd]")
{
    const auto tmp = boost::filesystem::temp_directory_path() / "snapfork-ofd-test";
    const auto seed_path    = tmp / "seed.json";
    const auto overlay_path = tmp / "overlay.ndjson";
    write_file(seed_path, kTwoVariantSeed);
    write_file(overlay_path,
               R"({"brand":"Acme","filament":"PLA Basic","variant":"Red","color_hex":"#111111"}
{"brand":"Other","filament":"PETG","variant":"Green","color_hex":"#00ff00"}
)");
    const auto rows = ofd_load_catalog(seed_path.string(), overlay_path.string());
    REQUIRE(rows.size() == 3);
    const auto red = ofd_lookup(rows, "", "Red");
    REQUIRE(red.size() == 1);
    CHECK(red.front().color_hexes.front() == "#FF0000");
    const auto green = ofd_lookup(rows, "", "Green");
    REQUIRE(green.size() == 1);
    CHECK(green.front().brand == "Other");
}

TEST_CASE("OFD recents LRU cap 10 newest first same key moves to front", "[Ofd]")
{
    std::vector<OfdVariant> recents;
    for (int i = 0; i < 12; ++i) {
        OfdVariant v;
        v.brand        = "B";
        v.filament     = "F";
        v.variant      = "V" + std::to_string(i);
        v.color_hexes  = {"#000000"};
        ofd_recents_push(recents, v, 10);
    }
    REQUIRE(recents.size() == 10);
    CHECK(recents.front().variant == "V11");
    CHECK(recents.back().variant == "V2");

    OfdVariant again;
    again.brand       = "B";
    again.filament    = "F";
    again.variant     = "V5";
    again.color_hexes = {"#FFFFFF"};
    ofd_recents_push(recents, again, 10);
    REQUIRE(recents.size() == 10);
    CHECK(recents.front().variant == "V5");
    CHECK(recents.front().color_hexes.front() == "#FFFFFF");
}

TEST_CASE("OFD stamp single-color leaves other slots unchanged", "[Ofd]")
{
    std::vector<std::string> colour{"#111111", "#222222", "#333333"};
    std::vector<std::string> multi{"", "", ""};
    std::vector<int>         mode{1, 1, 1};
    const auto               before = colour;
    REQUIRE(ofd_stamp_slot(colour, multi, mode, 1, {"#aabbcc"}));
    CHECK(colour[0] == before[0]);
    CHECK(colour[2] == before[2]);
    CHECK(colour[1] == "#AABBCC");
    CHECK(multi[1] == "#AABBCC");
    CHECK(mode[1] == 0);
}

TEST_CASE("OFD stamp dual-color writes pipe join and mode 0", "[Ofd]")
{
    std::vector<std::string> colour{"#111111", "#222222"};
    std::vector<std::string> multi{"", ""};
    std::vector<int>         mode{1, 1};
    REQUIRE(ofd_stamp_slot(colour, multi, mode, 0, {"#ff0000", "#00ff00"}));
    CHECK(colour[0] == "#FF0000");
    CHECK(multi[0] == "#FF0000|#00FF00");
    CHECK(mode[0] == 0);
    CHECK(colour[1] == "#222222");
}

TEST_CASE("OFD stamp OOB and empty hexes leave vectors unchanged", "[Ofd]")
{
    std::vector<std::string> colour{"#111111"};
    std::vector<std::string> multi{""};
    std::vector<int>         mode{1};
    const auto               c0 = colour;
    const auto               m0 = multi;
    const auto               d0 = mode;
    CHECK_FALSE(ofd_stamp_slot(colour, multi, mode, 1, {"#ff0000"}));
    CHECK_FALSE(ofd_stamp_slot(colour, multi, mode, 0, {}));
    CHECK_FALSE(ofd_stamp_slot(colour, multi, mode, 0, {"not-a-hex"}));
    CHECK(colour == c0);
    CHECK(multi == m0);
    CHECK(mode == d0);
}

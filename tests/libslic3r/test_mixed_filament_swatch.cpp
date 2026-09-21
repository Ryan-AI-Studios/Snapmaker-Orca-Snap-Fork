#include <catch2/catch_test_macros.hpp>

#include "libslic3r/MixedFilamentSwatch.hpp"
#include "libslic3r/ColorSpace.hpp"

#include <boost/filesystem.hpp>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace Slic3r;

TEST_CASE("recipe keys encode pair/grad/phys and reject junk", "[MixedFilamentSwatch]")
{
    CHECK(snap_recipe_key_pair(1, 2, 40) == "pair:1:2:40");
    CHECK(snap_recipe_key_grad("1/2/3", "30/30/40") == "grad:1/2/3:30/30/40");
    CHECK(snap_recipe_key_phys(3) == "phys:3");
    CHECK(is_valid_swatch_recipe_key("pair:1:2:40"));
    CHECK(is_valid_swatch_recipe_key("grad:1/2/3:30/30/40"));
    CHECK(is_valid_swatch_recipe_key("phys:1"));
    CHECK_FALSE(is_valid_swatch_recipe_key("pair:1:2"));
    CHECK_FALSE(is_valid_swatch_recipe_key("stable:99"));
    CHECK_FALSE(is_valid_swatch_recipe_key(""));
}

TEST_CASE("JSON round-trip and CSV import the same three entries", "[MixedFilamentSwatch]")
{
    const std::string json = R"({
      "version": 1,
      "batch_key": "abc",
      "lighting": "D65 booth",
      "illuminant": "D65",
      "entries": [
        {"recipe_key": "pair:1:2:40", "L": 50.0, "a": 10.0, "b": -5.0},
        {"recipe_key": "pair:1:3:50", "L": 40.0, "a": 0.0, "b": 20.0},
        {"recipe_key": "grad:1/2/3:30/30/40", "L": 55.5, "a": 1.25, "b": 2.5}
      ]
    })";
    SwatchLut         lut;
    SwatchParseReport report;
    REQUIRE(parse_swatch_lut_json(json, lut, report));
    REQUIRE(report.ok);
    CHECK(report.accepted == 3);
    CHECK(lut.batch_key == "abc");
    CHECK(lut.lighting == "D65 booth");
    REQUIRE(measured_lab_for(lut, "pair:1:2:40").has_value());
    CHECK(measured_lab_for(lut, "pair:1:2:40")->L == 50.0);

    const std::string dumped = serialize_swatch_lut_json(lut);
    SwatchLut         again;
    SwatchParseReport r2;
    REQUIRE(parse_swatch_lut_json(dumped, again, r2));
    CHECK(r2.accepted == 3);
    CHECK(again.batch_key == "abc");

    const std::string csv =
        "# batch_key=abc\n"
        "recipe_key,L,a,b\n"
        "pair:1:2:40,50.0,10.0,-5.0\n"
        "pair:1:3:50,40.0,0.0,20.0\n"
        "grad:1/2/3:30/30/40,55.5,1.25,2.5\n";
    SwatchLut         csv_lut;
    SwatchParseReport csv_report;
    REQUIRE(parse_swatch_lut_csv(csv, csv_lut, csv_report));
    REQUIRE(csv_report.ok);
    CHECK(csv_report.accepted == 3);
    CHECK(csv_lut.batch_key == "abc");
    CHECK(measured_lab_for(csv_lut, "pair:1:3:50")->a == 0.0);
}

TEST_CASE("reject version 2; skip non-finite Lab and duplicate keys", "[MixedFilamentSwatch]")
{
    SwatchLut         lut;
    SwatchParseReport report;
    REQUIRE_FALSE(parse_swatch_lut_json(R"({"version":2,"entries":[]})", lut, report));
    CHECK_FALSE(report.ok);
    CHECK_FALSE(report.error.empty());

    const std::string json = R"({
      "version": 1,
      "entries": [
        {"recipe_key": "pair:1:2:40", "L": 50, "a": 0, "b": 0},
        {"recipe_key": "pair:1:2:40", "L": 10, "a": 0, "b": 0},
        {"recipe_key": "pair:1:2:41", "L": "nan", "a": 0, "b": 0},
        {"recipe_key": "nope", "L": 1, "a": 2, "b": 3}
      ]
    })";
    SwatchLut         lut2;
    SwatchParseReport r2;
    REQUIRE(parse_swatch_lut_json(json, lut2, r2));
    CHECK(r2.ok);
    CHECK(r2.accepted == 1);
    CHECK(r2.skipped_duplicate == 1);
    CHECK(r2.skipped_unknown_key >= 1);
}

TEST_CASE("batch-key mismatch uses predicted distance", "[MixedFilamentSwatch]")
{
    SwatchLut lut;
    lut.batch_key = compute_swatch_batch_key({"#FF0000", "#00FF00"});
    lut.entries.push_back({"pair:1:2:40", CIELab{50, 0, 0}});
    lut.index["pair:1:2:40"] = 0;

    const CIELab target{50, 0, 0};
    const CIELab predicted{10, 40, 0};
    const std::string live = compute_swatch_batch_key({"#0000FF", "#FFFFFF"});
    REQUIRE(lut_is_stale(lut, live));
    bool used = true;
    const double de = candidate_distance(target, predicted, &lut, "pair:1:2:40", live, &used);
    CHECK_FALSE(used);
    CHECK(de == delta_e00(target, predicted));
}

TEST_CASE("empty LUT ranking matches predicted-only", "[MixedFilamentSwatch]")
{
    const CIELab target{50, 0, 0};
    const CIELab pred_a{20, 10, 0};
    const CIELab pred_b{48, 1, 0};
    const std::string key_a = snap_recipe_key_pair(1, 2, 40);
    const std::string key_b = snap_recipe_key_pair(1, 3, 50);

    const double de_a = candidate_distance(target, pred_a, nullptr, key_a, "batch", nullptr);
    const double de_b = candidate_distance(target, pred_b, nullptr, key_b, "batch", nullptr);
    CHECK(de_b < de_a);

    SwatchLut empty;
    empty.batch_key = "batch";
    const double de_a2 = candidate_distance(target, pred_a, &empty, key_a, "batch", nullptr);
    const double de_b2 = candidate_distance(target, pred_b, &empty, key_b, "batch", nullptr);
    CHECK(de_a2 == de_a);
    CHECK(de_b2 == de_b);
    CHECK(de_b2 < de_a2);
}

TEST_CASE("measured hit shifts rank for that recipe only", "[MixedFilamentSwatch]")
{
    const CIELab target{50, 0, 0};
    const CIELab pred_a{20, 10, 0}; // worse predicted
    const CIELab pred_b{48, 1, 0};  // better predicted
    const std::string key_a = snap_recipe_key_pair(1, 2, 40);
    const std::string key_b = snap_recipe_key_pair(1, 3, 50);
    const std::string batch = compute_swatch_batch_key({"#FF0000"});

    SwatchLut lut;
    lut.batch_key = batch;
    lut.entries.push_back({key_a, target});
    lut.index[key_a] = 0;

    bool used_a = false, used_b = false;
    const double de_a = candidate_distance(target, pred_a, &lut, key_a, batch, &used_a);
    const double de_b = candidate_distance(target, pred_b, &lut, key_b, batch, &used_b);
    CHECK(used_a);
    CHECK_FALSE(used_b);
    CHECK(de_a < de_b);
}

TEST_CASE("atomic JSON save round-trips", "[MixedFilamentSwatch]")
{
    SwatchLut lut;
    lut.version   = 1;
    lut.batch_key = "deadbeef";
    lut.entries.push_back({"pair:1:2:25", CIELab{12.5, -3.0, 4.0}});
    lut.index["pair:1:2:25"] = 0;

    const auto dir  = boost::filesystem::temp_directory_path() / "snapfork-swatch-test";
    boost::filesystem::create_directories(dir);
    const auto path = (dir / "swatch_lut.json").string();
    std::string err;
    REQUIRE(save_swatch_lut(path, lut, &err));
    SwatchLut         loaded;
    SwatchParseReport report;
    REQUIRE(load_swatch_lut_file(path, loaded, report));
    CHECK(report.ok);
    CHECK(loaded.batch_key == "deadbeef");
    REQUIRE(measured_lab_for(loaded, "pair:1:2:25").has_value());
    CHECK(measured_lab_for(loaded, "pair:1:2:25")->L == 12.5);
}

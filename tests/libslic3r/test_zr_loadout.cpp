#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/SlotRemap.hpp"
#include "libslic3r/ZrToolheadLoadout.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleSelector.hpp"

#include "test_utils.hpp"

using namespace Slic3r;

namespace {

ZrToolheadLoadout sample_loadout()
{
    ZrToolheadLoadout l;
    l.filament_presets      = {"PLA Red", "PLA Green", "PLA Blue", "PLA White"};
    l.filament_colour       = {"#FF0000", "#00FF00", "#0000FF", "#FFFFFF"};
    l.filament_multi_colors = {"#FF0000", "#00FF00", "#0000FF", "#FFFFFF"};
    l.filament_colour_mode  = {0, 0, 0, 0};
    l.nozzle_diameter       = {0.4, 0.4, 0.6, 0.4};
    return l;
}

} // namespace

TEST_CASE("ZR loadout round-trips through in-memory AppConfig", "[ZrLoadout]")
{
    AppConfig cfg;
    const std::string name = kZrUltraSPreset04;
    const auto        original = sample_loadout();
    zr_loadout_save(cfg, name, original);
    CHECK_FALSE(cfg.get_printer_setting(name, "filament_colors").empty());
    CHECK(cfg.get_printer_setting(name, "filament_colour").empty());
    const auto loaded = zr_loadout_load(cfg, name);
    CHECK(loaded == original);
    CHECK(loaded.has_filament_presets());
    CHECK(loaded.has_colours());
    CHECK(loaded.has_nozzles());
}

TEST_CASE("ZR loadout empty colour record stays empty", "[ZrLoadout]")
{
    AppConfig cfg;
    const std::string name = kZrUltraSPreset04;
    cfg.set_printer_setting(name, "filament", "PLA Red");
    cfg.set_printer_setting(name, "filament_01", "PLA Green");
    cfg.set_printer_setting(name, "filament_02", "PLA Blue");
    cfg.set_printer_setting(name, "filament_03", "PLA White");
    cfg.set_printer_setting(name, "nozzle_diameter", "0.4,0.4,0.4,0.4");
    const auto loaded = zr_loadout_load(cfg, name);
    CHECK(loaded.has_filament_presets());
    CHECK_FALSE(loaded.has_colours());
    CHECK(loaded.filament_colour[0].empty());

    DynamicPrintConfig printer;
    printer.set_key_value("nozzle_diameter", new ConfigOptionFloats(std::vector<double>{0.6, 0.6, 0.6, 0.6}));
    const auto resolved = zr_loadout_resolve(cfg, name, &printer);
    CHECK(resolved.dest_colours.size() == 4);
    CHECK(resolved.dest_colours[0].empty());
    CHECK(resolved.dest_preset_names[0] == "PLA Red");
    // Nozzle key is present, so fallback is not used.
    CHECK_THAT(resolved.nozzle_diameter[0], Catch::Matchers::WithinAbs(0.4, 1e-9));
}

TEST_CASE("ZR loadout nozzle fallback uses printer preset config", "[ZrLoadout]")
{
    AppConfig cfg;
    const std::string name = kZrUltraSPreset04;
    cfg.set_printer_setting(name, "filament", "A");
    cfg.set_printer_setting(name, "filament_01", "B");
    cfg.set_printer_setting(name, "filament_02", "C");
    cfg.set_printer_setting(name, "filament_03", "D");
    cfg.set_printer_setting(name, "filament_colors", "#111111,#222222,#333333,#444444");

    DynamicPrintConfig printer;
    printer.set_key_value("nozzle_diameter", new ConfigOptionFloats(std::vector<double>{0.6, 0.6, 0.6, 0.6}));
    const auto resolved = zr_loadout_resolve(cfg, name, &printer);
    CHECK_THAT(resolved.nozzle_diameter[0], Catch::Matchers::WithinAbs(0.6, 1e-9));
    CHECK_THAT(resolved.nozzle_diameter[3], Catch::Matchers::WithinAbs(0.6, 1e-9));
    CHECK(resolved.dest_colours[0] == "#111111");
}

TEST_CASE("ZR loadout resolve destination is not a copy of source palette", "[ZrLoadout]")
{
    AppConfig cfg;
    const std::string name = kZrUltraSPreset04;
    cfg.set_printer_setting(name, "filament", "MachineA");
    cfg.set_printer_setting(name, "filament_01", "MachineB");
    cfg.set_printer_setting(name, "filament_02", "MachineC");
    cfg.set_printer_setting(name, "filament_03", "MachineD");
    cfg.set_printer_setting(name, "filament_colors", "#08ABFB,#D93B90,#F9ED3D,#9199A4");
    cfg.set_printer_setting(name, "nozzle_diameter", "0.4,0.4,0.4,0.4");

    const std::vector<std::string> source = {"#FF0000", "#00FF00", "#0000FF", "#FFFF00"};
    const auto resolved = zr_loadout_resolve(cfg, name, nullptr);
    REQUIRE(resolved.dest_colours.size() == 4);
    CHECK(resolved.dest_colours != source);
    CHECK(resolved.dest_colours[0] == "#08ABFB");
    CHECK(resolved.dest_preset_names[0] == "MachineA");
}

TEST_CASE("ZR loadout short colour csv is empty not padded", "[ZrLoadout]")
{
    AppConfig cfg;
    const std::string name = kZrUltraSPreset04;
    cfg.set_printer_setting(name, "filament_colors", "#FF0000,#00FF00");
    const auto loaded = zr_loadout_load(cfg, name);
    CHECK_FALSE(loaded.has_colours());
    CHECK(loaded.filament_colour[0].empty());
}

TEST_CASE("ZR loadout snapshot restore does not keep a replacement record", "[ZrLoadout]")
{
    AppConfig cfg;
    const std::string name = kZrUltraSPreset04;
    cfg.set_printer_setting(name, "filament", "SavedPLA");
    cfg.set_printer_setting(name, "filament_colors", "#010101,#020202,#030303,#040404");
    const auto snap = zr_loadout_snapshot_appconfig(cfg);

    cfg.clear_printer_settings(name);
    cfg.set_printer_setting(name, "filament", "FromFile");
    cfg.set_printer_setting(name, "filament_colors", "#AABBCC,#AABBCC,#AABBCC,#AABBCC");
    zr_loadout_restore_appconfig(cfg, snap);
    CHECK(cfg.get_printer_setting(name, "filament") == "SavedPLA");
    CHECK(cfg.get_printer_setting(name, "filament_colors") == "#010101,#020202,#030303,#040404");

    AppConfig none;
    const auto none_snap = zr_loadout_snapshot_appconfig(none);
    none.set_printer_setting(name, "filament", "Clobber");
    zr_loadout_restore_appconfig(none, none_snap);
    CHECK_FALSE(none.has_printer_settings(name));
}

TEST_CASE("ZR loadout install force shrinks extra slots to four", "[ZrLoadout]")
{
    std::vector<std::string> presets = {"A", "B", "C", "D", "E", "F"};
    DynamicPrintConfig project;
    project.set_key_value("filament_colour",
                          new ConfigOptionStrings(std::vector<std::string>{"#1", "#2", "#3", "#4", "#5", "#6"}));
    project.set_key_value("filament_multi_colors",
                          new ConfigOptionStrings(std::vector<std::string>{"#1", "#2", "#3", "#4", "#5", "#6"}));
    project.set_key_value("filament_colour_mode", new ConfigOptionInts(std::vector<int>{0, 0, 0, 0, 0, 0}));
    DynamicPrintConfig printer;
    printer.set_key_value("nozzle_diameter", new ConfigOptionFloats(std::vector<double>{0.4, 0.4, 0.4, 0.4}));
    const auto cap = zr_loadout_capture(presets, &project, &printer);
    std::vector<std::string> grown = presets;
    zr_loadout_install(grown, &project, &printer, cap, true, true);
    CHECK(grown.size() == 4);
    CHECK(project.option<ConfigOptionStrings>("filament_colour")->values.size() == 4);
    CHECK(grown[0] == "A");
}

TEST_CASE("ZR loadout lookup prefers the active ZR name over 0.4", "[ZrLoadout]")
{
    AppConfig cfg;
    cfg.set_printer_setting(kZrUltraSPreset04, "filament", "Old04");
    cfg.set_printer_setting("WonderMaker ZR Ultra S 0.6 nozzle", "filament", "Live06");
    CHECK(zr_loadout_lookup_name(cfg, "WonderMaker ZR Ultra S 0.6 nozzle") ==
          "WonderMaker ZR Ultra S 0.6 nozzle");
    CHECK(zr_loadout_lookup_name(cfg, "Bambu Lab X1 Carbon 0.4 nozzle") == kZrUltraSPreset04);
}

TEST_CASE("ZR loadout pick preset name from uniform nozzles", "[ZrLoadout]")
{
    ZrToolheadLoadout mixed = sample_loadout();
    CHECK(zr_loadout_pick_preset_name(mixed) == kZrUltraSPreset04);

    ZrToolheadLoadout uniform;
    uniform.nozzle_diameter = {0.6, 0.6, 0.6, 0.6};
    CHECK(zr_loadout_pick_preset_name(uniform) == "WonderMaker ZR Ultra S 0.6 nozzle");

    ZrToolheadLoadout empty;
    CHECK(zr_loadout_pick_preset_name(empty) == kZrUltraSPreset04);
}

TEST_CASE("ZR loadout parse hex accepts hash and bare RRGGBB", "[ZrLoadout]")
{
    unsigned char r = 0, g = 0, b = 0;
    REQUIRE(zr_loadout_parse_hex("#A1B2C3", r, g, b));
    CHECK(r == 0xA1);
    CHECK(g == 0xB2);
    CHECK(b == 0xC3);
    REQUIRE(zr_loadout_parse_hex("ff00aa", r, g, b));
    CHECK(r == 0xFF);
    CHECK(g == 0x00);
    CHECK(b == 0xAA);
    REQUIRE(zr_loadout_parse_hex("#FF0000FF", r, g, b));
    CHECK(r == 0xFF);
    CHECK(g == 0x00);
    CHECK(b == 0x00);
    REQUIRE(zr_loadout_parse_hex("#00FF00|#0000FF", r, g, b));
    CHECK(r == 0x00);
    CHECK(g == 0xFF);
    CHECK(b == 0x00);
    CHECK_FALSE(zr_loadout_parse_hex("xyz", r, g, b));
    CHECK_FALSE(zr_loadout_parse_hex("", r, g, b));
}

TEST_CASE("slot_remap_apply still leaves filament_colour unchanged", "[ZrLoadout][SlotRemap]")
{
    Model model;
    ModelObject *object = model.add_object();
    ModelVolume *volume = object->add_volume(make_cube(20., 20., 20.));
    object->add_instance();
    TriangleSelector selector(volume->mesh());
    selector.set_facet(0, EnforcerBlockerType(1));
    REQUIRE(volume->mmu_segmentation_facets.set(selector));

    DynamicPrintConfig cfg;
    const std::vector<std::string> colours = {"#111111", "#222222", "#333333", "#444444"};
    cfg.set_key_value("filament_colour", new ConfigOptionStrings(colours));
    SlotRemapMap map{{1, 2}};
    REQUIRE(slot_remap_apply(model, &cfg, map, 4));
    CHECK(cfg.option<ConfigOptionStrings>("filament_colour")->values == colours);
}

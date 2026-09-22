#include <catch2/catch_test_macros.hpp>

#include "libslic3r/CustomGCode.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/SlotRemap.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleSelector.hpp"

#include "test_utils.hpp"

using namespace Slic3r;

namespace {

ModelVolume *add_cube_volume(Model &model)
{
    ModelObject *object = model.add_object();
    object->name        = "slot-remap-cube";
    ModelVolume *volume = object->add_volume(make_cube(20., 20., 20.));
    object->add_instance();
    return volume;
}

void paint_facet(ModelVolume *volume, int extruder_state)
{
    TriangleSelector selector(volume->mesh());
    selector.set_facet(0, static_cast<EnforcerBlockerType>(extruder_state));
    REQUIRE(volume->mmu_segmentation_facets.set(selector));
}

bool used_state(const ModelVolume *volume, size_t id)
{
    const auto &states = volume->mmu_segmentation_facets.get_data().used_states;
    return id < states.size() && states[id];
}

DynamicPrintConfig make_feature_config(int wall, int sparse)
{
    DynamicPrintConfig cfg;
    cfg.set_key_value("wall_filament", new ConfigOptionInt(wall));
    cfg.set_key_value("sparse_infill_filament", new ConfigOptionInt(sparse));
    cfg.set_key_value("first_layer_print_sequence", new ConfigOptionInts(std::vector<int>{1, 2, 3, 4}));
    return cfg;
}

} // namespace

TEST_CASE("permutation remaps paint, configs, feature keys, gcodes, sequence", "[SlotRemap]")
{
    Model model;
    ModelVolume *volume = add_cube_volume(model);
    ModelObject *object = model.objects.front();
    paint_facet(volume, 1);
    object->config.set_key_value("extruder", new ConfigOptionInt(1));
    volume->config.set_key_value("extruder", new ConfigOptionInt(2));
    object->layer_config_ranges[{0.0, 1.0}].set_key_value("extruder", new ConfigOptionInt(3));

    CustomGCode::Item tc;
    tc.type     = CustomGCode::Type::ToolChange;
    tc.extruder = 4;
    tc.print_z  = 0.2;
    model.plates_custom_gcodes[0].gcodes.push_back(tc);

    DynamicPrintConfig cfg = make_feature_config(1, 4);
    const std::vector<std::string> colours_before = {"#FF0000", "#00FF00", "#0000FF", "#FFFF00"};
    cfg.set_key_value("filament_colour", new ConfigOptionStrings(colours_before));

    SlotRemapMap map{{1, 2}, {2, 1}, {3, 4}, {4, 3}};
    REQUIRE(slot_remap_apply(model, &cfg, map, 4));

    CHECK(used_state(volume, 2));
    CHECK_FALSE(used_state(volume, 1));
    CHECK(object->config.extruder() == 2);
    CHECK(volume->config.extruder() == 1);
    CHECK(object->layer_config_ranges[{0.0, 1.0}].extruder() == 4);
    CHECK(model.plates_custom_gcodes[0].gcodes.front().extruder == 3);
    CHECK(cfg.option<ConfigOptionInt>("wall_filament")->value == 2);
    CHECK(cfg.option<ConfigOptionInt>("sparse_infill_filament")->value == 3);
    CHECK(cfg.option<ConfigOptionInts>("first_layer_print_sequence")->values ==
          std::vector<int>({2, 1, 4, 3}));
    CHECK(cfg.option<ConfigOptionStrings>("filament_colour")->values == colours_before);
}

TEST_CASE("many-to-one maps two sources onto toolhead 1", "[SlotRemap]")
{
    Model model;
    ModelVolume *volume = add_cube_volume(model);
    TriangleSelector selector(volume->mesh());
    REQUIRE(volume->mesh().its.indices.size() >= 2);
    selector.set_facet(0, EnforcerBlockerType(1));
    selector.set_facet(1, EnforcerBlockerType(2));
    REQUIRE(volume->mmu_segmentation_facets.set(selector));

    SlotRemapMap map{{1, 1}, {2, 1}};
    REQUIRE(slot_remap_apply(model, nullptr, map, 4));
    CHECK(used_state(volume, 1));
    CHECK_FALSE(used_state(volume, 2));
}

TEST_CASE("sparse high-index sources remap; unused ids stay identity", "[SlotRemap]")
{
    Model model;
    ModelVolume *volume = add_cube_volume(model);
    TriangleSelector selector(volume->mesh());
    REQUIRE(volume->mesh().its.indices.size() >= 2);
    selector.set_facet(0, EnforcerBlockerType(2));
    selector.set_facet(1, EnforcerBlockerType(8));
    REQUIRE(volume->mmu_segmentation_facets.set(selector));

    SlotRemapMap map{{2, 1}, {8, 2}};
    REQUIRE(slot_remap_apply(model, nullptr, map, 4));
    CHECK(used_state(volume, 1));
    CHECK(used_state(volume, 2));
    CHECK_FALSE(used_state(volume, 8));
}

TEST_CASE("identity map is a zero-mutation no-op", "[SlotRemap]")
{
    Model model;
    ModelVolume *volume = add_cube_volume(model);
    paint_facet(volume, 1);
    ModelObject *object = model.objects.front();
    object->config.set_key_value("extruder", new ConfigOptionInt(2));
    DynamicPrintConfig cfg = make_feature_config(3, 4);

    SlotRemapMap map{{1, 1}, {2, 2}, {3, 3}, {4, 4}};
    REQUIRE(slot_remap_is_identity(map));
    REQUIRE(slot_remap_apply(model, &cfg, map, 4));
    CHECK(used_state(volume, 1));
    CHECK(object->config.extruder() == 2);
    CHECK(cfg.option<ConfigOptionInt>("wall_filament")->value == 3);
    CHECK(cfg.option<ConfigOptionInts>("first_layer_print_sequence")->values ==
          std::vector<int>({1, 2, 3, 4}));
}

TEST_CASE("empty map is identity", "[SlotRemap]")
{
    Model model;
    ModelVolume *volume = add_cube_volume(model);
    paint_facet(volume, 3);
    SlotRemapMap empty;
    REQUIRE(slot_remap_is_identity(empty));
    REQUIRE(slot_remap_apply(model, nullptr, empty, 4));
    CHECK(used_state(volume, 3));
}

TEST_CASE("used source with dest 0 fails and leaves the model unchanged", "[SlotRemap]")
{
    Model model;
    ModelVolume *volume = add_cube_volume(model);
    paint_facet(volume, 1);
    DynamicPrintConfig cfg = make_feature_config(1, 2);
    SlotRemapMap map{{1, 0}};
    REQUIRE_FALSE(slot_remap_apply(model, &cfg, map, 4));
    CHECK(used_state(volume, 1));
    CHECK(cfg.option<ConfigOptionInt>("wall_filament")->value == 1);
}

TEST_CASE("dest 5 with dest_count 4 fails unchanged", "[SlotRemap]")
{
    Model model;
    ModelVolume *volume = add_cube_volume(model);
    paint_facet(volume, 1);
    SlotRemapMap map{{1, 5}};
    REQUIRE_FALSE(slot_remap_apply(model, nullptr, map, 4));
    CHECK(used_state(volume, 1));
}

TEST_CASE("should_prompt_remap_four_color_project gates", "[SlotRemap]")
{
    SlotRemapPromptInput base;
    base.unique_color_count   = 4;
    base.has_assignments      = true;
    base.zr_ultra_s           = true;
    base.physical_count       = 4;
    base.source_printer_model = "Bambu Lab X1 Carbon";
    base.source_colours       = {"#FF0000", "#00FF00", "#0000FF", "#FFFF00"};
    base.dest_colours         = {"#08ABFB", "#D93B90", "#F9ED3D", "#9199A4"};
    base.used_ids             = {1, 2, 3, 4};
    CHECK(should_prompt_remap_four_color_project(base));

    SlotRemapPromptInput too_many = base;
    too_many.unique_color_count   = 5;
    CHECK_FALSE(should_prompt_remap_four_color_project(too_many));

    SlotRemapPromptInput mixes = base;
    mixes.enabled_mix_count    = 1;
    CHECK_FALSE(should_prompt_remap_four_color_project(mixes));

    SlotRemapPromptInput native = base;
    native.source_printer_model = "WonderMaker ZR Ultra S";
    CHECK_FALSE(should_prompt_remap_four_color_project(native));

    SlotRemapPromptInput native_out = native;
    native_out.used_ids             = {1, 5};
    CHECK(should_prompt_remap_four_color_project(native_out));

    SlotRemapPromptInput silence = base;
    silence.silence              = true;
    CHECK_FALSE(should_prompt_remap_four_color_project(silence));
}

TEST_CASE("slot_remap_apply leaves filament_colour unchanged", "[SlotRemap]")
{
    Model model;
    ModelVolume *volume = add_cube_volume(model);
    paint_facet(volume, 1);
    DynamicPrintConfig cfg;
    const std::vector<std::string> colours = {"#111111", "#222222", "#333333", "#444444"};
    cfg.set_key_value("filament_colour", new ConfigOptionStrings(colours));
    SlotRemapMap map{{1, 2}};
    REQUIRE(slot_remap_apply(model, &cfg, map, 4));
    CHECK(cfg.option<ConfigOptionStrings>("filament_colour")->values == colours);
    CHECK(used_state(volume, 2));
}

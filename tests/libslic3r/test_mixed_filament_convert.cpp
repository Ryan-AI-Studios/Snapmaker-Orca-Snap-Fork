#include <catch2/catch_test_macros.hpp>

#include "test_utils.hpp"
#include "libslic3r/MixedFilamentConvert.hpp"
#include "libslic3r/MixedFilament.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleSelector.hpp"

using namespace Slic3r;

namespace {

ModelVolume *add_cube_volume(Model &model)
{
    ModelObject *object = model.add_object();
    object->name        = "convert-paint-cube";
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

} // namespace

TEST_CASE("normalize_painted_colour_hex folds case, hash, and alpha", "[MixedFilamentConvert]")
{
    CHECK(normalize_painted_colour_hex("#ff0000") == "#FF0000");
    CHECK(normalize_painted_colour_hex("00ff00") == "#00FF00");
    CHECK(normalize_painted_colour_hex("#0000FFAA") == "#0000FF");
    CHECK(normalize_painted_colour_hex("  #Abc ") == "#AABBCC");
    CHECK(normalize_painted_colour_hex("not-a-color").empty());
}

TEST_CASE("should_prompt_convert_painted_colours: 4 colours ineligible", "[MixedFilamentConvert]")
{
    Model model;
    ModelVolume *volume = add_cube_volume(model);
    paint_facet(volume, 1);

    const std::vector<std::string> colours = {"#FF0000", "#00FF00", "#0000FF", "#FFFF00"};
    PaintedSourcePalette palette           = capture_painted_source_palette(model, colours, nullptr);
    palette.filament_n                     = 4;
    CHECK(palette.paint_nonempty);
    CHECK(palette.unique_color_count() <= 4);
    CHECK_FALSE(should_prompt_convert_painted_colours(palette, false));
}

TEST_CASE("should_prompt_convert_painted_colours: 5+ colours eligible", "[MixedFilamentConvert]")
{
    Model model;
    ModelVolume *volume = add_cube_volume(model);
    TriangleSelector selector(volume->mesh());
    // Cube has many facets; paint several states on the first few.
    const int n_facets = static_cast<int>(volume->mesh().its.indices.size());
    REQUIRE(n_facets >= 5);
    for (int i = 0; i < 5; ++i)
        selector.set_facet(i, static_cast<EnforcerBlockerType>(i + 1));
    REQUIRE(volume->mmu_segmentation_facets.set(selector));

    const std::vector<std::string> colours = {
        "#FF0000", "#00FF00", "#0000FF", "#FFFF00", "#00FFFF"};
    const PaintedSourcePalette palette = capture_painted_source_palette(model, colours, nullptr);
    CHECK(palette.paint_nonempty);
    CHECK(palette.unique_color_count() >= 5);
    CHECK(should_prompt_convert_painted_colours(palette, false));
    CHECK_FALSE(should_prompt_convert_painted_colours(palette, true));
}

TEST_CASE("should_prompt_convert_painted_colours: mixes already present skip", "[MixedFilamentConvert]")
{
    Model model;
    ModelVolume *volume = add_cube_volume(model);
    TriangleSelector selector(volume->mesh());
    const int n_facets = static_cast<int>(volume->mesh().its.indices.size());
    REQUIRE(n_facets >= 5);
    for (int i = 0; i < 5; ++i)
        selector.set_facet(i, static_cast<EnforcerBlockerType>(i + 1));
    REQUIRE(volume->mmu_segmentation_facets.set(selector));

    const std::vector<std::string> colours = {
        "#FF0000", "#00FF00", "#0000FF", "#FFFF00", "#00FFFF"};
    MixedFilamentManager mgr;
    mgr.add_custom_filament(1, 2, 50, colours);
    REQUIRE(mgr.enabled_count() == 1);

    const PaintedSourcePalette palette = capture_painted_source_palette(model, colours, &mgr);
    CHECK(palette.enabled_mix_count == 1);
    CHECK_FALSE(should_prompt_convert_painted_colours(palette, false));
}

TEST_CASE("capture_painted_source_palette survives a simulated 4-tool clip", "[MixedFilamentConvert]")
{
    Model model;
    ModelVolume *volume = add_cube_volume(model);
    TriangleSelector selector(volume->mesh());
    const int n_facets = static_cast<int>(volume->mesh().its.indices.size());
    REQUIRE(n_facets >= 6);
    for (int i = 0; i < 6; ++i)
        selector.set_facet(i, static_cast<EnforcerBlockerType>(i + 1));
    REQUIRE(volume->mmu_segmentation_facets.set(selector));

    const std::vector<std::string> colours = {
        "#AA0000", "#00AA00", "#0000AA", "#AAAA00", "#00AAAA", "#AA00AA"};
    const PaintedSourcePalette before = capture_painted_source_palette(model, colours, nullptr);
    REQUIRE(before.unique_color_count() >= 6);

    Model clipped = model;
    REQUIRE(clipped.objects.size() == 1);
    REQUIRE(clipped.objects.front()->volumes.size() == 1);
    clipped.objects.front()->volumes.front()->update_extruder_count(4);

    CHECK(before.colors[4].hex == "#00AAAA");
    CHECK(before.colors[5].hex == "#AA00AA");
    // Clip mutates the copy only; the captured palette still holds the original hexes.
    const PaintedSourcePalette after_clip = capture_painted_source_palette(clipped, colours, nullptr);
    CHECK(after_clip.unique_color_count() < before.unique_color_count());
}

TEST_CASE("plan_convert_slot_cap never silently drops without a kept fallback", "[MixedFilamentConvert]")
{
    const ConvertCapPlan plan = plan_convert_slot_cap(4, 70);
    CHECK(plan.accepted_mixes == MAXIMUM_FILAMENT_NUMBER - 4);
    CHECK(plan.dropped_mixes == 70 - plan.accepted_mixes);
    CHECK(plan.fallback_filament_id == static_cast<unsigned int>(4 + plan.accepted_mixes));
    CHECK(plan.fallback_filament_id != 0);

    std::vector<unsigned int> assigned(70, 0);
    for (size_t i = 0; i < plan.accepted_mixes; ++i)
        assigned[i] = static_cast<unsigned int>(5 + i);
    const size_t rewritten = apply_convert_cap_fallbacks(assigned, plan);
    CHECK(rewritten == plan.dropped_mixes);
    for (unsigned int id : assigned)
        CHECK(id != 0);
}

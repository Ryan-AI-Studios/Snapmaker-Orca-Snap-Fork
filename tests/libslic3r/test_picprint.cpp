#include <catch2/catch_test_macros.hpp>

#include "libslic3r/PicPrint.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleSelector.hpp"

#include <cmath>
#include <cstdint>
#include <set>
#include <vector>

using namespace Slic3r;

namespace {

void put_rgb(std::vector<std::uint8_t> &rgb, int w, int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    const size_t i = (size_t(y) * size_t(w) + size_t(x)) * 3;
    rgb[i]         = r;
    rgb[i + 1]     = g;
    rgb[i + 2]     = b;
}

std::vector<std::uint8_t> two_colour_8x2()
{
    std::vector<std::uint8_t> rgb(size_t(8) * 2 * 3, 0);
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 8; ++x) {
            if (x < 4)
                put_rgb(rgb, 8, x, y, 0x08, 0xAB, 0xFB);
            else
                put_rgb(rgb, 8, x, y, 0xD9, 0x3B, 0x90);
        }
    }
    return rgb;
}

std::vector<std::uint8_t> yflip_8x2()
{
    std::vector<std::uint8_t> rgb(size_t(8) * 2 * 3, 0);
    for (int x = 0; x < 8; ++x) {
        put_rgb(rgb, 8, x, 0, 0xFF, 0x00, 0x00); // top image row
        put_rgb(rgb, 8, x, 1, 0x00, 0x00, 0xFF); // bottom image row
    }
    return rgb;
}

std::vector<std::uint8_t> four_colour_8x2()
{
    std::vector<std::uint8_t> rgb(size_t(8) * 2 * 3, 0);
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 8; ++x) {
            if (x < 2)
                put_rgb(rgb, 8, x, y, 0xFF, 0x00, 0x00);
            else if (x < 4)
                put_rgb(rgb, 8, x, y, 0x00, 0xFF, 0x00);
            else if (x < 6)
                put_rgb(rgb, 8, x, y, 0x00, 0x00, 0xFF);
            else
                put_rgb(rgb, 8, x, y, 0xFF, 0xFF, 0x00);
        }
    }
    return rgb;
}

bool has_state(const ModelVolume &v, EnforcerBlockerType st)
{
    TriangleSelector sel(v.mesh());
    sel.deserialize(v.mmu_segmentation_facets.get_data(), true);
    return sel.num_facets(st) > 0;
}

} // namespace

TEST_CASE("PicPrint 8x2 two-colour buffer clusters and samples", "[PicPrint]")
{
    const std::vector<std::uint8_t> rgb  = two_colour_8x2();
    const PicPrintPlan              plan = plan_picprint(rgb.data(), 8, 2, 4, 0, 16);
    REQUIRE(plan.valid);
    REQUIRE(plan.error.empty());
    REQUIRE(plan.width == 8);
    REQUIRE(plan.height == 2);
    REQUIRE(plan.cluster_count == 2);
    const unsigned left  = picprint_sample_cluster(plan, 0.1, 0.5);
    const unsigned right = picprint_sample_cluster(plan, 0.9, 0.5);
    CHECK(left != right);
    CHECK(picprint_sample_dest(plan, 0.1, 0.5) != picprint_sample_dest(plan, 0.9, 0.5));
}

TEST_CASE("PicPrint Y-flip samples bottom image row at v=0", "[PicPrint]")
{
    const std::vector<std::uint8_t> rgb  = yflip_8x2();
    const PicPrintPlan              plan = plan_picprint(rgb.data(), 8, 2, 4, 0, 16);
    REQUIRE(plan.valid);
    REQUIRE(plan.cluster_count == 2);
    const unsigned bottom = picprint_sample_cluster(plan, 0.5, 0.0);
    const unsigned top    = picprint_sample_cluster(plan, 0.5, 1.0);
    CHECK(bottom != top);
    // v=0 is the bottom image row (Y-flip): py = (1-v)*(h-1)
    CHECK(picprint_sample_cluster(plan, 0.5, 0.0) == picprint_sample_cluster(plan, 0.5, 0.0));
}

TEST_CASE("PicPrint world XY uses passed bbox min/max", "[PicPrint]")
{
    BoundingBoxf3 box(Vec3d(0, 0, 0), Vec3d(10, 10, 10));
    double        u = 0;
    double        v = 0;
    REQUIRE(picprint_world_to_uv(Vec3d(0, 0, 0), box, u, v));
    CHECK(std::abs(u) < 1e-9);
    CHECK(std::abs(v) < 1e-9);
    REQUIRE(picprint_world_to_uv(Vec3d(10, 10, 0), box, u, v));
    CHECK(std::abs(u - 1.0) < 1e-9);
    CHECK(std::abs(v - 1.0) < 1e-9);

    BoundingBoxf3 shifted(Vec3d(5, 7, 0), Vec3d(15, 17, 1));
    REQUIRE(picprint_world_to_uv(Vec3d(5, 7, 0), shifted, u, v));
    CHECK(std::abs(u) < 1e-9);
    CHECK(std::abs(v) < 1e-9);
    REQUIRE(picprint_world_to_uv(Vec3d(15, 17, 0), shifted, u, v));
    CHECK(std::abs(u - 1.0) < 1e-9);
    CHECK(std::abs(v - 1.0) < 1e-9);

    BoundingBoxf3 thin_x(Vec3d(0, 0, 0), Vec3d(1e-9, 10, 10));
    CHECK_FALSE(picprint_world_to_uv(Vec3d(0, 5, 0), thin_x, u, v));
    BoundingBoxf3 thin_y(Vec3d(0, 0, 0), Vec3d(10, 1e-9, 10));
    CHECK_FALSE(picprint_world_to_uv(Vec3d(5, 0, 0), thin_y, u, v));
}

TEST_CASE("PicPrint apply paints original facets of a box", "[PicPrint]")
{
    const std::vector<std::uint8_t> rgb  = two_colour_8x2();
    const PicPrintPlan              plan = plan_picprint(rgb.data(), 8, 2, 4, 0, 16);
    REQUIRE(plan.valid);

    Model        model;
    ModelObject *object = model.add_object();
    ModelVolume *volume = object->add_volume(make_cube(20., 20., 20.));
    object->add_instance();
    const size_t          ntri  = volume->mesh().its.indices.size();
    const Transform3d     world = object->instances.front()->get_matrix() * volume->get_matrix();
    const BoundingBoxf3   bbox  = object->instance_bounding_box(*object->instances.front());
    std::vector<unsigned> dest_map(plan.cluster_count, 1);
    for (size_t i = 0; i < dest_map.size(); ++i)
        dest_map[i] = static_cast<unsigned>(i + 1);
    PicPrintPlan painted = plan;
    picprint_set_dest_ids(painted, dest_map);
    REQUIRE(picprint_apply_to_volume(*volume, world, bbox, painted, false));
    CHECK(volume->mesh().its.indices.size() == ntri);
    TriangleSelector sel(volume->mesh());
    sel.deserialize(volume->mmu_segmentation_facets.get_data(), true);
    std::set<int> used;
    for (int i = 0; i < int(ntri); ++i)
        used.insert(int(sel.orig_facet_state(i)));
    CHECK(used.size() >= 2);
}

TEST_CASE("PicPrint front-face paints +Z and keeps pre-painted back", "[PicPrint]")
{
    const std::vector<std::uint8_t> rgb  = two_colour_8x2();
    const PicPrintPlan              plan = plan_picprint(rgb.data(), 8, 2, 4, 0, 16);
    REQUIRE(plan.valid);

    Model        model;
    ModelObject *object = model.add_object();
    ModelVolume *volume = object->add_volume(make_cube(20., 20., 20.));
    object->add_instance();
    TriangleSelector pre(volume->mesh());
    for (int i = 0; i < int(volume->mesh().its.indices.size()); ++i)
        pre.set_facet(i, EnforcerBlockerType::Extruder1);
    REQUIRE(volume->mmu_segmentation_facets.set(pre));

    const Transform3d   world = object->instances.front()->get_matrix() * volume->get_matrix();
    const BoundingBoxf3 bbox  = object->instance_bounding_box(*object->instances.front());
    size_t              skipped = 0;
    REQUIRE(picprint_apply_to_volume(*volume, world, bbox, plan, true, &skipped));
    CHECK(skipped > 0);
    CHECK(has_state(*volume, EnforcerBlockerType::Extruder1));

    const auto &its     = volume->mesh().its;
    const auto  normals = its_face_normals(its);
    TriangleSelector sel(volume->mesh());
    sel.deserialize(volume->mmu_segmentation_facets.get_data(), true);
    bool painted_front = false;
    bool back_kept     = false;
    for (int i = 0; i < int(its.indices.size()); ++i) {
        const bool front = picprint_is_front_face(normals[size_t(i)].cast<double>(), world);
        const auto st    = sel.orig_facet_state(i);
        if (front)
            painted_front = painted_front || (st != EnforcerBlockerType::NONE && st != EnforcerBlockerType::Extruder1);
        else
            back_kept = back_kept || (st == EnforcerBlockerType::Extruder1);
    }
    CHECK(painted_front);
    CHECK(back_kept);
    CHECK(picprint_is_front_face(Vec3d(0, 0, 1), world));
    CHECK_FALSE(picprint_is_front_face(Vec3d(0, 0, -1), world));
    CHECK_FALSE(picprint_is_front_face(Vec3d(1, 0, 0), world));
}

TEST_CASE("PicPrint capacity room 0 fails and overflow collapses", "[PicPrint]")
{
    const std::vector<std::uint8_t> rgb = four_colour_8x2();
    const PicPrintPlan none = plan_picprint(rgb.data(), 8, 2, MAXIMUM_FILAMENT_NUMBER, 0, 16);
    CHECK_FALSE(none.valid);
    CHECK_FALSE(none.error.empty());

    const PicPrintPlan overflow = plan_picprint(rgb.data(), 8, 2, MAXIMUM_FILAMENT_NUMBER - 2, 0, 16);
    REQUIRE(overflow.valid);
    CHECK(overflow.cluster_count <= 2);
    CHECK(overflow.collapsed_clusters > 0);
    for (unsigned d : overflow.dest_id)
        CHECK(d >= 1);
}

TEST_CASE("PicPrint existing mix count offsets dest ids", "[PicPrint]")
{
    const std::vector<std::uint8_t> rgb  = two_colour_8x2();
    const PicPrintPlan              plan = plan_picprint(rgb.data(), 8, 2, 4, 3, 16);
    REQUIRE(plan.valid);
    REQUIRE_FALSE(plan.dest_id.empty());
    unsigned min_d = plan.dest_id.front();
    for (unsigned d : plan.dest_id) {
        CHECK(d >= 8); // 4 physical + 3 existing mixes + 1
        if (d < min_d)
            min_d = d;
        CHECK(d != 0);
    }
    CHECK(min_d >= 8);
}

TEST_CASE("PicPrint thin bbox refuses apply", "[PicPrint]")
{
    const std::vector<std::uint8_t> rgb  = two_colour_8x2();
    const PicPrintPlan              plan = plan_picprint(rgb.data(), 8, 2, 4, 0, 16);
    REQUIRE(plan.valid);
    Model        model;
    ModelObject *object = model.add_object();
    ModelVolume *volume = object->add_volume(make_cube(20., 20., 20.));
    BoundingBoxf3 thin(Vec3d(0, 0, 0), Vec3d(1e-9, 20, 20));
    CHECK_FALSE(picprint_apply_to_volume(*volume, Transform3d::Identity(), thin, plan, false));
    CHECK(volume->mmu_segmentation_facets.empty());
}

TEST_CASE("PicPrint fewer than two physicals fails", "[PicPrint]")
{
    const std::vector<std::uint8_t> rgb  = two_colour_8x2();
    const PicPrintPlan              plan = plan_picprint(rgb.data(), 8, 2, 1, 0, 16);
    CHECK_FALSE(plan.valid);
}

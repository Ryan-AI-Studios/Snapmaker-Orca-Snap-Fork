#include <catch2/catch_test_macros.hpp>

#include "libslic3r/PaintReproject.hpp"
#include "libslic3r/CutUtils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/Geometry.hpp"

#include <atomic>

using namespace Slic3r;

namespace {

Vec3f centroid(const indexed_triangle_set &its, int face)
{
    const auto &t = its.indices[face];
    return (its.vertices[t(0)] + its.vertices[t(1)] + its.vertices[t(2)]) / 3.f;
}

void paint_by_x(TriangleSelector &sel, const TriangleMesh &mesh, float split_x)
{
    for (int i = 0; i < int(mesh.its.indices.size()); ++i) {
        const Vec3f c = centroid(mesh.its, i);
        sel.set_facet(i, c.x() < split_x ? EnforcerBlockerType::Extruder1 : EnforcerBlockerType::Extruder2);
    }
}

ModelVolume *add_painted_cube(Model &model, bool two_colour)
{
    ModelObject *object = model.add_object();
    object->name        = "paint-reproject-cube";
    TriangleMesh mesh   = make_cube(20., 20., 20.);
    ModelVolume *volume = object->add_volume(mesh);
    object->add_instance();
    TriangleSelector sel(volume->mesh());
    if (two_colour)
        paint_by_x(sel, volume->mesh(), 10.f);
    else {
        for (int i = 0; i < int(volume->mesh().its.indices.size()); ++i)
            sel.set_facet(i, EnforcerBlockerType::Extruder1);
    }
    REQUIRE(volume->mmu_segmentation_facets.set(sel));
    return volume;
}

bool has_state(const ModelVolume &v, EnforcerBlockerType st)
{
    TriangleSelector sel(v.mesh());
    sel.deserialize(v.mmu_segmentation_facets.get_data(), true);
    return sel.num_facets(st) > 0;
}

} // namespace

TEST_CASE("cut_mesh records source faces and caps as -1", "[PaintReproject]")
{
    TriangleMesh mesh = make_cube(20., 20., 20.);
    indexed_triangle_set upper, lower;
    std::vector<int>     upper_src, lower_src;
    cut_mesh(mesh.its, 10.f, &upper, &lower, true, &upper_src, &lower_src);
    REQUIRE(upper_src.size() == upper.indices.size());
    REQUIRE(lower_src.size() == lower.indices.size());
    const int nsrc = int(mesh.its.indices.size());
    bool      saw_mapped = false;
    bool      saw_cap    = false;
    for (int s : upper_src) {
        if (s >= 0) {
            saw_mapped = true;
            CHECK(s < nsrc);
        } else
            saw_cap = true;
    }
    CHECK(saw_mapped);
    CHECK(saw_cap);
}

TEST_CASE("planar Cut keeps two colours and leaves caps NONE", "[PaintReproject]")
{
    Model model;
    add_painted_cube(model, true);
    Cut cut(model.objects.front(), 0, Geometry::translation_transform(10. * Vec3d::UnitZ()));
    cut.set_keep_painting(true);
    const ModelObjectPtrs &objs = cut.perform_with_plane();
    REQUIRE(objs.size() >= 1);
    bool e1 = false, e2 = false, none_caps = false;
    for (const ModelObject *o : objs) {
        for (const ModelVolume *v : o->volumes) {
            if (!v->is_model_part())
                continue;
            e1 |= has_state(*v, EnforcerBlockerType::Extruder1);
            e2 |= has_state(*v, EnforcerBlockerType::Extruder2);
            TriangleSelector sel(v->mesh());
            sel.deserialize(v->mmu_segmentation_facets.get_data(), true);
            none_caps |= sel.num_facets(EnforcerBlockerType::NONE) > 0;
        }
    }
    CHECK(e1);
    CHECK(e2);
    CHECK(none_caps);
}

TEST_CASE("uniform paint survives Cut", "[PaintReproject]")
{
    Model model;
    add_painted_cube(model, false);
    Cut cut(model.objects.front(), 0, Geometry::translation_transform(10. * Vec3d::UnitZ()));
    cut.set_keep_painting(true);
    const ModelObjectPtrs &objs = cut.perform_with_plane();
    bool any = false;
    for (const ModelObject *o : objs) {
        for (const ModelVolume *v : o->volumes) {
            if (!v->is_model_part())
                continue;
            CHECK(has_state(*v, EnforcerBlockerType::Extruder1));
            CHECK_FALSE(has_state(*v, EnforcerBlockerType::Extruder2));
            any = true;
        }
    }
    CHECK(any);
}

TEST_CASE("keep_painting off wipes paint on Cut", "[PaintReproject]")
{
    Model model;
    add_painted_cube(model, true);
    Cut cut(model.objects.front(), 0, Geometry::translation_transform(10. * Vec3d::UnitZ()));
    cut.set_keep_painting(false);
    const ModelObjectPtrs &objs = cut.perform_with_plane();
    for (const ModelObject *o : objs) {
        for (const ModelVolume *v : o->volumes) {
            if (!v->is_model_part())
                continue;
            CHECK(v->mmu_segmentation_facets.empty());
        }
    }
}

TEST_CASE("spatial reproject: far dest faces stay NONE", "[PaintReproject]")
{
    TriangleMesh src = make_cube(20., 20., 20.);
    TriangleSelector sel(src);
    paint_by_x(sel, src, 10.f);
    TriangleMesh dest = src;
    dest.translate(Vec3f(0.f, 0.f, 1000.f));
    TriangleSelector out(dest);
    REQUIRE(reproject_painting_spatial(src, sel.serialize(), dest, out, 1.0f, nullptr));
    CHECK(out.num_facets(EnforcerBlockerType::Extruder1) == 0);
    CHECK(out.num_facets(EnforcerBlockerType::Extruder2) == 0);
}

TEST_CASE("spatial reproject: nearby dest keeps both colours", "[PaintReproject]")
{
    TriangleMesh src = make_cube(20., 20., 20.);
    TriangleSelector sel(src);
    paint_by_x(sel, src, 10.f);
    TriangleMesh dest = src;
    TriangleSelector out(dest);
    REQUIRE(reproject_painting_spatial(src, sel.serialize(), dest, out, 1.0f, nullptr));
    CHECK(out.num_facets(EnforcerBlockerType::Extruder1) > 0);
    CHECK(out.num_facets(EnforcerBlockerType::Extruder2) > 0);
}

TEST_CASE("cancel aborts reproject without leaving paint", "[PaintReproject]")
{
    TriangleMesh src = make_cube(20., 20., 20.);
    TriangleSelector sel(src);
    paint_by_x(sel, src, 10.f);
    TriangleMesh dest = src;
    TriangleSelector out(dest);
    std::atomic<bool> cancel{true};
    CHECK_FALSE(reproject_painting_spatial(src, sel.serialize(), dest, out, 1.0f, &cancel));
    CHECK(out.num_facets(EnforcerBlockerType::Extruder1) == 0);
    out.split_dest_triangle(0);
}

TEST_CASE("SavedPainting snapshots all four channels", "[PaintReproject]")
{
    Model model;
    ModelVolume *v = add_painted_cube(model, true);
    TriangleSelector sup(v->mesh());
    sup.set_facet(0, EnforcerBlockerType::ENFORCER);
    REQUIRE(v->supported_facets.set(sup));
    SavedPainting snap = snapshot_volume_painting(*v);
    CHECK_FALSE(snap.mmu.triangles_to_split.empty());
    CHECK_FALSE(snap.supported.triangles_to_split.empty());
    CHECK(snap.seam.triangles_to_split.empty());
}

TEST_CASE("FacetsAnnotation round-trips after Cut keep-paint", "[PaintReproject]")
{
    Model model;
    add_painted_cube(model, true);
    Cut cut(model.objects.front(), 0, Geometry::translation_transform(10. * Vec3d::UnitZ()));
    cut.set_keep_painting(true);
    const ModelObjectPtrs &objs = cut.perform_with_plane();
    REQUIRE_FALSE(objs.empty());
    const ModelVolume *v = nullptr;
    for (const ModelVolume *vol : objs.front()->volumes) {
        if (vol->is_model_part() && !vol->mmu_segmentation_facets.empty()) {
            v = vol;
            break;
        }
    }
    REQUIRE(v != nullptr);
    TriangleSelector restored(v->mesh());
    restored.deserialize(v->mmu_segmentation_facets.get_data(), true);
    CHECK((restored.has_facets(EnforcerBlockerType::Extruder1) || restored.has_facets(EnforcerBlockerType::Extruder2)));
}

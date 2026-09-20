#include "PaintReproject.hpp"
#include "Model.hpp"
#include "AABBTreeIndirect.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r {

namespace {

Vec3f face_centroid(const indexed_triangle_set &its, int face)
{
    const auto &t = its.indices[face];
    return (its.vertices[t(0)] + its.vertices[t(1)] + its.vertices[t(2)]) / 3.f;
}

bool canceled(const std::atomic<bool> *cancel)
{
    return cancel != nullptr && cancel->load();
}

void load_source_selector(TriangleSelector &sel, const TriangleSelector::TriangleSplittingData &data)
{
    if (data.triangles_to_split.empty())
        return;
    sel.deserialize(data, true);
}

bool reproject_channel_from_faces(
    const TriangleMesh                            &source_mesh,
    const TriangleSelector::TriangleSplittingData &src_data,
    const TriangleMesh                            &dest_mesh,
    const std::vector<int>                        &dest_source_face,
    TriangleSelector                              &dest,
    const std::atomic<bool>                       *cancel)
{
    dest.reset();
    dest.set_edge_limit(kPaintReprojectBoundaryMm);
    if (canceled(cancel))
        return false;

    TriangleSelector src(source_mesh);
    load_source_selector(src, src_data);

    const size_t n = dest_mesh.its.indices.size();
    const size_t nmap = std::min(n, dest_source_face.size());
    for (size_t i = 0; i < n; ++i) {
        if (canceled(cancel)) {
            dest.reset();
            return false;
        }
        const int src_face = (i < nmap) ? dest_source_face[i] : -1;
        EnforcerBlockerType state = EnforcerBlockerType::NONE;
        if (src_face >= 0) {
            state = src.orig_facet_state(src_face);
            if (state == EnforcerBlockerType::NONE) {
                const Vec3f c = face_centroid(dest_mesh.its, int(i));
                state = src.state_at_point(src_face, c);
            }
        }
        dest.set_facet(int(i), state);
    }
    return true;
}

} // namespace

SavedPainting snapshot_volume_painting(const ModelVolume &volume)
{
    SavedPainting out;
    out.supported = volume.supported_facets.get_data();
    out.seam      = volume.seam_facets.get_data();
    out.mmu       = volume.mmu_segmentation_facets.get_data();
    out.fuzzy     = volume.fuzzy_skin_facets.get_data();
    return out;
}

bool reproject_painting_from_faces(
    const TriangleMesh                            &source_mesh,
    const TriangleSelector::TriangleSplittingData &src_data,
    const TriangleMesh                            &dest_mesh,
    const std::vector<int>                        &dest_source_face,
    TriangleSelector                              &dest,
    const std::atomic<bool>                       *cancel)
{
    return reproject_channel_from_faces(source_mesh, src_data, dest_mesh, dest_source_face, dest, cancel);
}

bool reproject_painting_spatial(
    const TriangleMesh                            &source_mesh,
    const TriangleSelector::TriangleSplittingData &src_data,
    const TriangleMesh                            &dest_mesh,
    TriangleSelector                              &dest,
    float                                          max_distance,
    const std::atomic<bool>                       *cancel)
{
    dest.reset();
    dest.set_edge_limit(kPaintReprojectBoundaryMm);
    if (canceled(cancel))
        return false;
    if (source_mesh.empty() || dest_mesh.empty())
        return true;

    TriangleSelector src(source_mesh);
    load_source_selector(src, src_data);

    const auto tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(
        source_mesh.its.vertices, source_mesh.its.indices);
    const float max_d2 = max_distance * max_distance;

    for (int i = 0; i < int(dest_mesh.its.indices.size()); ++i) {
        if (canceled(cancel)) {
            dest.reset();
            return false;
        }
        const Vec3f c = face_centroid(dest_mesh.its, i);
        size_t      hit_idx = size_t(-1);
        Vec3f       hit_pt  = Vec3f::Zero();
        const float d2 = AABBTreeIndirect::squared_distance_to_indexed_triangle_set(
            source_mesh.its.vertices, source_mesh.its.indices, tree, c, hit_idx, hit_pt);
        EnforcerBlockerType state = EnforcerBlockerType::NONE;
        if (d2 >= 0.f && d2 <= max_d2 && hit_idx != size_t(-1))
            state = src.state_at_point(int(hit_idx), hit_pt);
        dest.set_facet(i, state);
    }
    return true;
}

namespace {

void apply_channel(ModelVolume &dest, FacetsAnnotation &ann,
                   const TriangleSelector::TriangleSplittingData &data,
                   const TriangleMesh &source_mesh,
                   const std::vector<int> *face_map,
                   float spatial_max,
                   const std::atomic<bool> *cancel)
{
    TriangleSelector sel(dest.mesh());
    bool ok = true;
    if (face_map != nullptr)
        ok = reproject_painting_from_faces(source_mesh, data, dest.mesh(), *face_map, sel, cancel);
    else
        ok = reproject_painting_spatial(source_mesh, data, dest.mesh(), sel, spatial_max, cancel);
    if (ok)
        ann.set(sel);
    else
        ann.reset();
}

} // namespace

bool reproject_volume_from_faces(
    ModelVolume            &dest,
    const SavedPainting    &src,
    const TriangleMesh     &source_mesh,
    const std::vector<int> &dest_source_face,
    const std::atomic<bool> *cancel)
{
    apply_channel(dest, dest.supported_facets, src.supported, source_mesh, &dest_source_face, 0.f, cancel);
    if (canceled(cancel))
        return false;
    apply_channel(dest, dest.seam_facets, src.seam, source_mesh, &dest_source_face, 0.f, cancel);
    if (canceled(cancel))
        return false;
    apply_channel(dest, dest.mmu_segmentation_facets, src.mmu, source_mesh, &dest_source_face, 0.f, cancel);
    if (canceled(cancel))
        return false;
    apply_channel(dest, dest.fuzzy_skin_facets, src.fuzzy, source_mesh, &dest_source_face, 0.f, cancel);
    return !canceled(cancel);
}

bool reproject_volume_spatial(
    ModelVolume             &dest,
    const SavedPainting     &src,
    const TriangleMesh      &source_mesh,
    float                    max_distance,
    const std::atomic<bool> *cancel)
{
    apply_channel(dest, dest.supported_facets, src.supported, source_mesh, nullptr, max_distance, cancel);
    if (canceled(cancel))
        return false;
    apply_channel(dest, dest.seam_facets, src.seam, source_mesh, nullptr, max_distance, cancel);
    if (canceled(cancel))
        return false;
    apply_channel(dest, dest.mmu_segmentation_facets, src.mmu, source_mesh, nullptr, max_distance, cancel);
    if (canceled(cancel))
        return false;
    apply_channel(dest, dest.fuzzy_skin_facets, src.fuzzy, source_mesh, nullptr, max_distance, cancel);
    return !canceled(cancel);
}

} // namespace Slic3r

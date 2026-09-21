#ifndef slic3r_PaintReproject_hpp_
#define slic3r_PaintReproject_hpp_

#include "TriangleSelector.hpp"
#include "TriangleMesh.hpp"

#include <atomic>
#include <vector>

namespace Slic3r {

class ModelVolume;
class FacetsAnnotation;

constexpr float  kPaintReprojectBoundaryMm = 0.2f;
constexpr int    kPaintReprojectMaxDepth   = 8;
constexpr size_t kPaintReprojectNodeBudget = 4096;

// Snapshot of all four ModelVolume FacetsAnnotation channels.
struct SavedPainting {
    TriangleSelector::TriangleSplittingData supported;
    TriangleSelector::TriangleSplittingData seam;
    TriangleSelector::TriangleSplittingData mmu;
    TriangleSelector::TriangleSplittingData fuzzy;

    bool empty() const {
        return supported.triangles_to_split.empty() && seam.triangles_to_split.empty()
            && mmu.triangles_to_split.empty() && fuzzy.triangles_to_split.empty();
    }
};

SavedPainting snapshot_volume_painting(const ModelVolume &volume);

// dest_source_face[i] = source orig facet, or -1 for caps / new faces.
bool reproject_painting_from_faces(
    const TriangleMesh                              &source_mesh,
    const TriangleSelector::TriangleSplittingData   &src_data,
    const TriangleMesh                              &dest_mesh,
    const std::vector<int>                          &dest_source_face,
    TriangleSelector                                &dest,
    const std::atomic<bool>                         *cancel = nullptr);

// Nearest-source-face reprojection for retessellated dest (Repair).
bool reproject_painting_spatial(
    const TriangleMesh                              &source_mesh,
    const TriangleSelector::TriangleSplittingData   &src_data,
    const TriangleMesh                              &dest_mesh,
    TriangleSelector                                &dest,
    float                                            max_distance,
    const std::atomic<bool>                         *cancel = nullptr);

bool reproject_volume_from_faces(
    ModelVolume             &dest,
    const SavedPainting     &src,
    const TriangleMesh      &source_mesh,
    const std::vector<int>  &dest_source_face,
    const std::atomic<bool> *cancel = nullptr);

bool reproject_volume_spatial(
    ModelVolume             &dest,
    const SavedPainting     &src,
    const TriangleMesh      &source_mesh,
    float                    max_distance,
    const std::atomic<bool> *cancel = nullptr);

} // namespace Slic3r

#endif

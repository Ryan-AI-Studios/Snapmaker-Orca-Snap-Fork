#ifndef slic3r_PicPrint_hpp_
#define slic3r_PicPrint_hpp_

#include "BoundingBox.hpp"
#include "ColorSpace.hpp"
#include "libslic3r.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Slic3r {

class ModelVolume;

constexpr int    kPicPrintLongEdge            = 256;
constexpr size_t kPicPrintDefaultMaxClusters  = 16;
constexpr size_t kPicPrintHardMaxClusters     = 32;

struct PicPrintPlan
{
    bool                     valid = false;
    std::string              error;
    int                      width  = 0;
    int                      height = 0;
    size_t                   cluster_count      = 0;
    size_t                   collapsed_clusters = 0;
    std::vector<std::string> cluster_hex;   // #RRGGBB, size cluster_count
    std::vector<unsigned>    pixel_cluster; // 0-based, size width*height
    std::vector<unsigned>    dest_id;       // 1-based filament ids, size width*height
};

size_t picprint_cluster_room(size_t num_physical, size_t existing_mix_count);

PicPrintPlan plan_picprint(
    const std::uint8_t *rgb,
    int                 w,
    int                 h,
    size_t              num_physical,
    size_t              existing_mix_count = 0,
    size_t              max_clusters       = kPicPrintDefaultMaxClusters);

bool picprint_world_to_uv(const Vec3d &world, const BoundingBoxf3 &xy_bbox, double &u, double &v);

unsigned picprint_sample_cluster(const PicPrintPlan &plan, double u, double v);
unsigned picprint_sample_dest(const PicPrintPlan &plan, double u, double v);

bool picprint_is_front_face(const Vec3d &local_normal, const Transform3d &world, double eps = 1e-4);

void picprint_set_dest_ids(PicPrintPlan &plan, const std::vector<unsigned> &cluster_to_dest);

bool picprint_apply_to_volume(
    ModelVolume           &vol,
    const Transform3d     &world,
    const BoundingBoxf3   &xy_bbox,
    const PicPrintPlan    &plan,
    bool                   front_faces_only = true,
    size_t                *skipped_out      = nullptr);

} // namespace Slic3r

#endif

#include "PicPrint.hpp"

#include "ColorSpace.hpp"
#include "Geometry.hpp"
#include "Model.hpp"
#include "TriangleMesh.hpp"
#include "TriangleSelector.hpp"

#include <Eigen/Geometry>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace Slic3r {

namespace {

int clamp_int(int v, int lo, int hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

int picprint_pixel_x(double u, int w)
{
    if (w <= 1)
        return 0;
    u = std::clamp(u, 0.0, 1.0);
    return clamp_int(int(u * double(w - 1)), 0, w - 1);
}

int picprint_pixel_y(double v, int h)
{
    if (h <= 1)
        return 0;
    v = std::clamp(v, 0.0, 1.0);
    return clamp_int(int((1.0 - v) * double(h - 1)), 0, h - 1);
}

std::string hex_rgb(unsigned r, unsigned g, unsigned b)
{
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
    return buf;
}

void downsample_rgb(const std::uint8_t *rgb, int w, int h, std::vector<std::uint8_t> &out, int &nw, int &nh)
{
    const int long_edge = std::max(w, h);
    nw                  = w;
    nh                  = h;
    if (long_edge > kPicPrintLongEdge) {
        nw = std::max(1, (w * kPicPrintLongEdge) / long_edge);
        nh = std::max(1, (h * kPicPrintLongEdge) / long_edge);
    }
    out.resize(size_t(nw) * size_t(nh) * 3);
    for (int y = 0; y < nh; ++y) {
        const int sy = (y * h) / nh;
        for (int x = 0; x < nw; ++x) {
            const int    sx = (x * w) / nw;
            const size_t si = (size_t(sy) * size_t(w) + size_t(sx)) * 3;
            const size_t di = (size_t(y) * size_t(nw) + size_t(x)) * 3;
            out[di]         = rgb[si];
            out[di + 1]     = rgb[si + 1];
            out[di + 2]     = rgb[si + 2];
        }
    }
}

struct RgbPixel
{
    std::uint8_t r     = 0;
    std::uint8_t g     = 0;
    std::uint8_t b     = 0;
    int          index = 0;
};

int channel_of(const RgbPixel &p, int ch)
{
    if (ch == 0)
        return p.r;
    if (ch == 1)
        return p.g;
    return p.b;
}

struct ColorBox
{
    size_t begin   = 0;
    size_t end     = 0;
    int    minc[3] = {0, 0, 0};
    int    maxc[3] = {0, 0, 0};
};

void refresh_box(ColorBox &box, const std::vector<RgbPixel> &pixels)
{
    box.minc[0] = box.minc[1] = box.minc[2] = 255;
    box.maxc[0] = box.maxc[1] = box.maxc[2] = 0;
    for (size_t i = box.begin; i < box.end; ++i) {
        const RgbPixel &p = pixels[i];
        box.minc[0]       = std::min(box.minc[0], int(p.r));
        box.maxc[0]       = std::max(box.maxc[0], int(p.r));
        box.minc[1]       = std::min(box.minc[1], int(p.g));
        box.maxc[1]       = std::max(box.maxc[1], int(p.g));
        box.minc[2]       = std::min(box.minc[2], int(p.b));
        box.maxc[2]       = std::max(box.maxc[2], int(p.b));
    }
}

int box_longest_channel(const ColorBox &box)
{
    int best_ch = 0;
    int best_r  = box.maxc[0] - box.minc[0];
    for (int ch = 1; ch < 3; ++ch) {
        const int r = box.maxc[ch] - box.minc[ch];
        if (r > best_r) {
            best_r  = r;
            best_ch = ch;
        }
    }
    return best_ch;
}

int box_range(const ColorBox &box)
{
    int best = 0;
    for (int ch = 0; ch < 3; ++ch)
        best = std::max(best, box.maxc[ch] - box.minc[ch]);
    return best;
}

struct RgbCentroid
{
    unsigned r = 0;
    unsigned g = 0;
    unsigned b = 0;
};

void median_cut(const std::vector<std::uint8_t> &rgb,
                int                              w,
                int                              h,
                size_t                           max_clusters,
                std::vector<int>                &cluster_of,
                std::vector<RgbCentroid>        &centroids)
{
    const size_t n = size_t(w) * size_t(h);
    cluster_of.assign(n, 0);
    centroids.clear();
    if (n == 0 || max_clusters == 0)
        return;

    std::vector<RgbPixel> pixels(n);
    for (size_t i = 0; i < n; ++i) {
        pixels[i].r     = rgb[i * 3];
        pixels[i].g     = rgb[i * 3 + 1];
        pixels[i].b     = rgb[i * 3 + 2];
        pixels[i].index = int(i);
    }

    std::vector<ColorBox> boxes;
    boxes.reserve(max_clusters);
    ColorBox root;
    root.begin = 0;
    root.end   = n;
    refresh_box(root, pixels);
    boxes.push_back(root);

    while (boxes.size() < max_clusters) {
        size_t best_i = 0;
        int    best_r = -1;
        for (size_t i = 0; i < boxes.size(); ++i) {
            if (boxes[i].end - boxes[i].begin < 2)
                continue;
            const int r = box_range(boxes[i]);
            if (r > best_r) {
                best_r = r;
                best_i = i;
            }
        }
        if (best_r <= 0)
            break;

        ColorBox &src = boxes[best_i];
        const int ch  = box_longest_channel(src);
        std::sort(pixels.begin() + std::ptrdiff_t(src.begin), pixels.begin() + std::ptrdiff_t(src.end),
                  [ch](const RgbPixel &a, const RgbPixel &b) {
                      const int ca = channel_of(a, ch);
                      const int cb = channel_of(b, ch);
                      if (ca != cb)
                          return ca < cb;
                      return a.index < b.index;
                  });

        size_t mid = src.begin + (src.end - src.begin) / 2;
        if (mid == src.begin)
            mid = src.begin + 1;
        if (mid >= src.end)
            break;

        ColorBox left  = src;
        ColorBox right = src;
        left.end       = mid;
        right.begin    = mid;
        refresh_box(left, pixels);
        refresh_box(right, pixels);
        src = left;
        boxes.push_back(right);
    }

    centroids.resize(boxes.size());
    for (size_t b = 0; b < boxes.size(); ++b) {
        std::uint64_t sr  = 0;
        std::uint64_t sg  = 0;
        std::uint64_t sb  = 0;
        const size_t  cnt = boxes[b].end - boxes[b].begin;
        for (size_t i = boxes[b].begin; i < boxes[b].end; ++i) {
            sr += pixels[i].r;
            sg += pixels[i].g;
            sb += pixels[i].b;
            cluster_of[size_t(pixels[i].index)] = int(b);
        }
        if (cnt == 0)
            continue;
        const auto avg = [cnt](std::uint64_t s) -> unsigned {
            return static_cast<unsigned>((s + cnt / 2) / cnt);
        };
        centroids[b] = RgbCentroid{avg(sr), avg(sg), avg(sb)};
    }
}

size_t nearest_kept(size_t drop, const std::vector<RgbCentroid> &centroids, const std::vector<char> &keep)
{
    const CIELab lab = rgb_u8_to_lab(centroids[drop].r, centroids[drop].g, centroids[drop].b);
    size_t       best = 0;
    double       best_d = 1e300;
    bool         any    = false;
    for (size_t i = 0; i < centroids.size(); ++i) {
        if (!keep[i])
            continue;
        const CIELab other = rgb_u8_to_lab(centroids[i].r, centroids[i].g, centroids[i].b);
        const double d     = delta_e00(lab, other);
        if (!any || d < best_d) {
            best_d = d;
            best   = i;
            any    = true;
        }
    }
    return best;
}

} // namespace

size_t picprint_cluster_room(size_t num_physical, size_t existing_mix_count)
{
    const size_t used = num_physical + existing_mix_count;
    if (used >= MAXIMUM_FILAMENT_NUMBER)
        return 0;
    return MAXIMUM_FILAMENT_NUMBER - used;
}

PicPrintPlan plan_picprint(
    const std::uint8_t *rgb,
    int                 w,
    int                 h,
    size_t              num_physical,
    size_t              existing_mix_count,
    size_t              max_clusters)
{
    PicPrintPlan plan;
    if (rgb == nullptr || w <= 0 || h <= 0) {
        plan.error = "Invalid image buffer";
        return plan;
    }
    if (num_physical < 2) {
        plan.error = "Need at least two physical filament colours (prefer distinct colours on slots 1-4).";
        return plan;
    }

    const size_t room = picprint_cluster_room(num_physical, existing_mix_count);
    if (room == 0) {
        plan.error = "No mix slots remaining under the filament cap.";
        return plan;
    }

    size_t want = max_clusters;
    if (want == 0)
        want = kPicPrintDefaultMaxClusters;
    want = std::min(want, kPicPrintHardMaxClusters);

    std::vector<std::uint8_t> down;
    int                       nw = 0;
    int                       nh = 0;
    downsample_rgb(rgb, w, h, down, nw, nh);

    std::vector<int>         cluster_of;
    std::vector<RgbCentroid> centroids;
    median_cut(down, nw, nh, want, cluster_of, centroids);
    if (centroids.empty()) {
        plan.error = "PicPrint produced no clusters.";
        return plan;
    }

    std::vector<int> remap(centroids.size(), -1);
    size_t           collapsed = 0;
    if (centroids.size() > room) {
        std::vector<size_t> counts(centroids.size(), 0);
        for (int c : cluster_of) {
            if (c >= 0 && size_t(c) < counts.size())
                ++counts[size_t(c)];
        }
        std::vector<size_t> order(centroids.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            if (counts[a] != counts[b])
                return counts[a] > counts[b];
            return a < b;
        });

        std::vector<char> keep(centroids.size(), 0);
        for (size_t i = 0; i < room; ++i)
            keep[order[i]] = 1;

        for (size_t i = 0; i < centroids.size(); ++i) {
            if (keep[i])
                continue;
            const size_t dst = nearest_kept(i, centroids, keep);
            remap[i]         = int(dst);
            ++collapsed;
        }
        for (int &c : cluster_of) {
            if (c >= 0 && size_t(c) < remap.size() && remap[size_t(c)] >= 0)
                c = remap[size_t(c)];
        }
    }

    std::vector<int> compact(centroids.size(), -1);
    size_t           next = 0;
    for (size_t i = 0; i < centroids.size(); ++i) {
        if (remap[i] >= 0)
            continue;
        compact[i] = int(next++);
    }
    for (int &c : cluster_of) {
        if (c >= 0 && size_t(c) < compact.size() && compact[size_t(c)] >= 0)
            c = compact[size_t(c)];
        else
            c = 0;
    }

    plan.width              = nw;
    plan.height             = nh;
    plan.cluster_count      = next;
    plan.collapsed_clusters = collapsed;
    plan.cluster_hex.resize(next);
    for (size_t i = 0; i < centroids.size(); ++i) {
        if (compact[i] < 0)
            continue;
        const RgbCentroid &cc                   = centroids[i];
        plan.cluster_hex[size_t(compact[i])] = hex_rgb(cc.r, cc.g, cc.b);
    }

    const size_t npx = size_t(nw) * size_t(nh);
    plan.pixel_cluster.resize(npx);
    plan.dest_id.resize(npx);
    const unsigned dest_base = static_cast<unsigned>(num_physical + existing_mix_count);
    for (size_t i = 0; i < npx; ++i) {
        unsigned c               = cluster_of[i] < 0 ? 0 : unsigned(cluster_of[i]);
        if (c >= plan.cluster_count)
            c = 0;
        plan.pixel_cluster[i] = c;
        plan.dest_id[i]       = dest_base + c + 1;
    }
    plan.valid = plan.cluster_count > 0 && !plan.dest_id.empty();
    if (!plan.valid)
        plan.error = "PicPrint produced no clusters.";
    return plan;
}

bool picprint_world_to_uv(const Vec3d &world, const BoundingBoxf3 &xy_bbox, double &u, double &v)
{
    const double dx = xy_bbox.max.x() - xy_bbox.min.x();
    const double dy = xy_bbox.max.y() - xy_bbox.min.y();
    if (dx < 1e-6 || dy < 1e-6)
        return false;
    u = (world.x() - xy_bbox.min.x()) / dx;
    v = (world.y() - xy_bbox.min.y()) / dy;
    return true;
}

unsigned picprint_sample_cluster(const PicPrintPlan &plan, double u, double v)
{
    if (!plan.valid || plan.width <= 0 || plan.height <= 0)
        return 0;
    const size_t expect = size_t(plan.width) * size_t(plan.height);
    if (plan.pixel_cluster.size() != expect)
        return 0;
    const int px = picprint_pixel_x(u, plan.width);
    const int py = picprint_pixel_y(v, plan.height);
    return plan.pixel_cluster[size_t(py) * size_t(plan.width) + size_t(px)];
}

unsigned picprint_sample_dest(const PicPrintPlan &plan, double u, double v)
{
    if (!plan.valid || plan.width <= 0 || plan.height <= 0)
        return 0;
    const size_t expect = size_t(plan.width) * size_t(plan.height);
    if (plan.dest_id.size() != expect)
        return 0;
    const int px = picprint_pixel_x(u, plan.width);
    const int py = picprint_pixel_y(v, plan.height);
    return plan.dest_id[size_t(py) * size_t(plan.width) + size_t(px)];
}

bool picprint_is_front_face(const Vec3d &local_normal, const Transform3d &world, double eps)
{
    if (!local_normal.allFinite() || local_normal.norm() < 1e-12)
        return false;
    const Eigen::Matrix3d linear = world.linear();
    Eigen::Matrix3d       inv;
    bool                  invertible = false;
    linear.computeInverseWithCheck(inv, invertible, 1e-12);
    if (!invertible)
        return false;
    Vec3d n_world = (inv.transpose() * local_normal);
    if (!n_world.allFinite() || n_world.norm() < 1e-12)
        return false;
    n_world.normalize();
    if (linear.determinant() < 0.0)
        n_world = -n_world;
    return n_world.z() > eps;
}

void picprint_set_dest_ids(PicPrintPlan &plan, const std::vector<unsigned> &cluster_to_dest)
{
    if (plan.pixel_cluster.size() != plan.dest_id.size())
        return;
    for (size_t i = 0; i < plan.pixel_cluster.size(); ++i) {
        const unsigned c = plan.pixel_cluster[i];
        unsigned       d = 0;
        if (c < cluster_to_dest.size())
            d = cluster_to_dest[c];
        plan.dest_id[i] = d;
    }
}

bool picprint_apply_to_volume(
    ModelVolume         &vol,
    const Transform3d   &world,
    const BoundingBoxf3 &xy_bbox,
    const PicPrintPlan  &plan,
    bool                 front_faces_only,
    size_t              *skipped_out)
{
    if (skipped_out != nullptr)
        *skipped_out = 0;
    if (!plan.valid)
        return false;

    double u0 = 0;
    double v0 = 0;
    if (!picprint_world_to_uv(xy_bbox.min, xy_bbox, u0, v0))
        return false;

    const indexed_triangle_set &its = vol.mesh().its;
    TriangleSelector            selector(vol.mesh());
    if (!vol.mmu_segmentation_facets.empty())
        selector.deserialize(vol.mmu_segmentation_facets.get_data(), true, EnforcerBlockerType::ExtruderMax);

    const std::vector<Vec3f> face_normals = front_faces_only ? its_face_normals(its) : std::vector<Vec3f>{};
    size_t                   skipped      = 0;
    const int                n            = int(its.indices.size());
    for (int i = 0; i < n; ++i) {
        if (front_faces_only) {
            Vec3d local_n = Vec3d::Zero();
            if (size_t(i) < face_normals.size())
                local_n = face_normals[size_t(i)].cast<double>();
            if (!picprint_is_front_face(local_n, world)) {
                ++skipped;
                continue;
            }
        }
        const stl_triangle_vertex_indices &tri = its.indices[size_t(i)];
        Vec3d                              centroid = Vec3d::Zero();
        for (int k = 0; k < 3; ++k)
            centroid += its.vertices[size_t(tri(k))].cast<double>();
        centroid /= 3.0;
        const Vec3d world_pt = world * centroid;
        double      u        = 0;
        double      v        = 0;
        if (!picprint_world_to_uv(world_pt, xy_bbox, u, v))
            return false;
        const unsigned dest = picprint_sample_dest(plan, u, v);
        if (dest < 1)
            continue;
        selector.set_facet(i, EnforcerBlockerType(dest));
    }
    if (skipped_out != nullptr)
        *skipped_out = skipped;
    vol.mmu_segmentation_facets.set(selector);
    return true;
}

} // namespace Slic3r

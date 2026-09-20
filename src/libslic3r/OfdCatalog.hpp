#ifndef slic3r_OfdCatalog_hpp_
#define slic3r_OfdCatalog_hpp_

#include <cstddef>
#include <string>
#include <vector>

namespace Slic3r {

constexpr size_t kOfdRecentCap = 10;

struct OfdVariant
{
    std::string              brand;
    std::string              filament;
    std::string              variant;
    std::string              material;
    std::vector<std::string> color_hexes; // 1+; first is slot default
    bool                     translucent = false;
    bool                     transparent = false;
};

struct OfdComposedList
{
    std::vector<OfdVariant> matches;
    size_t                  recent_prefix{0};
};

std::string ofd_variant_key(const OfdVariant &v);

// Seed JSON `{ "variants": [ ... ] }`, a JSON array of variants, or OFD NDJSON
// (`_type` brand/filament/variant join). Never throws.
std::vector<OfdVariant> ofd_parse(const std::string &text);

// Load seed then overlay. Dedup by ofd_variant_key; seed wins. Missing files → empty extra.
std::vector<OfdVariant> ofd_load_catalog(
    const std::string &seed_json_path,
    const std::string &user_ndjson_path = {});

void ofd_recents_push(
    std::vector<OfdVariant> &recents,
    const OfdVariant        &applied,
    size_t                   cap = kOfdRecentCap);

std::vector<OfdVariant> ofd_recents_parse(const std::string &text);
std::string             ofd_recents_serialize(const std::vector<OfdVariant> &recents);

OfdComposedList ofd_compose_list(
    const std::vector<OfdVariant> &recents,
    const std::vector<OfdVariant> &catalog,
    const std::string             &brand_filter,
    const std::string             &name_substring);

std::vector<OfdVariant> ofd_lookup(
    const std::vector<OfdVariant> &catalog,
    const std::string             &brand_filter,
    const std::string             &name_substring);

std::string ofd_slot_hex(const OfdVariant &v);

// Stamp existing slot i (i < filament_colour.size()). Dual-color → '|' join and mode 0.
// Does not grow filament_colour. Empty / OOB → false, no writes.
bool ofd_stamp_slot(
    std::vector<std::string>       &filament_colour,
    std::vector<std::string>       &filament_multi_colors,
    std::vector<int>               &filament_colour_mode,
    size_t                          slot,
    const std::vector<std::string> &hexes);

std::string ofd_default_seed_path();
std::string ofd_default_overlay_path();
std::string ofd_default_recents_path();

bool                ofd_save_recents_file(const std::string &path, const std::vector<OfdVariant> &recents);
std::vector<OfdVariant> ofd_load_recents_file(const std::string &path);

} // namespace Slic3r

#endif

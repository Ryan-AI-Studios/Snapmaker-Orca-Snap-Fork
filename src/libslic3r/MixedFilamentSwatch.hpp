#ifndef slic3r_MixedFilamentSwatch_hpp_
#define slic3r_MixedFilamentSwatch_hpp_

#include "ColorSpace.hpp"
#include "MixedFilamentConvert.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Slic3r {

struct SwatchLutEntry {
    std::string recipe_key;
    CIELab      lab;
};

struct SwatchLut {
    int         version = 1;
    std::string batch_key;
    std::string lighting;
    std::string illuminant;
    std::string source_path;
    std::vector<SwatchLutEntry> entries;
    std::unordered_map<std::string, size_t> index; // recipe_key -> entries offset
};

struct SwatchParseReport {
    bool        ok                   = false;
    std::string error;
    size_t      accepted             = 0;
    size_t      skipped_nonfinite    = 0;
    size_t      skipped_duplicate    = 0;
    size_t      skipped_unknown_key  = 0;
};

std::string snap_recipe_key_pair(unsigned component_a, unsigned component_b, int mix_b_percent);
std::string snap_recipe_key_grad(const std::string &encoded_ids, const std::string &encoded_weights);
std::string snap_recipe_key_phys(unsigned physical_id);
bool        is_valid_swatch_recipe_key(const std::string &key);

std::string compute_swatch_batch_key(const std::vector<std::string> &hexes,
                                     const std::vector<std::string> &type_names = {});

std::string default_swatch_lut_path();

bool parse_swatch_lut_json(const std::string &text, SwatchLut &out, SwatchParseReport &report);
bool parse_swatch_lut_csv(const std::string &text, SwatchLut &out, SwatchParseReport &report);
std::string serialize_swatch_lut_json(const SwatchLut &lut);

bool load_swatch_lut_file(const std::string &path, SwatchLut &out, SwatchParseReport &report);
bool save_swatch_lut(const std::string &path, const SwatchLut &lut, std::string *error = nullptr);

bool                  lut_is_stale(const SwatchLut &lut, const std::string &live_batch_key);
std::optional<CIELab> measured_lab_for(const SwatchLut &lut, const std::string &recipe_key);

// Predicted ΔE unless lut is non-null, fresh for live_batch_key, and recipe_key hits.
double candidate_distance(const CIELab     &target,
                          const CIELab     &predicted,
                          const SwatchLut  *lut,
                          const std::string &recipe_key,
                          const std::string &live_batch_key,
                          bool             *used_measured = nullptr);

} // namespace Slic3r

#endif

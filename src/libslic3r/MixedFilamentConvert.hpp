#ifndef slic3r_MixedFilamentConvert_hpp_
#define slic3r_MixedFilamentConvert_hpp_

#include "libslic3r.h"
#include "MixedFilament.hpp"

#include <string>
#include <vector>

namespace Slic3r {

class Model;

// One unique painted source colour captured before any 4-tool truncation.
struct PaintedSourceColor {
    std::string               hex;          // normalized "#RRGGBB", empty if unparseable
    std::vector<unsigned int> extruder_ids; // 1-based paint/extruder IDs that share this hex
};

// In-memory source palette for on-open / pre-switch convert (no 3MF key).
struct PaintedSourcePalette {
    std::vector<PaintedSourceColor> colors;
    size_t filament_n          = 0;
    size_t enabled_mix_count   = 0;
    bool   paint_nonempty      = false;

    size_t unique_color_count() const { return colors.size(); }
};

// Normalize a filament hex to "#RRGGBB" (upper case). Empty on failure.
std::string normalize_painted_colour_hex(const std::string &raw);

// Collect unique painted colours from MMU facet states + filament_colour
// (and mix display_color when mixed != nullptr). Does not require Print or wx.
PaintedSourcePalette capture_painted_source_palette(
    const Model                       &model,
    const std::vector<std::string>    &filament_colours,
    const MixedFilamentManager        *mixed = nullptr);

// Eligibility for the Convert/Keep prompt. False when restore/silence, mixes
// already exist, paint is empty, or both filament_n and unique colours are <= 4.
bool should_prompt_convert_painted_colours(
    const PaintedSourcePalette &palette,
    bool                        restore_or_silence);

// How many new mix rows fit under MAXIMUM_FILAMENT_NUMBER given current physicals.
struct ConvertCapPlan {
    size_t       accepted_mixes       = 0;
    size_t       dropped_mixes        = 0;
    unsigned int fallback_filament_id = 1; // nearest kept mix or physical 1
};

ConvertCapPlan plan_convert_slot_cap(size_t num_physical, size_t new_mix_count);

// Rewrite zero assigned virtual-ids (cap drops) onto fallback_filament_id so
// cleanup cannot delete those source regions. Returns how many were rewritten.
size_t apply_convert_cap_fallbacks(
    std::vector<unsigned int>       &assigned_ids,
    const ConvertCapPlan            &plan);

} // namespace Slic3r

#endif

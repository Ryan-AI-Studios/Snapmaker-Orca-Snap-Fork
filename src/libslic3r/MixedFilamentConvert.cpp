#include "MixedFilamentConvert.hpp"
#include "Model.hpp"
#include "TriangleSelector.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace Slic3r {

std::string normalize_painted_colour_hex(const std::string &raw)
{
    std::string s;
    s.reserve(raw.size());
    for (unsigned char c : raw) {
        if (!std::isspace(c))
            s.push_back(static_cast<char>(c));
    }
    if (!s.empty() && s.front() == '#')
        s.erase(s.begin());
    if (s.size() >= 8)
        s.resize(6);
    else if (s.size() == 3) {
        s = {s[0], s[0], s[1], s[1], s[2], s[2]};
    }
    if (s.size() != 6)
        return {};
    for (char &c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (char c : s) {
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return {};
    }
    return std::string("#") + s;
}

static std::string colour_for_extruder(
    unsigned int                   eid,
    const std::vector<std::string> &filament_colours,
    const MixedFilamentManager     *mixed)
{
    if (eid < 1)
        return {};
    const size_t idx = static_cast<size_t>(eid - 1);
    if (idx < filament_colours.size())
        return filament_colours[idx];
    if (mixed == nullptr)
        return {};
    const MixedFilament *mf = mixed->mixed_filament_from_id(eid, filament_colours.size());
    if (mf != nullptr && !mf->display_color.empty())
        return mf->display_color;
    return {};
}

PaintedSourcePalette capture_painted_source_palette(
    const Model                    &model,
    const std::vector<std::string> &filament_colours,
    const MixedFilamentManager     *mixed)
{
    PaintedSourcePalette out;
    out.filament_n        = filament_colours.size();
    out.enabled_mix_count = mixed != nullptr ? mixed->enabled_count() : 0;

    std::unordered_map<std::string, size_t> hex_index;
    hex_index.reserve(16);

    for (const ModelObject *mo : model.objects) {
        if (mo == nullptr)
            continue;
        for (const ModelVolume *vol : mo->volumes) {
            if (vol == nullptr || vol->type() != ModelVolumeType::MODEL_PART)
                continue;
            if (vol->mmu_segmentation_facets.empty())
                continue;
            out.paint_nonempty = true;
            const auto &used = vol->mmu_segmentation_facets.get_data().used_states;
            for (size_t i = 1; i < used.size(); ++i) {
                if (!used[i])
                    continue;
                const unsigned int eid = static_cast<unsigned int>(i);
                const std::string  hex = normalize_painted_colour_hex(
                    colour_for_extruder(eid, filament_colours, mixed));
                if (hex.empty())
                    continue;
                auto it = hex_index.find(hex);
                if (it == hex_index.end()) {
                    hex_index.emplace(hex, out.colors.size());
                    PaintedSourceColor entry;
                    entry.hex = hex;
                    entry.extruder_ids.push_back(eid);
                    out.colors.push_back(std::move(entry));
                } else {
                    auto &ids = out.colors[it->second].extruder_ids;
                    if (std::find(ids.begin(), ids.end(), eid) == ids.end())
                        ids.push_back(eid);
                }
            }
        }
    }
    return out;
}

bool should_prompt_convert_painted_colours(
    const PaintedSourcePalette &palette,
    bool                        restore_or_silence)
{
    if (restore_or_silence)
        return false;
    if (palette.enabled_mix_count > 0)
        return false;
    if (!palette.paint_nonempty)
        return false;
    if (palette.filament_n <= 4 && palette.unique_color_count() <= 4)
        return false;
    return true;
}

ConvertCapPlan plan_convert_slot_cap(size_t num_physical, size_t new_mix_count)
{
    ConvertCapPlan plan;
    const size_t physical = std::max<size_t>(num_physical, 1);
    const size_t room     = (physical >= MAXIMUM_FILAMENT_NUMBER)
                              ? 0
                              : (MAXIMUM_FILAMENT_NUMBER - physical);
    plan.accepted_mixes = std::min(new_mix_count, room);
    plan.dropped_mixes  = new_mix_count > plan.accepted_mixes
                              ? new_mix_count - plan.accepted_mixes
                              : 0;
    if (plan.accepted_mixes > 0)
        plan.fallback_filament_id = static_cast<unsigned int>(physical + plan.accepted_mixes);
    else
        plan.fallback_filament_id = 1;
    return plan;
}

size_t apply_convert_cap_fallbacks(
    std::vector<unsigned int> &assigned_ids,
    const ConvertCapPlan      &plan)
{
    size_t rewritten = 0;
    for (unsigned int &id : assigned_ids) {
        if (id == 0) {
            id = plan.fallback_filament_id;
            ++rewritten;
        }
    }
    return rewritten;
}

} // namespace Slic3r

#include "SlotRemap.hpp"

#include "ColorSpace.hpp"
#include "CustomGCode.hpp"
#include "Model.hpp"
#include "PrintConfig.hpp"
#include "TriangleSelector.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <set>
#include <unordered_set>

namespace Slic3r {

namespace {

const char *kFeatureKeys[] = {
    "wall_filament",
    "sparse_infill_filament",
    "solid_infill_filament",
    "support_filament",
    "support_interface_filament"};

std::string normalize_hex(const std::string &raw)
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
    else if (s.size() == 3)
        s = {s[0], s[0], s[1], s[1], s[2], s[2]};
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

bool hex_to_lab(const std::string &hex, CIELab &out)
{
    const std::string n = normalize_hex(hex);
    if (n.size() != 7)
        return false;
    auto nybble = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return unsigned(c - '0');
        return 10u + unsigned(c - 'A');
    };
    const unsigned r = (nybble(n[1]) << 4) | nybble(n[2]);
    const unsigned g = (nybble(n[3]) << 4) | nybble(n[4]);
    const unsigned b = (nybble(n[5]) << 4) | nybble(n[6]);
    out = rgb_u8_to_lab(r, g, b);
    return true;
}

void collect_id(std::set<unsigned int> &out, int id)
{
    if (id > 0)
        out.insert(static_cast<unsigned int>(id));
}

void collect_config_extruder(std::set<unsigned int> &out, const ModelConfig &cfg)
{
    if (!cfg.has("extruder"))
        return;
    collect_id(out, cfg.extruder());
}

void remap_config_extruder(ModelConfig &cfg, const SlotRemapMap &map)
{
    if (!cfg.has("extruder"))
        return;
    const int old = cfg.extruder();
    if (old <= 0)
        return;
    const unsigned int dest = slot_remap_lookup(map, static_cast<unsigned int>(old));
    if (dest == 0 || dest == static_cast<unsigned int>(old))
        return;
    cfg.set_key_value("extruder", new ConfigOptionInt(static_cast<int>(dest)));
}

void remap_int_option(DynamicPrintConfig &cfg, const char *key, const SlotRemapMap &map)
{
    auto *opt = cfg.option<ConfigOptionInt>(key);
    if (opt == nullptr)
        return;
    const int old = opt->value;
    if (old <= 0)
        return;
    const unsigned int dest = slot_remap_lookup(map, static_cast<unsigned int>(old));
    if (dest == 0 || dest == static_cast<unsigned int>(old))
        return;
    opt->value = static_cast<int>(dest);
}

} // namespace

unsigned int slot_remap_lookup(const SlotRemapMap &map, unsigned int src_1based)
{
    if (src_1based == 0)
        return 0;
    auto it = map.find(src_1based);
    if (it == map.end())
        return src_1based;
    return it->second;
}

bool slot_remap_is_identity(const SlotRemapMap &map)
{
    for (const auto &kv : map) {
        if (kv.first != 0 && kv.second != kv.first)
            return false;
    }
    return true;
}

bool slot_remap_is_identity_for_used(
    const SlotRemapMap              &map,
    const std::vector<unsigned int> &used_1based)
{
    for (unsigned int src : used_1based) {
        if (src == 0)
            continue;
        if (slot_remap_lookup(map, src) != src)
            return false;
    }
    return true;
}

std::vector<unsigned int> slot_remap_collect_used_ids(
    const Model              &model,
    const DynamicPrintConfig *project_config)
{
    std::set<unsigned int> used;

    for (const ModelObject *mo : model.objects) {
        if (mo == nullptr)
            continue;
        collect_config_extruder(used, mo->config);
        for (const ModelVolume *mv : mo->volumes) {
            if (mv == nullptr)
                continue;
            collect_config_extruder(used, mv->config);
            if (mv->type() == ModelVolumeType::MODEL_PART &&
                !mv->mmu_segmentation_facets.empty()) {
                const auto &states = mv->mmu_segmentation_facets.get_data().used_states;
                for (size_t i = 1; i < states.size(); ++i) {
                    if (states[i])
                        used.insert(static_cast<unsigned int>(i));
                }
            }
        }
        for (const auto &lr : mo->layer_config_ranges)
            collect_config_extruder(used, lr.second);
    }

    for (const auto &plate : model.plates_custom_gcodes) {
        for (const CustomGCode::Item &item : plate.second.gcodes) {
            if (item.type == CustomGCode::Type::ToolChange)
                collect_id(used, item.extruder);
        }
    }

    if (project_config != nullptr) {
        for (const char *key : kFeatureKeys) {
            if (const auto *opt = project_config->option<ConfigOptionInt>(key))
                collect_id(used, opt->value);
        }
        if (const auto *seq = project_config->option<ConfigOptionInts>("first_layer_print_sequence")) {
            for (int v : seq->values)
                collect_id(used, v);
        }
    }

    return std::vector<unsigned int>(used.begin(), used.end());
}

void slot_remap_int_sequence(std::vector<int> &seq, const SlotRemapMap &map)
{
    for (int &v : seq) {
        if (v <= 0)
            continue;
        const unsigned int dest = slot_remap_lookup(map, static_cast<unsigned int>(v));
        if (dest != 0)
            v = static_cast<int>(dest);
    }
}

bool slot_remap_apply(
    Model              &model,
    DynamicPrintConfig *project_config,
    const SlotRemapMap &map,
    size_t              dest_count)
{
    if (dest_count == 0)
        return false;

    for (const auto &kv : map) {
        if (kv.first == 0)
            return false;
        if (kv.second == 0)
            continue;
        if (kv.second > dest_count)
            return false;
    }

    const std::vector<unsigned int> used = slot_remap_collect_used_ids(model, project_config);
    for (unsigned int src : used) {
        const unsigned int dest = slot_remap_lookup(map, src);
        if (dest == 0 || dest > dest_count)
            return false;
    }

    if (slot_remap_is_identity_for_used(map, used))
        return true;

    constexpr size_t MAX_EBT = static_cast<size_t>(EnforcerBlockerType::ExtruderMax);
    EnforcerBlockerStateMap state_map;
    for (size_t i = 0; i <= MAX_EBT; ++i)
        state_map[i] = static_cast<EnforcerBlockerType>(i);
    auto apply_pair = [&](unsigned int src, unsigned int dest) {
        if (src == 0 || src > MAX_EBT || dest == 0 || dest > MAX_EBT)
            return;
        state_map[src] = static_cast<EnforcerBlockerType>(dest);
    };
    for (const auto &kv : map)
        apply_pair(kv.first, kv.second);
    for (unsigned int src : used)
        apply_pair(src, slot_remap_lookup(map, src));

    for (ModelObject *mo : model.objects) {
        if (mo == nullptr)
            continue;

        remap_config_extruder(mo->config, map);

        for (ModelVolume *mv : mo->volumes) {
            if (mv == nullptr)
                continue;
            const ModelVolumeType vt          = mv->type();
            const bool            is_part     = (vt == ModelVolumeType::MODEL_PART);
            const bool            is_modifier = (vt == ModelVolumeType::PARAMETER_MODIFIER);
            if (is_part && !mv->mmu_segmentation_facets.empty())
                mv->remap_extruder_ids(dest_count, state_map);
            if (!is_part && !is_modifier)
                continue;
            remap_config_extruder(mv->config, map);
        }
        for (auto &lr : mo->layer_config_ranges)
            remap_config_extruder(lr.second, map);
    }

    for (auto &plate : model.plates_custom_gcodes) {
        for (CustomGCode::Item &item : plate.second.gcodes) {
            if (item.type != CustomGCode::Type::ToolChange)
                continue;
            if (item.extruder <= 0)
                continue;
            const unsigned int dest = slot_remap_lookup(map, static_cast<unsigned int>(item.extruder));
            if (dest != 0)
                item.extruder = static_cast<int>(dest);
        }
    }

    if (project_config != nullptr) {
        for (const char *key : kFeatureKeys)
            remap_int_option(*project_config, key, map);
        if (auto *seq = project_config->option<ConfigOptionInts>("first_layer_print_sequence"))
            slot_remap_int_sequence(seq->values, map);
    }

    return true;
}

bool should_prompt_remap_four_color_project(const SlotRemapPromptInput &in)
{
    if (in.silence)
        return false;
    if (in.enabled_mix_count > 0)
        return false;
    if (in.unique_color_count > 4)
        return false;
    if (!in.has_assignments)
        return false;
    if (!in.zr_ultra_s)
        return false;
    if (in.physical_count != 4)
        return false;

    bool ids_in_range = true;
    for (unsigned int id : in.used_ids) {
        if (id < 1 || id > 4) {
            ids_in_range = false;
            break;
        }
    }

    const bool native_zr = in.source_printer_model == "WonderMaker ZR Ultra S";
    if (native_zr && ids_in_range)
        return false;

    return true;
}

SlotRemapMap slot_remap_suggest(
    const std::vector<std::string>  &source_colours,
    const std::vector<std::string>  &dest_colours,
    const std::vector<unsigned int> &used_ids)
{
    SlotRemapMap out;
    if (dest_colours.empty() || used_ids.empty())
        return out;

    std::vector<CIELab> dest_lab(dest_colours.size());
    std::vector<char>   dest_ok(dest_colours.size(), 0);
    for (size_t i = 0; i < dest_colours.size(); ++i)
        dest_ok[i] = hex_to_lab(dest_colours[i], dest_lab[i]) ? 1 : 0;

    std::unordered_set<unsigned int> taken;
    for (unsigned int src : used_ids) {
        if (src == 0)
            continue;
        CIELab src_lab;
        const bool have_src = src - 1 < source_colours.size() &&
                              hex_to_lab(source_colours[src - 1], src_lab);
        if (!have_src) {
            out[src] = src;
            continue;
        }

        double best_free = std::numeric_limits<double>::infinity();
        size_t best_free_i = dest_colours.size();
        double best_any  = std::numeric_limits<double>::infinity();
        size_t best_any_i  = dest_colours.size();
        for (size_t i = 0; i < dest_colours.size(); ++i) {
            if (!dest_ok[i])
                continue;
            const double d = delta_e00(src_lab, dest_lab[i]);
            if (d < best_any) {
                best_any   = d;
                best_any_i = i;
            }
            const unsigned int dest = static_cast<unsigned int>(i + 1);
            if (taken.count(dest) == 0 && d < best_free) {
                best_free   = d;
                best_free_i = i;
            }
        }
        const size_t pick = best_free_i < dest_colours.size() ? best_free_i : best_any_i;
        if (pick >= dest_colours.size()) {
            out[src] = src;
            continue;
        }
        const unsigned int dest = static_cast<unsigned int>(pick + 1);
        out[src] = dest;
        taken.insert(dest);
    }
    return out;
}

} // namespace Slic3r

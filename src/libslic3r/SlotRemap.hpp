#ifndef slic3r_SlotRemap_hpp_
#define slic3r_SlotRemap_hpp_

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace Slic3r {

class Model;
class DynamicPrintConfig;

// Explicit 1-based source slot → 1-based destination toolhead.
// Missing keys are identity (Wondermaker SlotMap).
using SlotRemapMap = std::map<unsigned int, unsigned int>;

unsigned int slot_remap_lookup(const SlotRemapMap &map, unsigned int src_1based);

bool slot_remap_is_identity(const SlotRemapMap &map);
bool slot_remap_is_identity_for_used(
    const SlotRemapMap              &map,
    const std::vector<unsigned int> &used_1based);

// Paint, object/volume/layer "extruder", feature-filament keys, ToolChange
// custom gcodes, and first_layer_print_sequence on project_config.
std::vector<unsigned int> slot_remap_collect_used_ids(
    const Model              &model,
    const DynamicPrintConfig *project_config);

// Remap a 1-based int sequence in place. Values <= 0 are left as-is.
void slot_remap_int_sequence(std::vector<int> &seq, const SlotRemapMap &map);

// Validate then apply. Identity / empty map for used ids → true, no writes.
// Used source resolving to 0 or dest > dest_count → false, no writes.
// Never throws. Never writes filament_colour*. Never grows the filament list.
bool slot_remap_apply(
    Model              &model,
    DynamicPrintConfig *project_config,
    const SlotRemapMap &map,
    size_t              dest_count);

struct SlotRemapPromptInput
{
    bool                         silence             = false;
    size_t                       unique_color_count  = 0;
    size_t                       enabled_mix_count   = 0;
    bool                         has_assignments     = false;
    bool                         zr_ultra_s          = false; // adopt-or-active
    size_t                       physical_count      = 0;
    std::string                  source_printer_model;
    std::vector<std::string>     source_colours;
    std::vector<std::string>     dest_colours;
    std::vector<unsigned int>    used_ids;
};

bool should_prompt_remap_four_color_project(const SlotRemapPromptInput &in);

// Greedy CIEDE2000 suggestion. Missing colours → identity for that source.
SlotRemapMap slot_remap_suggest(
    const std::vector<std::string>  &source_colours,
    const std::vector<std::string>  &dest_colours,
    const std::vector<unsigned int> &used_ids);

} // namespace Slic3r

#endif

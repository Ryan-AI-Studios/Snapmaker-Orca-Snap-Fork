#ifndef slic3r_ZrToolheadLoadout_hpp_
#define slic3r_ZrToolheadLoadout_hpp_

#include <array>
#include <map>
#include <string>
#include <vector>

namespace Slic3r {

class AppConfig;
class DynamicPrintConfig;

static constexpr size_t kZrToolheadCount = 4;
static constexpr const char *kZrUltraSPrinterModel = "WonderMaker ZR Ultra S";
static constexpr const char *kZrUltraSPreset04 = "WonderMaker ZR Ultra S 0.4 nozzle";

// Concrete system preset names the loadout is keyed by.
const std::array<const char *, 4> &zr_ultra_s_system_preset_names();

bool is_zr_ultra_s_preset_name(const std::string &preset_name);
bool is_zr_ultra_s_printer_model(const std::string &printer_model);

// "WonderMaker ZR Ultra S 0.4 nozzle" from 0.4, etc. Unknown sizes → 0.4 name.
std::string zr_ultra_s_preset_name_for_nozzle(double nozzle_mm);

struct ZrToolheadLoadout
{
    std::array<std::string, kZrToolheadCount> filament_presets{};
    std::array<std::string, kZrToolheadCount> filament_colour{};
    std::array<std::string, kZrToolheadCount> filament_multi_colors{};
    std::array<int, kZrToolheadCount>         filament_colour_mode{};
    std::array<double, kZrToolheadCount>      nozzle_diameter{};

    bool has_filament_presets() const;
    bool has_colours() const;
    bool has_nozzles() const;
    bool empty() const;
};

bool operator==(const ZrToolheadLoadout &a, const ZrToolheadLoadout &b);
inline bool operator!=(const ZrToolheadLoadout &a, const ZrToolheadLoadout &b) { return !(a == b); }

// Missing or short AppConfig input → empty fields (not a guessed palette).
ZrToolheadLoadout zr_loadout_load(const AppConfig &config, const std::string &printer_preset_name);
void              zr_loadout_save(AppConfig &config, const std::string &printer_preset_name, const ZrToolheadLoadout &loadout);

std::string zr_loadout_format_nozzles(const std::array<double, kZrToolheadCount> &nozzles);
std::string zr_loadout_format_nozzles(const std::vector<double> &nozzles);
bool        zr_loadout_parse_nozzles(const std::string &raw, std::array<double, kZrToolheadCount> &out);

// Accepts "#RRGGBB" or "RRGGBB" (either case). Invalid → false.
bool zr_loadout_parse_hex(const std::string &raw, unsigned char &r, unsigned char &g, unsigned char &b);

// Pick the system preset adopt should select: uniform array matching 0.2/0.4/0.6/0.8
// uses that profile; otherwise first diameter; else 0.4.
std::string zr_loadout_pick_preset_name(const ZrToolheadLoadout &loadout);

// Destination colours/names from the AppConfig record. Nozzles from the record, or
// from zr_printer_preset_config when the nozzle key is absent/short. Never copies
// a caller source palette.
struct ZrLoadoutResolveResult
{
    ZrToolheadLoadout            loadout;
    std::vector<std::string>     dest_colours;
    std::vector<std::string>     dest_preset_names;
    std::array<double, kZrToolheadCount> nozzle_diameter{};
};

ZrLoadoutResolveResult zr_loadout_resolve(
    const AppConfig          &config,
    const std::string        &zr_preset_name,
    const DynamicPrintConfig *zr_printer_preset_config = nullptr);

// Which concrete ZR name to read for a foreign adopt. Prefers a ZR active name
// with a record, then the four system names (0.4 first).
std::string zr_loadout_lookup_name(const AppConfig &config, const std::string &active_printer_name);

// Full-record snapshot of the four system ZR names (presence included).
struct ZrPrinterSettingRecord
{
    bool                               present = false;
    std::map<std::string, std::string> keys;
};
using ZrAppConfigSnapshot = std::map<std::string, ZrPrinterSettingRecord>;

ZrAppConfigSnapshot zr_loadout_snapshot_appconfig(const AppConfig &config);
void                zr_loadout_restore_appconfig(AppConfig &config, const ZrAppConfigSnapshot &snap);

ZrToolheadLoadout zr_loadout_capture(
    const std::vector<std::string> &filament_presets,
    const DynamicPrintConfig       *project_config,
    const DynamicPrintConfig       *printer_config);

void zr_loadout_install(
    std::vector<std::string> &filament_presets,
    DynamicPrintConfig       *project_config,
    DynamicPrintConfig       *printer_config,
    const ZrToolheadLoadout  &loadout,
    bool                      write_colours,
    bool                      force = false);

} // namespace Slic3r

#endif

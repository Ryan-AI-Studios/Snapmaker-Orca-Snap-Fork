#include "ZrToolheadLoadout.hpp"

#include "AppConfig.hpp"
#include "LocalesUtils.hpp"
#include "Preset.hpp"
#include "PrintConfig.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/join.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <locale>
#include <sstream>

namespace Slic3r {

namespace {

const char *kSnapshotKeys[] = {
    PRESET_PRINTER_NAME,
    PRESET_PRINT_NAME,
    PRESET_FILAMENT_NAME,
    "filament_01",
    "filament_02",
    "filament_03",
    "filament_colors",
    "filament_multi_colors",
    "filament_colour_mode",
    "nozzle_diameter",
    "curr_bed_type",
    "flush_volumes_matrix",
    "flush_volumes_vector"};

std::string trim_copy(const std::string &s)
{
    return boost::algorithm::trim_copy(s);
}

std::vector<std::string> split_csv(const std::string &raw)
{
    std::vector<std::string> values;
    if (raw.empty())
        return values;
    boost::algorithm::split(values, raw, boost::algorithm::is_any_of(","));
    for (std::string &v : values)
        boost::algorithm::trim(v);
    return values;
}

std::string join_csv(const std::array<std::string, kZrToolheadCount> &values)
{
    std::vector<std::string> parts(values.begin(), values.end());
    return boost::algorithm::join(parts, ",");
}

std::string format_one_nozzle(double v)
{
    CNumericLocalesSetter locales_setter;
    std::ostringstream    oss;
    oss.imbue(std::locale::classic());
    oss << v;
    return oss.str();
}

bool parse_one_nozzle(const std::string &raw, double &out)
{
    const std::string s = trim_copy(raw);
    if (s.empty())
        return false;
    CNumericLocalesSetter locales_setter;
    char                 *end = nullptr;
    const double          v   = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || (end != nullptr && *end != '\0') || !std::isfinite(v) || v <= 0.0)
        return false;
    out = v;
    return true;
}

bool nearly_eq(double a, double b)
{
    return std::fabs(a - b) < 1e-4;
}

bool is_known_zr_nozzle(double v)
{
    return nearly_eq(v, 0.2) || nearly_eq(v, 0.4) || nearly_eq(v, 0.6) || nearly_eq(v, 0.8);
}

std::string filament_slot_key(size_t i)
{
    if (i == 0)
        return PRESET_FILAMENT_NAME;
    char name[64];
    std::sprintf(name, "filament_%02u", static_cast<unsigned>(i));
    return name;
}

} // namespace

const std::array<const char *, 4> &zr_ultra_s_system_preset_names()
{
    static const std::array<const char *, 4> names = {
        "WonderMaker ZR Ultra S 0.2 nozzle",
        "WonderMaker ZR Ultra S 0.4 nozzle",
        "WonderMaker ZR Ultra S 0.6 nozzle",
        "WonderMaker ZR Ultra S 0.8 nozzle"};
    return names;
}

bool is_zr_ultra_s_preset_name(const std::string &preset_name)
{
    return preset_name.find(kZrUltraSPrinterModel) != std::string::npos;
}

bool is_zr_ultra_s_printer_model(const std::string &printer_model)
{
    return printer_model == kZrUltraSPrinterModel;
}

std::string zr_ultra_s_preset_name_for_nozzle(double nozzle_mm)
{
    double use = 0.4;
    if (is_known_zr_nozzle(nozzle_mm))
        use = nozzle_mm;
    std::string s = float_to_string_decimal_point(use, 2);
    while (!s.empty() && s.back() == '0')
        s.pop_back();
    if (!s.empty() && s.back() == '.')
        s.pop_back();
    return std::string(kZrUltraSPrinterModel) + " " + s + " nozzle";
}

bool ZrToolheadLoadout::has_filament_presets() const
{
    for (const std::string &name : filament_presets) {
        if (name.empty())
            return false;
    }
    return true;
}

bool ZrToolheadLoadout::has_colours() const
{
    for (const std::string &c : filament_colour) {
        if (c.empty())
            return false;
    }
    return true;
}

bool ZrToolheadLoadout::has_nozzles() const
{
    for (double v : nozzle_diameter) {
        if (!(v > 0.0))
            return false;
    }
    return true;
}

bool ZrToolheadLoadout::empty() const
{
    return !has_filament_presets() && !has_colours() && !has_nozzles();
}

bool operator==(const ZrToolheadLoadout &a, const ZrToolheadLoadout &b)
{
    return a.filament_presets == b.filament_presets && a.filament_colour == b.filament_colour &&
           a.filament_multi_colors == b.filament_multi_colors && a.filament_colour_mode == b.filament_colour_mode &&
           a.nozzle_diameter == b.nozzle_diameter;
}

ZrToolheadLoadout zr_loadout_load(const AppConfig &config, const std::string &printer_preset_name)
{
    ZrToolheadLoadout out;
    if (printer_preset_name.empty() || !config.has_printer_settings(printer_preset_name))
        return out;

    bool names_ok = true;
    for (size_t i = 0; i < kZrToolheadCount; ++i) {
        const std::string key = filament_slot_key(i);
        if (!config.has_printer_setting(printer_preset_name, key)) {
            names_ok = false;
            break;
        }
        out.filament_presets[i] = config.get_printer_setting(printer_preset_name, key);
        if (out.filament_presets[i].empty())
            names_ok = false;
    }
    if (!names_ok)
        out.filament_presets = {};

    const std::vector<std::string> colours = split_csv(config.get_printer_setting(printer_preset_name, "filament_colors"));
    if (colours.size() == kZrToolheadCount) {
        for (size_t i = 0; i < kZrToolheadCount; ++i)
            out.filament_colour[i] = colours[i];
    }

    const std::vector<std::string> multi = split_csv(config.get_printer_setting(printer_preset_name, "filament_multi_colors"));
    if (multi.size() == kZrToolheadCount) {
        for (size_t i = 0; i < kZrToolheadCount; ++i)
            out.filament_multi_colors[i] = multi[i];
    }

    const std::vector<std::string> modes = split_csv(config.get_printer_setting(printer_preset_name, "filament_colour_mode"));
    if (modes.size() == kZrToolheadCount) {
        for (size_t i = 0; i < kZrToolheadCount; ++i) {
            char       *end   = nullptr;
            const long  parsed = std::strtol(modes[i].c_str(), &end, 10);
            if (end != modes[i].c_str() && (end == nullptr || *end == '\0'))
                out.filament_colour_mode[i] = parsed == 0 ? 0 : 1;
        }
    }

    std::array<double, kZrToolheadCount> nozzles{};
    if (zr_loadout_parse_nozzles(config.get_printer_setting(printer_preset_name, "nozzle_diameter"), nozzles))
        out.nozzle_diameter = nozzles;

    return out;
}

void zr_loadout_save(AppConfig &config, const std::string &printer_preset_name, const ZrToolheadLoadout &loadout)
{
    if (printer_preset_name.empty())
        return;

    if (loadout.has_filament_presets()) {
        for (size_t i = 0; i < kZrToolheadCount; ++i)
            config.set_printer_setting(printer_preset_name, filament_slot_key(i), loadout.filament_presets[i]);
    }
    if (loadout.has_colours()) {
        config.set_printer_setting(printer_preset_name, "filament_colors", join_csv(loadout.filament_colour));
        config.set_printer_setting(printer_preset_name, "filament_multi_colors", join_csv(loadout.filament_multi_colors));
        std::array<std::string, kZrToolheadCount> mode_strs;
        for (size_t i = 0; i < kZrToolheadCount; ++i)
            mode_strs[i] = std::to_string(loadout.filament_colour_mode[i] == 0 ? 0 : 1);
        config.set_printer_setting(printer_preset_name, "filament_colour_mode", join_csv(mode_strs));
    }
    if (loadout.has_nozzles())
        config.set_printer_setting(printer_preset_name, "nozzle_diameter", zr_loadout_format_nozzles(loadout.nozzle_diameter));
}

std::string zr_loadout_format_nozzles(const std::array<double, kZrToolheadCount> &nozzles)
{
    std::array<std::string, kZrToolheadCount> parts;
    for (size_t i = 0; i < kZrToolheadCount; ++i)
        parts[i] = format_one_nozzle(nozzles[i]);
    return join_csv(parts);
}

std::string zr_loadout_format_nozzles(const std::vector<double> &nozzles)
{
    std::array<double, kZrToolheadCount> arr{};
    const size_t                         n = std::min(nozzles.size(), kZrToolheadCount);
    for (size_t i = 0; i < n; ++i)
        arr[i] = nozzles[i];
    return zr_loadout_format_nozzles(arr);
}

bool zr_loadout_parse_nozzles(const std::string &raw, std::array<double, kZrToolheadCount> &out)
{
    out = {};
    const std::vector<std::string> parts = split_csv(raw);
    if (parts.size() != kZrToolheadCount)
        return false;
    std::array<double, kZrToolheadCount> tmp{};
    for (size_t i = 0; i < kZrToolheadCount; ++i) {
        if (!parse_one_nozzle(parts[i], tmp[i]))
            return false;
    }
    out = tmp;
    return true;
}

bool zr_loadout_parse_hex(const std::string &raw, unsigned char &r, unsigned char &g, unsigned char &b)
{
    std::string s;
    s.reserve(raw.size());
    for (unsigned char c : raw) {
        if (!std::isspace(c))
            s.push_back(static_cast<char>(c));
    }
    const auto pipe = s.find('|');
    if (pipe != std::string::npos)
        s.resize(pipe);
    if (!s.empty() && s.front() == '#')
        s.erase(s.begin());
    if (s.size() >= 8)
        s.resize(6);
    else if (s.size() == 3)
        s = {s[0], s[0], s[1], s[1], s[2], s[2]};
    if (s.size() != 6)
        return false;
    for (char c : s) {
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
    }
    auto nyb = [](char c) -> unsigned {
        if (c >= '0' && c <= '9')
            return static_cast<unsigned>(c - '0');
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return static_cast<unsigned>(10 + c - 'a');
    };
    r = static_cast<unsigned char>((nyb(s[0]) << 4) | nyb(s[1]));
    g = static_cast<unsigned char>((nyb(s[2]) << 4) | nyb(s[3]));
    b = static_cast<unsigned char>((nyb(s[4]) << 4) | nyb(s[5]));
    return true;
}

std::string zr_loadout_pick_preset_name(const ZrToolheadLoadout &loadout)
{
    if (loadout.has_nozzles()) {
        const double first = loadout.nozzle_diameter[0];
        bool         uniform = true;
        for (size_t i = 1; i < kZrToolheadCount; ++i) {
            if (!nearly_eq(loadout.nozzle_diameter[i], first)) {
                uniform = false;
                break;
            }
        }
        if (uniform && is_known_zr_nozzle(first))
            return zr_ultra_s_preset_name_for_nozzle(first);
        if (is_known_zr_nozzle(first))
            return zr_ultra_s_preset_name_for_nozzle(first);
    }
    return kZrUltraSPreset04;
}

ZrLoadoutResolveResult zr_loadout_resolve(
    const AppConfig          &config,
    const std::string        &zr_preset_name,
    const DynamicPrintConfig *zr_printer_preset_config)
{
    ZrLoadoutResolveResult out;
    out.loadout = zr_loadout_load(config, zr_preset_name);
    out.dest_colours.assign(kZrToolheadCount, std::string());
    out.dest_preset_names.assign(kZrToolheadCount, std::string());
    for (size_t i = 0; i < kZrToolheadCount; ++i) {
        out.dest_colours[i]      = out.loadout.filament_colour[i];
        out.dest_preset_names[i] = out.loadout.filament_presets[i];
    }
    if (out.loadout.has_nozzles()) {
        out.nozzle_diameter = out.loadout.nozzle_diameter;
    } else if (zr_printer_preset_config != nullptr) {
        if (const auto *nd = zr_printer_preset_config->option<ConfigOptionFloats>("nozzle_diameter");
            nd != nullptr && nd->values.size() >= kZrToolheadCount) {
            for (size_t i = 0; i < kZrToolheadCount; ++i)
                out.nozzle_diameter[i] = nd->values[i];
            out.loadout.nozzle_diameter = out.nozzle_diameter;
        }
    }
    return out;
}

std::string zr_loadout_lookup_name(const AppConfig &config, const std::string &active_printer_name)
{
    auto has_record = [&](const std::string &name) {
        return !name.empty() && is_zr_ultra_s_preset_name(name) && config.has_printer_settings(name);
    };
    if (has_record(active_printer_name))
        return active_printer_name;

    // Prefer 0.4, then the other system names.
    const char *preferred[] = {
        kZrUltraSPreset04,
        "WonderMaker ZR Ultra S 0.2 nozzle",
        "WonderMaker ZR Ultra S 0.6 nozzle",
        "WonderMaker ZR Ultra S 0.8 nozzle"};
    for (const char *name : preferred) {
        if (config.has_printer_settings(name))
            return name;
    }
    return kZrUltraSPreset04;
}

ZrAppConfigSnapshot zr_loadout_snapshot_appconfig(const AppConfig &config)
{
    ZrAppConfigSnapshot snap;
    for (const char *name : zr_ultra_s_system_preset_names()) {
        ZrPrinterSettingRecord rec;
        rec.present = config.has_printer_settings(name);
        if (rec.present) {
            for (const char *key : kSnapshotKeys) {
                if (config.has_printer_setting(name, key))
                    rec.keys[key] = config.get_printer_setting(name, key);
            }
            for (unsigned i = 4; i <= 16; ++i) {
                char buf[64];
                std::sprintf(buf, "filament_%02u", i);
                if (config.has_printer_setting(name, buf))
                    rec.keys[buf] = config.get_printer_setting(name, buf);
            }
        }
        snap[name] = std::move(rec);
    }
    return snap;
}

void zr_loadout_restore_appconfig(AppConfig &config, const ZrAppConfigSnapshot &snap)
{
    for (const char *name : zr_ultra_s_system_preset_names()) {
        auto it = snap.find(name);
        const bool present = it != snap.end() && it->second.present;
        if (config.has_printer_settings(name))
            config.clear_printer_settings(name);
        if (!present)
            continue;
        for (const auto &kv : it->second.keys)
            config.set_printer_setting(name, kv.first, kv.second);
    }
}

ZrToolheadLoadout zr_loadout_capture(
    const std::vector<std::string> &filament_presets,
    const DynamicPrintConfig       *project_config,
    const DynamicPrintConfig       *printer_config)
{
    ZrToolheadLoadout out;
    for (size_t i = 0; i < kZrToolheadCount; ++i) {
        if (i < filament_presets.size())
            out.filament_presets[i] = filament_presets[i];
    }
    if (project_config != nullptr) {
        if (const auto *fc = project_config->option<ConfigOptionStrings>("filament_colour")) {
            for (size_t i = 0; i < kZrToolheadCount && i < fc->values.size(); ++i)
                out.filament_colour[i] = fc->values[i];
        }
        if (const auto *mc = project_config->option<ConfigOptionStrings>("filament_multi_colors")) {
            for (size_t i = 0; i < kZrToolheadCount && i < mc->values.size(); ++i)
                out.filament_multi_colors[i] = mc->values[i];
        }
        if (const auto *modes = project_config->option<ConfigOptionInts>("filament_colour_mode")) {
            for (size_t i = 0; i < kZrToolheadCount && i < modes->values.size(); ++i)
                out.filament_colour_mode[i] = modes->values[i];
        }
    }
    if (printer_config != nullptr) {
        if (const auto *nd = printer_config->option<ConfigOptionFloats>("nozzle_diameter");
            nd != nullptr && nd->values.size() >= kZrToolheadCount) {
            for (size_t i = 0; i < kZrToolheadCount; ++i)
                out.nozzle_diameter[i] = nd->values[i];
        }
    }
    return out;
}

void zr_loadout_install(
    std::vector<std::string> &filament_presets,
    DynamicPrintConfig       *project_config,
    DynamicPrintConfig       *printer_config,
    const ZrToolheadLoadout  &loadout,
    bool                      write_colours,
    bool                      force)
{
    if (force || loadout.has_filament_presets()) {
        filament_presets.resize(kZrToolheadCount);
        for (size_t i = 0; i < kZrToolheadCount; ++i)
            filament_presets[i] = loadout.filament_presets[i];
    }
    if (write_colours && project_config != nullptr && (force || loadout.has_colours())) {
        std::vector<std::string> colours(kZrToolheadCount);
        std::vector<std::string> multi(kZrToolheadCount);
        std::vector<int>         modes(kZrToolheadCount, 0);
        for (size_t i = 0; i < kZrToolheadCount; ++i) {
            colours[i] = loadout.filament_colour[i];
            multi[i]   = loadout.filament_multi_colors[i].empty() ? loadout.filament_colour[i]
                                                               : loadout.filament_multi_colors[i];
            modes[i]   = loadout.filament_colour_mode[i] == 0 ? 0 : 1;
        }
        if (auto *fc = project_config->option<ConfigOptionStrings>("filament_colour")) {
            fc->values.resize(kZrToolheadCount);
            for (size_t i = 0; i < kZrToolheadCount; ++i)
                fc->values[i] = colours[i];
        }
        if (auto *mc = project_config->option<ConfigOptionStrings>("filament_multi_colors", true)) {
            mc->values.resize(kZrToolheadCount);
            for (size_t i = 0; i < kZrToolheadCount; ++i)
                mc->values[i] = multi[i];
        }
        if (auto *md = project_config->option<ConfigOptionInts>("filament_colour_mode", true)) {
            md->values.resize(kZrToolheadCount);
            for (size_t i = 0; i < kZrToolheadCount; ++i)
                md->values[i] = modes[i];
        }
    }
    if (printer_config != nullptr && loadout.has_nozzles()) {
        if (auto *nd = printer_config->option<ConfigOptionFloats>("nozzle_diameter")) {
            nd->values.assign(loadout.nozzle_diameter.begin(), loadout.nozzle_diameter.end());
        }
    }
}

} // namespace Slic3r

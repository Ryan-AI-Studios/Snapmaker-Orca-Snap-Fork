#include "OfdCatalog.hpp"

#include "FilamentColorLibrary.hpp"
#include "Utils.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <unordered_set>

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>

namespace Slic3r {

namespace {

std::string trim_copy(const std::string &s)
{
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])))
        ++b;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
        --e;
    return s.substr(b, e - b);
}

std::string ascii_lower(std::string s)
{
    for (char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool ci_contains(const std::string &hay, const std::string &needle)
{
    if (needle.empty())
        return true;
    return ascii_lower(hay).find(ascii_lower(needle)) != std::string::npos;
}

std::string variant_ci_key(const OfdVariant &v)
{
    return ascii_lower(trim_copy(v.brand)) + "|" + ascii_lower(trim_copy(v.filament)) + "|" +
           ascii_lower(trim_copy(v.variant));
}

std::string json_string_field(const nlohmann::json &j, const char *key)
{
    if (!j.is_object() || !j.contains(key))
        return {};
    const nlohmann::json &v = j[key];
    if (v.is_string())
        return v.get<std::string>();
    if (v.is_object() && v.contains("name") && v["name"].is_string())
        return v["name"].get<std::string>();
    return {};
}

bool json_bool_field(const nlohmann::json &j, const char *key)
{
    if (!j.is_object() || !j.contains(key))
        return false;
    const nlohmann::json &v = j[key];
    if (v.is_boolean())
        return v.get<bool>();
    if (v.is_number_integer())
        return v.get<int>() != 0;
    if (v.is_string()) {
        const std::string s = ascii_lower(v.get<std::string>());
        return s == "true" || s == "1" || s == "yes";
    }
    return false;
}

std::string json_id_field(const nlohmann::json &j, const char *key)
{
    if (!j.is_object() || !j.contains(key))
        return {};
    const nlohmann::json &v = j[key];
    if (v.is_string())
        return v.get<std::string>();
    if (v.is_number_integer())
        return std::to_string(v.get<long long>());
    return {};
}

std::vector<std::string> parse_color_hexes(const nlohmann::json &j)
{
    std::vector<std::string> out;
    if (!j.is_object() || !j.contains("color_hex"))
        return out;
    const nlohmann::json &hx = j["color_hex"];
    auto push = [&](const nlohmann::json &item) {
        if (!item.is_string())
            return;
        const std::string n = NormalizeFilamentHexColor(item.get<std::string>());
        if (!n.empty())
            out.push_back(n);
    };
    if (hx.is_string())
        push(hx);
    else if (hx.is_array()) {
        for (const nlohmann::json &item : hx)
            push(item);
    }
    return out;
}

bool parse_variant_object(const nlohmann::json &j, OfdVariant &out)
{
    if (!j.is_object())
        return false;
    OfdVariant v;
    v.color_hexes = parse_color_hexes(j);
    if (v.color_hexes.empty())
        return false;

    v.brand = json_string_field(j, "brand");
    if (v.brand.empty())
        v.brand = json_string_field(j, "brand_name");

    v.filament = json_string_field(j, "filament");
    if (v.filament.empty())
        v.filament = json_string_field(j, "filament_name");

    v.variant = json_string_field(j, "variant");
    if (v.variant.empty())
        v.variant = json_string_field(j, "name");

    v.material = json_string_field(j, "material");

    if (j.contains("traits") && j["traits"].is_object()) {
        v.translucent = json_bool_field(j["traits"], "translucent");
        v.transparent = json_bool_field(j["traits"], "transparent");
    }
    if (!v.translucent)
        v.translucent = json_bool_field(j, "translucent");
    if (!v.transparent)
        v.transparent = json_bool_field(j, "transparent");

    out = std::move(v);
    return true;
}

void append_from_json(const nlohmann::json &j, std::vector<OfdVariant> &out)
{
    if (j.is_array()) {
        for (const nlohmann::json &item : j) {
            OfdVariant v;
            if (parse_variant_object(item, v))
                out.push_back(std::move(v));
        }
        return;
    }
    if (!j.is_object())
        return;
    if (j.contains("variants") && j["variants"].is_array()) {
        append_from_json(j["variants"], out);
        return;
    }
    OfdVariant v;
    if (parse_variant_object(j, v))
        out.push_back(std::move(v));
}

std::string read_file_or_empty(const std::string &path)
{
    if (path.empty())
        return {};
    boost::nowide::ifstream ifs(path, std::ios::binary);
    if (!ifs)
        return {};
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

struct OfdFilamentRow
{
    std::string name;
    std::string brand_id;
    std::string material;
};

bool emit_joined_variant(
    const nlohmann::json                        &j,
    const std::map<std::string, std::string>    &brands,
    const std::map<std::string, OfdFilamentRow> &filaments,
    std::vector<OfdVariant>                     &out)
{
    OfdVariant v;
    if (!parse_variant_object(j, v))
        return true;
    const std::string fid = json_id_field(j, "filament_id");
    if (fid.empty())
        return true;
    auto fit = filaments.find(fid);
    if (fit == filaments.end())
        return false;
    const OfdFilamentRow &f = fit->second;
    auto bit = brands.find(f.brand_id);
    if (bit == brands.end())
        return false;
    v.brand    = bit->second;
    v.filament = f.name;
    if (v.material.empty())
        v.material = f.material;
    out.push_back(std::move(v));
    return true;
}

std::vector<OfdVariant> parse_ofd_typed_ndjson(const std::string &text)
{
    std::vector<OfdVariant>                  out;
    std::map<std::string, std::string>       brands;
    std::map<std::string, OfdFilamentRow>    filaments;
    std::vector<nlohmann::json>              buffered;

    std::istringstream iss(text);
    std::string        line;
    while (std::getline(iss, line)) {
        const std::string t = trim_copy(line);
        if (t.empty())
            continue;
        nlohmann::json j = nlohmann::json::parse(t, nullptr, false);
        if (j.is_discarded() || !j.is_object())
            continue;
        if (!j.contains("_type")) {
            append_from_json(j, out);
            continue;
        }
        const std::string type = ascii_lower(json_string_field(j, "_type"));
        if (type == "brand") {
            const std::string id = json_id_field(j, "id");
            const std::string nm = json_string_field(j, "name");
            if (!id.empty() && !nm.empty())
                brands[id] = nm;
        } else if (type == "filament") {
            const std::string id = json_id_field(j, "id");
            if (id.empty())
                continue;
            OfdFilamentRow row;
            row.name     = json_string_field(j, "name");
            row.brand_id = json_id_field(j, "brand_id");
            row.material = json_string_field(j, "material");
            filaments[id] = std::move(row);
        } else if (type == "variant") {
            if (!emit_joined_variant(j, brands, filaments, out))
                buffered.push_back(std::move(j));
        }
    }
    for (const nlohmann::json &j : buffered)
        emit_joined_variant(j, brands, filaments, out);
    return out;
}

bool first_line_has_type_key(const std::string &text)
{
    std::istringstream iss(text);
    std::string        line;
    while (std::getline(iss, line)) {
        const std::string t = trim_copy(line);
        if (t.empty())
            continue;
        return t.find("\"_type\"") != std::string::npos;
    }
    return false;
}

void catalog_append_keep_first(std::vector<OfdVariant> &out, const std::vector<OfdVariant> &extra)
{
    std::unordered_set<std::string> seen;
    seen.reserve(out.size() + extra.size());
    for (const OfdVariant &v : out)
        seen.insert(variant_ci_key(v));
    for (const OfdVariant &v : extra) {
        if (seen.insert(variant_ci_key(v)).second)
            out.push_back(v);
    }
}

} // namespace

std::string ofd_variant_key(const OfdVariant &v) { return variant_ci_key(v); }

std::vector<OfdVariant> ofd_parse(const std::string &text)
{
    std::vector<OfdVariant> out;
    const std::string       trimmed = trim_copy(text);
    if (trimmed.empty())
        return out;

    if (first_line_has_type_key(text))
        return parse_ofd_typed_ndjson(text);

    nlohmann::json j = nlohmann::json::parse(trimmed, nullptr, false);
    if (!j.is_discarded() && (j.is_object() || j.is_array())) {
        append_from_json(j, out);
        return out;
    }

    std::istringstream iss(text);
    std::string        line;
    while (std::getline(iss, line)) {
        const std::string t = trim_copy(line);
        if (t.empty())
            continue;
        nlohmann::json lj = nlohmann::json::parse(t, nullptr, false);
        if (lj.is_discarded())
            continue;
        append_from_json(lj, out);
    }
    return out;
}

std::vector<OfdVariant> ofd_load_catalog(const std::string &seed_json_path, const std::string &user_ndjson_path)
{
    std::vector<OfdVariant> out = ofd_parse(read_file_or_empty(seed_json_path));
    if (!user_ndjson_path.empty()) {
        const auto extra = ofd_parse(read_file_or_empty(user_ndjson_path));
        catalog_append_keep_first(out, extra);
    }
    return out;
}

void ofd_recents_push(std::vector<OfdVariant> &recents, const OfdVariant &applied, size_t cap)
{
    if (cap == 0) {
        recents.clear();
        return;
    }
    const std::string key = variant_ci_key(applied);
    recents.erase(
        std::remove_if(recents.begin(), recents.end(),
                       [&](const OfdVariant &v) { return variant_ci_key(v) == key; }),
        recents.end());
    recents.insert(recents.begin(), applied);
    if (recents.size() > cap)
        recents.resize(cap);
}

std::vector<OfdVariant> ofd_recents_parse(const std::string &text)
{
    std::vector<OfdVariant> out;
    const std::string       trimmed = trim_copy(text);
    if (trimmed.empty())
        return out;
    nlohmann::json j = nlohmann::json::parse(trimmed, nullptr, false);
    if (j.is_discarded())
        return out;
    append_from_json(j, out);
    return out;
}

std::string ofd_recents_serialize(const std::vector<OfdVariant> &recents)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const OfdVariant &v : recents) {
        nlohmann::json o = nlohmann::json::object();
        o["brand"]    = v.brand;
        o["filament"] = v.filament;
        o["variant"]  = v.variant;
        if (!v.material.empty())
            o["material"] = v.material;
        if (v.color_hexes.size() == 1)
            o["color_hex"] = v.color_hexes.front();
        else
            o["color_hex"] = v.color_hexes;
        if (v.translucent)
            o["translucent"] = true;
        if (v.transparent)
            o["transparent"] = true;
        arr.push_back(std::move(o));
    }
    return arr.dump();
}

std::vector<OfdVariant> ofd_lookup(
    const std::vector<OfdVariant> &catalog,
    const std::string             &brand_filter,
    const std::string             &name_substring)
{
    const std::string           brand  = trim_copy(brand_filter);
    const std::string           needle = trim_copy(name_substring);
    std::vector<OfdVariant>     out;
    for (const OfdVariant &v : catalog) {
        if (!brand.empty() && !ci_contains(v.brand, brand))
            continue;
        if (!needle.empty()) {
            const bool hit_name  = ci_contains(v.filament, needle) || ci_contains(v.variant, needle) ||
                                  ci_contains(v.material, needle);
            const bool hit_brand = brand.empty() && ci_contains(v.brand, needle);
            if (!hit_name && !hit_brand)
                continue;
        }
        out.push_back(v);
    }
    return out;
}

OfdComposedList ofd_compose_list(
    const std::vector<OfdVariant> &recents,
    const std::vector<OfdVariant> &catalog,
    const std::string             &brand_filter,
    const std::string             &name_substring)
{
    OfdComposedList view;
    view.matches       = ofd_lookup(recents, brand_filter, name_substring);
    view.recent_prefix = view.matches.size();
    std::unordered_set<std::string> seen;
    seen.reserve(view.matches.size());
    for (const OfdVariant &v : view.matches)
        seen.insert(variant_ci_key(v));
    for (const OfdVariant &v : ofd_lookup(catalog, brand_filter, name_substring)) {
        if (seen.insert(variant_ci_key(v)).second)
            view.matches.push_back(v);
    }
    return view;
}

std::string ofd_slot_hex(const OfdVariant &v)
{
    return v.color_hexes.empty() ? std::string() : v.color_hexes.front();
}

bool ofd_stamp_slot(
    std::vector<std::string>       &filament_colour,
    std::vector<std::string>       &filament_multi_colors,
    std::vector<int>               &filament_colour_mode,
    size_t                          slot,
    const std::vector<std::string> &hexes)
{
    if (hexes.empty() || slot >= filament_colour.size())
        return false;

    std::vector<std::string> norm;
    norm.reserve(hexes.size());
    for (const std::string &h : hexes) {
        const std::string n = NormalizeFilamentHexColor(h);
        if (!n.empty())
            norm.push_back(n);
    }
    if (norm.empty())
        return false;

    if (filament_multi_colors.size() < filament_colour.size())
        filament_multi_colors.resize(filament_colour.size());
    if (filament_colour_mode.size() < filament_colour.size())
        filament_colour_mode.resize(filament_colour.size(), FilamentColorModeToConfig(FilamentColorMode::Segment));

    filament_colour[slot]        = norm.front();
    filament_multi_colors[slot]  = JoinFilamentMultiColors(norm);
    filament_colour_mode[slot]   = FilamentColorModeToConfig(FilamentColorMode::Segment);
    return true;
}

std::string ofd_default_seed_path()
{
    return (boost::filesystem::path(resources_dir()) / "ofd" / "ofd_seed.json").string();
}

std::string ofd_default_overlay_path()
{
    return (boost::filesystem::path(data_dir()) / "ofd" / "ofd_custom.ndjson").string();
}

std::string ofd_default_recents_path()
{
    return (boost::filesystem::path(data_dir()) / "ofd" / "ofd_recents.json").string();
}

bool ofd_save_recents_file(const std::string &path, const std::vector<OfdVariant> &recents)
{
    if (path.empty())
        return false;
    boost::system::error_code ec;
    boost::filesystem::create_directories(boost::filesystem::path(path).parent_path(), ec);
    boost::nowide::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs)
        return false;
    ofs << ofd_recents_serialize(recents);
    return static_cast<bool>(ofs);
}

std::vector<OfdVariant> ofd_load_recents_file(const std::string &path)
{
    return ofd_recents_parse(read_file_or_empty(path));
}

} // namespace Slic3r

#include "MixedFilamentSwatch.hpp"
#include "Utils.hpp"

#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/uuid/detail/md5.hpp>
#include <nlohmann/json.hpp>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>

namespace Slic3r {

namespace {

bool is_finite_lab(double L, double a, double b)
{
    return std::isfinite(L) && std::isfinite(a) && std::isfinite(b);
}

bool accept_entry(SwatchLut &lut, SwatchParseReport &report, std::string key, double L, double a, double b)
{
    if (!is_valid_swatch_recipe_key(key)) {
        ++report.skipped_unknown_key;
        return true;
    }
    if (!is_finite_lab(L, a, b)) {
        ++report.skipped_nonfinite;
        return true;
    }
    if (lut.index.count(key) != 0) {
        ++report.skipped_duplicate;
        return true;
    }
    lut.index.emplace(key, lut.entries.size());
    lut.entries.push_back({std::move(key), CIELab{L, a, b}});
    ++report.accepted;
    return true;
}

std::string md5_hex(const std::string &data)
{
    using boost::uuids::detail::md5;
    md5              hash;
    md5::digest_type digest{};
    hash.process_bytes(data.data(), data.size());
    hash.get_digest(digest);
    std::string hex;
    boost::algorithm::hex(digest, digest + std::size(digest), std::back_inserter(hex));
    for (char &c : hex)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return hex;
}

} // namespace

std::string snap_recipe_key_pair(unsigned component_a, unsigned component_b, int mix_b_percent)
{
    return "pair:" + std::to_string(component_a) + ":" + std::to_string(component_b) + ":"
           + std::to_string(mix_b_percent);
}

std::string snap_recipe_key_grad(const std::string &encoded_ids, const std::string &encoded_weights)
{
    return "grad:" + encoded_ids + ":" + encoded_weights;
}

std::string snap_recipe_key_phys(unsigned physical_id) { return "phys:" + std::to_string(physical_id); }

bool is_valid_swatch_recipe_key(const std::string &key)
{
    if (key.rfind("pair:", 0) == 0) {
        unsigned a = 0, b = 0;
        int      pct = 0;
        char     extra = 0;
        if (std::sscanf(key.c_str(), "pair:%u:%u:%d%c", &a, &b, &pct, &extra) != 3)
            return false;
        return a >= 1 && b >= 1 && pct >= 0 && pct <= 100;
    }
    if (key.rfind("grad:", 0) == 0) {
        const auto colon = key.find(':', 5);
        return colon != std::string::npos && colon > 5 && colon + 1 < key.size();
    }
    if (key.rfind("phys:", 0) == 0) {
        unsigned id = 0;
        char     extra = 0;
        if (std::sscanf(key.c_str(), "phys:%u%c", &id, &extra) != 1)
            return false;
        return id >= 1;
    }
    return false;
}

std::string compute_swatch_batch_key(const std::vector<std::string> &hexes,
                                     const std::vector<std::string> &type_names)
{
    std::ostringstream ss;
    for (size_t i = 0; i < hexes.size(); ++i) {
        if (i)
            ss << '|';
        const std::string n = normalize_painted_colour_hex(hexes[i]);
        ss << (n.empty() ? hexes[i] : n);
    }
    if (!type_names.empty()) {
        ss << '#';
        for (size_t i = 0; i < type_names.size(); ++i) {
            if (i)
                ss << '|';
            ss << type_names[i];
        }
    }
    return md5_hex(ss.str());
}

std::string default_swatch_lut_path()
{
    boost::filesystem::path p(data_dir());
    p /= "mixed_filament";
    p /= "swatch_lut.json";
    return p.make_preferred().string();
}

bool parse_swatch_lut_json(const std::string &text, SwatchLut &out, SwatchParseReport &report)
{
    report = {};
    out    = {};
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(text);
    } catch (const std::exception &ex) {
        report.error = ex.what();
        return false;
    }
    if (!root.is_object()) {
        report.error = "root is not an object";
        return false;
    }
    if (!root.contains("version") || !root["version"].is_number_integer() || root["version"].get<int>() != 1) {
        report.error = "unsupported or missing version (expected 1)";
        return false;
    }
    out.version = 1;
    if (root.contains("batch_key") && root["batch_key"].is_string())
        out.batch_key = root["batch_key"].get<std::string>();
    if (root.contains("lighting") && root["lighting"].is_string())
        out.lighting = root["lighting"].get<std::string>();
    if (root.contains("illuminant") && root["illuminant"].is_string())
        out.illuminant = root["illuminant"].get<std::string>();
    if (!root.contains("entries") || !root["entries"].is_array()) {
        report.error = "entries must be an array";
        return false;
    }
    for (const auto &item : root["entries"]) {
        if (!item.is_object() || !item.contains("recipe_key") || !item["recipe_key"].is_string()) {
            ++report.skipped_unknown_key;
            continue;
        }
        double L = 0, a = 0, b = 0;
        try {
            L = item.value("L", 0.0);
            a = item.value("a", 0.0);
            b = item.value("b", 0.0);
        } catch (...) {
            ++report.skipped_nonfinite;
            continue;
        }
        accept_entry(out, report, item["recipe_key"].get<std::string>(), L, a, b);
    }
    report.ok = true;
    return true;
}

bool parse_swatch_lut_csv(const std::string &text, SwatchLut &out, SwatchParseReport &report)
{
    report = {};
    out    = {};
    out.version = 1;
    std::istringstream in(text);
    std::string        line;
    bool               header_ok = false;
    int                col_key = -1, col_L = -1, col_a = -1, col_b = -1;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        if (line[0] == '#') {
            const auto eq = line.find('=');
            if (eq != std::string::npos) {
                std::string k = line.substr(1, eq - 1);
                std::string v = line.substr(eq + 1);
                boost::algorithm::trim(k);
                boost::algorithm::trim(v);
                boost::algorithm::to_lower(k);
                if (k == "batch_key")
                    out.batch_key = v;
                else if (k == "lighting")
                    out.lighting = v;
                else if (k == "illuminant")
                    out.illuminant = v;
            }
            continue;
        }
        std::vector<std::string> cols;
        boost::split(cols, line, boost::is_any_of(","));
        for (auto &c : cols)
            boost::algorithm::trim(c);
        if (!header_ok) {
            for (int i = 0; i < static_cast<int>(cols.size()); ++i) {
                std::string h = cols[static_cast<size_t>(i)];
                boost::algorithm::to_lower(h);
                if (h == "recipe_key" || h == "key")
                    col_key = i;
                else if (h == "l")
                    col_L = i;
                else if (h == "a")
                    col_a = i;
                else if (h == "b")
                    col_b = i;
            }
            if (col_key < 0 || col_L < 0 || col_a < 0 || col_b < 0) {
                report.error = "CSV header must include recipe_key,L,a,b";
                return false;
            }
            header_ok = true;
            continue;
        }
        const int need = std::max(std::max(col_key, col_L), std::max(col_a, col_b));
        if (static_cast<int>(cols.size()) <= need) {
            ++report.skipped_unknown_key;
            continue;
        }
        double L = 0, a = 0, b = 0;
        try {
            L = std::stod(cols[static_cast<size_t>(col_L)]);
            a = std::stod(cols[static_cast<size_t>(col_a)]);
            b = std::stod(cols[static_cast<size_t>(col_b)]);
        } catch (...) {
            ++report.skipped_nonfinite;
            continue;
        }
        accept_entry(out, report, cols[static_cast<size_t>(col_key)], L, a, b);
    }
    if (!header_ok) {
        report.error = "CSV missing header";
        return false;
    }
    report.ok = true;
    return true;
}

std::string serialize_swatch_lut_json(const SwatchLut &lut)
{
    nlohmann::json root;
    root["version"]    = lut.version == 0 ? 1 : lut.version;
    root["batch_key"]  = lut.batch_key;
    root["lighting"]   = lut.lighting;
    root["illuminant"] = lut.illuminant;
    root["entries"]    = nlohmann::json::array();
    for (const auto &e : lut.entries) {
        nlohmann::json item;
        item["recipe_key"] = e.recipe_key;
        item["L"]          = e.lab.L;
        item["a"]          = e.lab.a;
        item["b"]          = e.lab.b;
        root["entries"].push_back(std::move(item));
    }
    return root.dump(2);
}

bool load_swatch_lut_file(const std::string &path, SwatchLut &out, SwatchParseReport &report)
{
    report = {};
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        report.error = "cannot open file";
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    const bool json        = boost::algorithm::iends_with(path, ".json") || (!text.empty() && text.front() == '{');
    const bool ok          = json ? parse_swatch_lut_json(text, out, report) : parse_swatch_lut_csv(text, out, report);
    if (ok)
        out.source_path = path;
    return ok;
}

bool save_swatch_lut(const std::string &path, const SwatchLut &lut, std::string *error)
{
    boost::filesystem::path dest(path);
    boost::system::error_code ec;
    if (dest.has_parent_path())
        boost::filesystem::create_directories(dest.parent_path(), ec);
    if (ec) {
        if (error)
            *error = ec.message();
        return false;
    }
    boost::filesystem::path tmp = dest;
    tmp += ".tmp";
    {
        boost::filesystem::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error)
                *error = "cannot write temp file";
            return false;
        }
        const std::string body = serialize_swatch_lut_json(lut);
        out.write(body.data(), static_cast<std::streamsize>(body.size()));
        if (!out) {
            if (error)
                *error = "write failed";
            return false;
        }
    }
    boost::filesystem::rename(tmp, dest, ec);
    if (ec) {
        boost::filesystem::remove(dest, ec);
        boost::filesystem::rename(tmp, dest, ec);
    }
    if (ec) {
        if (error)
            *error = ec.message();
        boost::filesystem::remove(tmp);
        return false;
    }
    return true;
}

bool lut_is_stale(const SwatchLut &lut, const std::string &live_batch_key)
{
    if (lut.batch_key.empty() || live_batch_key.empty())
        return true;
    return lut.batch_key != live_batch_key;
}

std::optional<CIELab> measured_lab_for(const SwatchLut &lut, const std::string &recipe_key)
{
    auto it = lut.index.find(recipe_key);
    if (it == lut.index.end() || it->second >= lut.entries.size())
        return std::nullopt;
    return lut.entries[it->second].lab;
}

double candidate_distance(const CIELab      &target,
                          const CIELab      &predicted,
                          const SwatchLut   *lut,
                          const std::string &recipe_key,
                          const std::string &live_batch_key,
                          bool              *used_measured)
{
    if (used_measured)
        *used_measured = false;
    if (lut != nullptr && !lut_is_stale(*lut, live_batch_key)) {
        if (auto lab = measured_lab_for(*lut, recipe_key)) {
            if (used_measured)
                *used_measured = true;
            return delta_e00(target, *lab);
        }
    }
    return delta_e00(target, predicted);
}

} // namespace Slic3r

// AI Helmets launcher plugin: the "AI Helmets" page in Swofty Launcher's sidebar. A Teams tab lists each
// team with its two drivers (the loaded career's line-up while the game runs, the original one otherwise);
// clicking a team opens a panel with the helmet its newcomers wear and each driver's helmet there, with an
// import button per driver. An Imported helmets tab manages the imported .erp files.
//
// Speaks the launcher plugin protocol (newline-delimited JSON on stdin/stdout, launcher API 1). Keeps
// config.json and imported helmets in the addon's data_dir, and on build hands them to the game folder
// (swofty/addons/sedz.ai-helmets/) with file.put ops, where the engine plugin reads them.

#include <swofty/json.hpp>

#include "convert.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace fs = std::filesystem;
namespace J = swofty::json;

namespace {

// must match swofty.json's id and the engine plugin's kAddon: the plugin reads its config from
// swofty/addons/<id>/ and registers imported helmets as <id>/<key>
constexpr const char *kAddon = "sedz.ai-helmets";
constexpr const char *kMyTeam = "myteam";  // the team key MyTeam picks are saved under (the plugin's too)
constexpr const char *kOldAddon = "custom.ai-helmets"; // the id up to 0.6.3
constexpr const char *kGameDir = "swofty/addons/sedz.ai-helmets/";

struct TeamDef {
    const char *key, *name;
};
constexpr TeamDef kTeams[] = {
    {"mclaren_26", "McLaren"},          {"mercedes_26", "Mercedes"},
    {"redbull_26", "Red Bull Racing"},  {"ferrari_26", "Ferrari"},
    {"williams_26", "Williams"},        {"tororosso_26", "Racing Bulls"},
    {"forceindia_26", "Aston Martin"},  {"haas_26", "Haas"},
    {"sauber_26", "Audi"},              {"renault_26", "Alpine"},
    {"gm_cadillac_26", "Cadillac"},     {"mclaren", "McLaren (2025)"},
    {"mercedes", "Mercedes (2025)"},    {"redbull", "Red Bull Racing (2025)"},
    {"ferrari", "Ferrari (2025)"},      {"williams", "Williams (2025)"},
    {"tororosso", "Racing Bulls (2025)"}, {"forceindia", "Aston Martin (2025)"},
    {"haas", "Haas (2025)"},            {"sauber", "Kick Sauber (2025)"},
    {"renault", "Alpine (2025)"},
};

// The game's helmet designs (customisation.custom_avatar.helmets rows the player can pick), by label.
// A team's newcomers wear one of these unless you pick another; tier01_livery76 ("Simplicity") by default.
struct Design {
    const char *label, *name;
};
struct GridHelmet {
    const char *driver, *helmet;  // a driver and his newest helmet row (helmet rows are <year>-<team>_<driver>)
};
constexpr const char *kDefaultBase = "game/tier01_livery76";
constexpr const char *kDefaultShell = "game/2026-ferrari_lewis_hamilton";
constexpr Design kDesigns[] = {
    {"tier04_livery100", "Apex Hunter"},
    {"tier01_livery80", "Aquamarine"},
    {"tier02_livery102", "Blacktronic"},
    {"tier04_livery99", "Blade"},
    {"tier05_livery74", "Clockwork"},
    {"tier01_livery78", "Constellation"},
    {"tier05_livery66", "Crystal Burst"},
    {"tier05_livery23", "Cyborg"},
    {"tier03_livery106", "Digital"},
    {"tier05_livery86", "Doomsday"},
    {"tier03_livery105", "Electric Lime"},
    {"tier04_livery105", "Fighting Forward"},
    {"tier04_livery101", "Fractured"},
    {"tier01_livery86", "Fresh Strike"},
    {"tier05_livery16", "Fusion"},
    {"tier04_livery94", "Galactic"},
    {"tier02_livery84", "Geo-Safari"},
    {"tier03_livery108", "Glitterspark"},
    {"tier02_livery01", "Heatsink"},
    {"tier05_livery88", "Invincible"},
    {"tier03_livery107", "Maestro"},
    {"tier03_livery96", "Maple Forest"},
    {"tier04_livery103", "Neo-Retro"},
    {"tier05_livery87", "Neon Fighter"},
    {"tier04_livery106", "Neon Verde"},
    {"tier01_livery85", "Old Glory"},
    {"tier04_livery06", "Primal Nature"},
    {"tier02_livery94", "Raging Thunder"},
    {"tier03_livery101", "Redwing"},
    {"tier02_livery95", "Retrosport"},
    {"tier02_livery104", "Riviera"},
    {"tier03_livery88", "Robo-Strike"},
    {"tier05_livery73", "Samurai"},
    {"tier01_livery76", "Simplicity"},
    {"tier04_livery86", "Sketchwork"},
    {"tier04_livery20", "Skyscraper"},
    {"tier04_livery104", "Speed Blade"},
    {"tier01_livery79", "Speeding Flag"},
    {"tier01_livery75", "Stripe Attack"},
    {"tier02_livery103", "Sunrise"},
    {"tier02_livery82", "Sunset"},
    {"tier01_livery52", "Teal"},
    {"unassigned_livery18", "The Story Is Yours"},
    {"unassigned_livery17", "Violet Dawn"},
    {"tier01_livery77", "Voyager Rose"},
    {"tier01_livery88", "Waveform"},
    {"tier03_livery99", "Wild Stripes"},
};
constexpr GridHelmet kGridHelmets[] = {
    {"game/oscar_piastri", "game/2026-mclaren_oscar_piastri"},
    {"game/lando_norris", "game/2026-mclaren_lando_norris"},
    {"game/george_russell", "game/2026-mercedes_george_russell"},
    {"game/andrea-kimi_antonelli", "game/2026-mercedes_andrea-kimi_antonelli"},
    {"game/max_verstappen", "game/2026-redbull_max_verstappen"},
    {"game/isack_hadjar", "game/2026-redbull_isack_hadjar"},
    {"game/charles_leclerc", "game/2026-ferrari_charles_leclerc"},
    {"game/lewis_hamilton", "game/2026-ferrari_lewis_hamilton"},
    {"game/alex_albon", "game/2026-williams_alex_albon"},
    {"game/carlos_sainz-jr", "game/2026-williams_carlos_sainz-jr"},
    {"game/arvid_lindblad", "game/2026-tororosso_arvid_lindblad"},
    {"game/liam_lawson", "game/2026-tororosso_liam_lawson"},
    {"game/lance_stroll", "game/2026-forceindia_lance_stroll"},
    {"game/fernando_alonso", "game/2026-forceindia_fernando_alonso"},
    {"game/esteban_ocon", "game/2026-haas_esteban_ocon"},
    {"game/oliver_bearman", "game/2026-haas_oliver_bearman"},
    {"game/nico_hulkenberg", "game/2026-sauber_nico_hulkenberg"},
    {"game/gabriel_bortoleto", "game/2026-sauber_gabriel_bortoleto"},
    {"game/pierre_gasly", "game/2026-renault_pierre_gasly"},
    {"game/franco_colapinto", "game/2026-renault_franco_colapinto"},
    {"game/sergio_perez", "game/2026-cadillac_sergio_perez"},
    {"game/valtteri_bottas", "game/2026-cadillac_valtteri_bottas"},
    {"game/yuki_tsunoda", "game/2025-tororosso_yuki_tsunoda"},
};

// ---------------------------------------------------------------------------------------------------------
// I/O

void send(const J::Value &msg)
{
    std::string line = J::dump(msg);
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

bool read_line(std::string &line)
{
    if (!std::getline(std::cin, line))
        return false;
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    return true;
}

int64_t g_next_call = 1000;

// A host call: the launcher answers before anything else.
J::Value call(const char *method, J::Value params = nullptr)
{
    J::Value msg = J::Value::object();
    msg["id"] = g_next_call++;
    msg["method"] = method;
    msg["params"] = std::move(params);
    send(msg);
    std::string line;
    if (!read_line(line))
        std::exit(0);
    J::Value reply = J::parse(line);
    return reply.contains("error") ? J::Value() : reply["result"];
}

void log(const std::string &s) { std::fprintf(stderr, "%s\n", s.c_str()); }

std::string read_file(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool write_file(const fs::path &p, const std::string &s)
{
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f)
            return false;
        f << s;
    }
    fs::rename(tmp, p, ec);
    return !ec;
}

fs::path from_utf8(std::string_view s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string to_utf8(const fs::path &p)
{
    auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

// ---------------------------------------------------------------------------------------------------------
// Names

std::string title_case(std::string s)
{
    bool up = true;
    for (char &c : s) {
        if (c == '_')
            c = ' ';
        if (up && std::isalpha(static_cast<unsigned char>(c)))
            c = char(std::toupper(static_cast<unsigned char>(c)));
        up = c == ' ' || c == '-';
    }
    return s;
}

std::string after_slash(const std::string &ref)
{
    auto i = ref.find('/');
    return i == std::string::npos ? ref : ref.substr(i + 1);
}

std::string driver_name(const std::string &ref) { return title_case(after_slash(ref)); }

std::string team_name(const std::string &key)
{
    if (key == kMyTeam)
        return "your MyTeam team";
    for (auto &t : kTeams)
        if (key == t.key)
            return t.name;
    return title_case(key);
}

bool is_design(const std::string &ref)
{
    std::string l = after_slash(ref);
    return ref.rfind("game/", 0) == 0 && (l.rfind("tier0", 0) == 0 || l.rfind("unassigned_", 0) == 0);
}

// "game/2026-ferrari_lewis_hamilton" -> {"2026", "ferrari", "lewis_hamilton"}; false for other labels
bool dated_helmet(const std::string &ref, std::string &year, std::string &team, std::string &driver)
{
    std::string l = after_slash(ref);
    if (l.size() < 7 || !std::isdigit(static_cast<unsigned char>(l[0])) || l[4] != '-')
        return false;
    auto us = l.find('_', 5);
    if (us == std::string::npos)
        return false;
    year = l.substr(0, 4);
    team = l.substr(5, us - 5);
    driver = l.substr(us + 1);
    return true;
}

std::string game_helmet_name(const std::string &ref)
{
    std::string l = after_slash(ref);
    for (auto &d : kDesigns)
        if (l == d.label)
            return d.name;
    if (is_design(ref))
        return "Design " + l;
    std::string year, team, driver;
    if (dated_helmet(ref, year, team, driver))
        return title_case(driver) + " (" + year + " " + title_case(team) + ")";
    return title_case(l);
}

// ---------------------------------------------------------------------------------------------------------
// State

struct Plugin {
    fs::path data_dir, cache_dir;
    std::string launcher;
    J::Value config;

    fs::path config_path() const { return data_dir / "config.json"; }
    fs::path helmets_dir() const { return data_dir / "helmets"; }

    void load()
    {
        config = J::Value::object();
        std::string text = read_file(config_path());
        // helmets picked under the old id are the same helmets under the new one
        bool migrated = false;
        for (std::string from = "\"" + std::string(kOldAddon) + "/", to = "\"" + std::string(kAddon) + "/";;) {
            size_t at = text.find(from);
            if (at == std::string::npos)
                break;
            text.replace(at, from.size(), to);
            migrated = true;
        }
        if (migrated)
            write_file(config_path(), text);
        if (!text.empty()) {
            J::Error err;
            J::Value v = J::parse(text, &err);
            if (err.ok() && v.is_object())
                config = std::move(v);
            else
                log("config.json is not valid JSON: " + err.message);
        }
        if (!config["version"].is_int())
            config["version"] = 1;
        if (!config["teams"].is_object())
            config["teams"] = J::Value::object();
        if (!config["drivers"].is_object())
            config["drivers"] = J::Value::object();
        if (!config["custom"].is_array())
            config["custom"] = J::Value::array();
        if (!config["picks"].is_object())
            config["picks"] = J::Value::object();
        load_addon_drivers();
    }

    // Drivers Driver Manager adds: database rows named dm_<stat id>, each with a look holding the helmet picked
    // for him there. The plugin can't tell which look is his, so their helmets go to it in config.json
    // ("addon_drivers"); their names are shown on the page.
    void load_addon_drivers()
    {
        J::Value map = J::Value::object();
        addon_names.clear();
        J::Value dm = J::parse(read_file(data_dir.parent_path() / "swofty.driver-manager" / "drivers.json"));
        if (auto *a = dm["drivers"].if_array())
            for (auto &d : *a) {
                if (!d["stat_id"].is_int())
                    continue;
                std::string ref = "game/dm_" + std::to_string(d["stat_id"].as_int());
                std::string helmet(d["helmet"].as_string());
                if (!helmet.empty())
                    map[ref] = "game/" + helmet;
                std::string first(d["first"].as_string()), last(d["last"].as_string());
                if (!last.empty())
                    addon_names[ref] = {first, last};
            }
        config["addon_drivers"] = std::move(map);
    }
    std::map<std::string, std::pair<std::string, std::string>> addon_names;  // driver ref -> first, last

    // data_dir's copy goes to the game at the next Play; while the game folder has the addon (the game is
    // running, or ran through the launcher), the copy there is updated too, and the plugin re-reads it at the
    // next Team HQ or session start.
    // live = false (the build): data_dir's copy only. The build hands config.json to the game folder with
    // file.put; a write there of our own during the build left the folder looking like an unfinished session to
    // the launcher (0.8.1), which then refused to start the game.
    bool save(bool live = true)
    {
        std::string text = J::dump(config, 2);
        bool ok = write_file(config_path(), text);
        if (!live)
            return ok;
        J::Value dir = call("addon_game_dir");
        std::error_code ec;
        if (dir.is_string() && fs::is_directory(from_utf8(dir.as_string()), ec))
            write_file(from_utf8(dir.as_string()) / "config.json", text);
        return ok;
    }

    // The catalogue the engine plugin wrote at the last Team HQ: newest of the live game folder copy and
    // the copy the launcher keeps between sessions.
    J::Value catalogue() { return stored("catalogue.json"); }
    // The last MyTeam career's team and drivers (written by the plugin in MyTeam careers only).
    J::Value myteam() { return stored("myteam.json"); }

    // What the page shows depends on: whether the game runs, and the plugin's catalogue.json and myteam.json
    // (rewritten at career load, Team HQ, session start and career unload). A fingerprint of those, taken when
    // the page is drawn and on each tick, tells when to redraw it.
    std::string page_signature()
    {
        std::string sig = call("game_status")["running"].as_bool(false) ? "R" : "-";
        J::Value dir = call("addon_game_dir");
        std::vector<fs::path> dirs{data_dir.parent_path().parent_path() / "addon-storage" / kAddon};
        if (dir.is_string())
            dirs.push_back(from_utf8(dir.as_string()));
        for (auto &d : dirs)
            for (const char *name : {"catalogue.json", "myteam.json"}) {
                std::error_code ec;
                auto t = fs::last_write_time(d / name, ec);
                auto n = ec ? 0 : fs::file_size(d / name, ec);
                sig += ec ? std::string("|x") : "|" + std::to_string(t.time_since_epoch().count()) + ":" + std::to_string(n);
            }
        return sig;
    }

    J::Value stored(const char *name)
    {
        std::vector<fs::path> candidates;
        J::Value dir = call("addon_game_dir");
        if (dir.is_string())
            candidates.push_back(from_utf8(dir.as_string()) / name);
        candidates.push_back(data_dir.parent_path().parent_path() / "addon-storage" / kAddon / name);
        fs::path best;
        fs::file_time_type best_time{};
        for (auto &p : candidates) {
            std::error_code ec;
            if (!fs::is_regular_file(p, ec))
                continue;
            auto t = fs::last_write_time(p, ec);
            if (!ec && (best.empty() || t > best_time)) {
                best = p;
                best_time = t;
            }
        }
        if (best.empty())
            return J::Value();
        J::Value v = J::parse(read_file(best));
        return v.is_object() ? v : J::Value();
    }

    std::vector<J::Value> custom() const
    {
        std::vector<J::Value> out;
        if (auto *a = config["custom"].if_array())
            for (auto &h : *a)
                out.push_back(h);
        return out;
    }

    // Helmet Exchange's helmets are database rows "hx_<n>"; their names are in its data folder, next to ours
    // (addon-data/giggles.helmet-exchange/helmets/<n>/meta.json).
    std::map<std::string, std::string> hx_names;
    bool hx_loaded = false;

    const std::map<std::string, std::string> &hx()
    {
        if (hx_loaded)
            return hx_names;
        hx_loaded = true;
        std::error_code ec;
        fs::path dir = data_dir.parent_path() / "giggles.helmet-exchange" / "helmets";
        if (!fs::is_directory(dir, ec))
            return hx_names;
        for (auto &e : fs::directory_iterator(dir, ec)) {
            J::Value m = J::parse(read_file(e.path() / "meta.json"));
            std::string n = to_utf8(e.path().filename());
            if (m.is_object() && m["name"].is_string() && !m["name"].as_string().empty())
                hx_names["game/hx_" + n] = std::string(m["name"].as_string());
        }
        return hx_names;
    }

    std::string helmet_label(const std::string &ref)
    {
        if (ref.rfind("game/hx_", 0) == 0) {
            auto it = hx().find(ref);
            return (it != hx().end() ? it->second : "Helmet " + ref.substr(8)) + " (HX)";
        }
        std::string own = std::string(kAddon) + "/";
        if (ref.rfind(own, 0) == 0) {
            std::string key = ref.substr(own.size());
            for (auto &h : custom())
                if (h["key"].as_string() == key)
                    return std::string(h["name"].as_string(key));
            return key;
        }
        if (ref.rfind("giggles.helmet-exchange/", 0) == 0)
            return title_case(after_slash(ref)) + " (Helmet Exchange)";
        if (ref.rfind("game/", 0) == 0)
            return game_helmet_name(ref);
        return ref;
    }
};

Plugin g;

// ---------------------------------------------------------------------------------------------------------
// Line-ups: the teams and their drivers. While the game runs and the plugin has seen the loaded career's
// Team HQ, the career's line-up (catalogue.json, "live"); otherwise each driver's original team.

struct Lineup {
    bool live = false;
    std::string career;
    std::vector<std::string> teams;                          // in display order
    std::map<std::string, std::vector<std::string>> drivers; // team key -> driver refs
    std::map<std::string, std::string> home;                 // driver ref -> his original team key
    std::set<std::string> player;                            // the driver the player races as (last career)
    std::set<std::string> myteam;                            // a MyTeam career's own drivers (live only)
};

std::string team_of_helmet(const std::string &code, bool s26)
{
    std::string key = code == "cadillac" ? "gm_cadillac" : code;
    return s26 ? key + "_26" : key;
}

Lineup lineup(const J::Value &cat, bool running)
{
    Lineup l;
    l.career = std::string(cat["career"].as_string());
    l.live = running && cat["live"].as_bool(false);
    std::map<std::string, std::vector<std::string>> by;
    // a MyTeam career's own drivers are on the MyTeam tab, not among the teams. They are the plugin's list, not
    // everyone with the MyTeam's team key: in a 2025-season MyTeam that key is a real team's too ("redbull"),
    // whose own drivers stay in the table.
    std::string myteam(cat["myteam"]["team"].as_string());
    std::set<std::string> mine;
    if (auto *a = cat["myteam"]["drivers"].if_array())
        for (auto &d : *a)
            if (d["id"].is_string())
                mine.insert(std::string(d["id"].as_string()));
    if (auto *a = cat["drivers"].if_array())
        for (auto &d : *a) {
            if (!d["seated"].as_bool(false) || !d["id"].is_string())
                continue;
            std::string id(d["id"].as_string()), team(d["team"].as_string()), orig(d["original"].as_string());
            if (!myteam.empty() && team == myteam && mine.count(id)) {
                l.myteam.insert(id);
                continue;
            }
            if (d["player"].as_bool(false))
                l.player.insert(id);
            std::string at = l.live ? team : orig;  // original "": he joined from the free agents
            if (at.empty())
                continue;
            by[at].push_back(id);
            l.home[id] = orig;
        }
    if (!l.live) {
        // the game is closed (or hasn't reached this run's first Team HQ): the stock line-up of the season the
        // last career used, which the plugin works out from the drivers' own helmet rows
        bool s26 = by.empty();
        for (auto &[t, v] : by)
            s26 |= t.size() > 3 && t.compare(t.size() - 3, 3, "_26") == 0;
        if (auto *o = cat["stock"][s26 ? "2026" : "2025"].if_object()) {
            by.clear();
            l.home.clear();
            for (auto &[team, ds] : *o)
                if (auto *a = ds.if_array())
                    for (auto &d : *a) {
                        by[team].push_back(std::string(d.as_string()));
                        l.home[std::string(d.as_string())] = team;
                    }
        }
    }
    if (by.empty()) {  // no catalogue yet: the stock 2026 grid from the drivers' 2026 helmets
        l.live = false;
        for (auto &gh : kGridHelmets) {
            std::string year, code, who;
            if (dated_helmet(gh.helmet, year, code, who) && year == "2026") {
                std::string team = team_of_helmet(code, true);
                by[team].push_back(gh.driver);
                l.home[gh.driver] = team;
            }
        }
    }
    bool s26 = false;
    for (auto &[t, v] : by)
        s26 |= t.size() > 3 && t.compare(t.size() - 3, 3, "_26") == 0;
    for (auto &t : kTeams) {
        std::string k(t.key);
        bool k26 = k.size() > 3 && k.compare(k.size() - 3, 3, "_26") == 0;
        if (k26 == s26 && std::find(l.teams.begin(), l.teams.end(), k) == l.teams.end())
            l.teams.push_back(k);
    }
    for (auto &[t, v] : by)
        if (std::find(l.teams.begin(), l.teams.end(), t) == l.teams.end())
            l.teams.push_back(t);
    l.drivers = std::move(by);
    return l;
}

// "game/carlos_sainz-jr" -> "Carlos " + "SAINZ-JR"
std::pair<std::string, std::string> name_parts(const std::string &ref)
{
    if (auto it = g.addon_names.find(ref); it != g.addon_names.end()) {
        std::string last = it->second.second;
        for (char &c : last)
            c = char(std::toupper(static_cast<unsigned char>(c)));
        return {it->second.first.empty() ? std::string() : it->second.first + " ", last};
    }
    std::string n = title_case(after_slash(ref));
    auto sp = n.find(' ');
    std::string first = sp == std::string::npos ? "" : n.substr(0, sp + 1), last = sp == std::string::npos ? n : n.substr(sp + 1);
    for (char &c : last)
        c = char(std::toupper(static_cast<unsigned char>(c)));
    return {first, last};
}

std::string driver_label(const std::string &ref)
{
    auto [f, l] = name_parts(ref);
    return f + l;
}

// ---------------------------------------------------------------------------------------------------------
// The page

J::Value option(const std::string &value, const std::string &label)
{
    return J::Value::object({{"value", value}, {"label", label}});
}
J::Value heading(const std::string &text) { return J::Value::object({{"heading", text}}); }

J::Value text_block(const std::string &text, const char *tone = "neutral")
{
    // the launcher's Block::Text requires both fields
    return J::Value::object({{"text", J::Value::object({{"text", text}, {"tone", tone}})}});
}

J::Value card(const std::string &title, J::Value blocks)
{
    return J::Value::object({{"card", J::Value::object({{"title", title}, {"blocks", std::move(blocks)}})}});
}

J::Value select_form(const std::string &form, const std::string &label, const J::Value &value, J::Value options,
                     const std::string &help = {})
{
    J::Value sel = J::Value::object({{"id", "helmet"}, {"label", label}, {"value", value}, {"search", true},
                                     {"options", std::move(options)}});
    if (!help.empty())
        sel["help"] = help;
    return J::Value::object({{"form", J::Value::object({{"id", form}, {"live", true},
                                                        {"fields", J::Value::array({J::Value::object({{"select", std::move(sel)}})})}})}});
}

J::Value import_button(const std::string &id, const std::string &label)
{
    return J::Value::object({{"id", id}, {"label", label}, {"icon", "upload"},
                             {"kind", J::Value::object({{"pick", J::Value::object({{"title", "Import a helmet .erp"},
                                                                                   {"extensions", J::Value::array({"erp"})},
                                                                                   {"folders", false},
                                                                                   {"multiple", false}})}})}});
}

std::string tab = "teams";
std::string g_fa_editing;  // the free agent whose panel shows the helmet list

std::string default_helmet(const J::Value &cfg)
{
    for (const char *k : {"default_helmet", "base_design"})
        if (cfg[k].is_string() && !cfg[k].as_string().empty() && cfg[k].as_string().find("/team_") == std::string::npos)
            return std::string(cfg[k].as_string());
    return kDefaultBase;
}

std::string team_helmet(const J::Value &cfg, const std::string &team)
{
    const J::Value &h = cfg["teams"][team]["helmet"];
    return h.is_string() && !h.as_string().empty() && h.as_string().find("/team_") == std::string::npos
               ? std::string(h.as_string())
               : std::string();
}

std::string helmet_file_kind(const fs::path &path);
std::string kind_problem(const std::string &kind);

std::string g_shown;  // page_signature() when the page was last drawn

J::Value view()
{
    g_shown = g.page_signature();
    g.load();
    const J::Value &cfg = g.config;      // const: reading must not add keys
    const J::Value cat = g.catalogue();  // const: indexing a null must not turn it into an object
    auto custom = g.custom();
    J::Value status = call("game_status");
    bool running = status["running"].as_bool(false);
    Lineup lu = lineup(cat, running);

    // helmet choices, grouped
    std::vector<std::string> game_helmets, addon_helmets, designs, hx_helmets;
    if (auto *a = cat["helmets"].if_array())
        for (auto &h : *a) {
            std::string id(h.as_string());
            if (id.rfind("game/hx_", 0) == 0)
                hx_helmets.push_back(id);
            else if (is_design(id))
                designs.push_back(id);
            else if (id.rfind("game/", 0) == 0)
                game_helmets.push_back(id);
            else if (id.rfind(std::string(kAddon) + "/", 0) != 0)
                addon_helmets.push_back(id);
        }
    if (game_helmets.empty())
        for (auto &d : kGridHelmets)
            game_helmets.push_back(d.helmet);
    if (designs.empty())
        for (auto &d : kDesigns)
            designs.push_back(std::string("game/") + d.label);
    auto by_name = [](std::vector<std::string> &v) {
        std::sort(v.begin(), v.end(),
                  [](const std::string &a, const std::string &b) { return g.helmet_label(a) < g.helmet_label(b); });
    };
    by_name(game_helmets);
    by_name(designs);
    by_name(addon_helmets);
    by_name(hx_helmets);
    J::Value helmets = J::Value::array();
    if (!custom.empty()) {
        helmets.push_back(heading("Your imported helmets"));
        for (auto &h : custom)
            helmets.push_back(option(std::string(kAddon) + "/" + std::string(h["key"].as_string()),
                                     std::string(h["name"].as_string(h["key"].as_string()))));
    }
    if (!hx_helmets.empty()) {
        helmets.push_back(heading("Helmet Exchange"));
        for (auto &h : hx_helmets)
            helmets.push_back(option(h, g.helmet_label(h)));
    }
    helmets.push_back(heading("Stock designs"));
    for (auto &h : designs)
        helmets.push_back(option(h, g.helmet_label(h)));
    helmets.push_back(heading("Drivers' helmets"));
    for (auto &h : game_helmets)
        helmets.push_back(option(h, g.helmet_label(h)));
    if (!addon_helmets.empty()) {
        helmets.push_back(heading("Other addons' helmets"));
        for (auto &h : addon_helmets)
            helmets.push_back(option(h, g.helmet_label(h)));
    }
    auto with = [&](J::Value first) {
        J::Value o = std::move(first);
        for (auto &x : *helmets.if_array())
            o.push_back(x);
        return o;
    };
    std::string dflt = default_helmet(cfg);

    J::Value blocks = J::Value::array();
    blocks.push_back(J::Value::object({{"tabs", J::Value::object({{"id", "page"},
                                                                 {"items", J::Value::array({
                                                                     J::Value::object({{"id", "teams"}, {"label", "Teams"}, {"icon", "users"}}),
                                                                     J::Value::object({{"id", "free"}, {"label", "Free agents"}, {"icon", "user"}}),
                                                                     J::Value::object({{"id", "myteam"}, {"label", "MyTeam"}, {"icon", "trophy"}}),
                                                                     J::Value::object({{"id", "helmets"}, {"label", "Imported helmets"}, {"icon", "layers"},
                                                                                       {"badge", std::to_string(custom.size())}}),
                                                                 })},
                                                                 {"active", tab}})}}));
    size_t picks = 0;
    if (auto *o = cfg["picks"].if_object())
        picks = o->size();

    if (tab == "helmets") {
        // ---- imported helmets, and the default newcomer helmet
        blocks.push_back(card("Default helmet for newcomers",
                              J::Value::array({select_form("default", "A driver who joins a team wears", J::Value(dflt),
                                                           with(J::Value::array()),
                                                           "Used by every team that has no helmet of its own set on the Teams tab.")})));
        J::Value own = J::Value::array();
        if (custom.empty())
            own.push_back(text_block("No imported helmets yet. Use Import helmet… at the top, or an import button in a "
                                     "team's panel on the Teams tab: helmet archives, career helmet mods and packs of "
                                     "either (.zip, .7z, .rar) are all made ready to use as they come in.",
                                     "muted"));
        else {
            J::Value shells = J::Value::array();
            for (auto &d : game_helmets)
                shells.push_back(option(d, g.helmet_label(d)));
            J::Value rows = J::Value::array();
            for (auto &h : custom) {
                std::string key(h["key"].as_string()), name(h["name"].as_string(key)), file(h["file"].as_string());
                bool converted = h["token"].is_string() && !h["token"].as_string().empty();
                bool pending = !converted && helmet_file_kind(g.helmets_dir() / file) != "ok";
                std::string type = converted ? "Helmet mod, converted"
                                   : pending ? "Converted at the next Play"
                                             : "Helmet archive";
                J::Value detail = J::Value::array();
                if (converted)
                    detail.push_back(text_block("Converted onto " +
                                                    g.helmet_label("game/" + std::string(h["shell"].as_string(convert::kShellToken))) +
                                                    "'s helmet, the one it was made for (or Lewis Hamilton's 2026 helmet when "
                                                    "that couldn't be told). Nothing to set.",
                                                "muted"));
                else if (pending)
                    detail.push_back(text_block("Imported by an earlier version as it was. It is converted into a helmet "
                                                "of its own the next time you press Play.",
                                                "muted"));
                else
                    // a helmet archive's resources are named after the driver it was made for: it has to be built
                    // on that driver's helmet, or the game draws it white
                    detail.push_back(J::Value::object(
                        {{"form", J::Value::object(
                                      {{"id", "custom"},
                                       {"live", true},
                                       {"fields", J::Value::array({J::Value::object(
                                                      {{"select", J::Value::object({{"id", "from:" + key},
                                                                                    {"label", "Made for"},
                                                                                    {"value", h["from"]},
                                                                                    {"search", true},
                                                                                    {"options", shells},
                                                                                    {"help", "The driver whose helmet file this was "
                                                                                             "made from. Matched from the file name "
                                                                                             "when it is <first>_<last>_helmet.erp."}})}})})}})}}));
                detail.push_back(J::Value::object(
                    {{"buttons", J::Value::array({J::Value::object(
                                     {{"id", "remove:" + key},
                                      {"label", "Remove"},
                                      {"style", "danger"},
                                      {"kind", J::Value::object({{"confirm", J::Value::object({{"title", "Remove " + name + "?"},
                                                                                               {"text", "Drivers and teams using it go back to their default helmet."}})}})}})})}}));
                rows.push_back(J::Value::object(
                    {{"id", key},
                     {"title", name},
                     {"subtitle", file},
                     {"cells", J::Value::array({J::Value::object({{"text", name}}),
                                                J::Value::object({{"text", type}, {"tone", pending ? "warn" : "muted"}}),
                                                J::Value::object({{"text", file}, {"tone", "muted"}})})},
                     {"detail", std::move(detail)}}));
            }
            own.push_back(J::Value::object(
                {{"table", J::Value::object({{"id", "imported"},
                                             {"columns", J::Value::array({
                                                 J::Value::object({{"key", "name"}, {"title", "Helmet"}, {"sort", true}}),
                                                 J::Value::object({{"key", "type"}, {"title", "Type"}}),
                                                 J::Value::object({{"key", "file"}, {"title", "File"}}),
                                             })},
                                             {"rows", std::move(rows)},
                                             {"sort", J::Value::object({{"column", "name"}, {"desc", false}})},
                                             {"search", J::Value::object({{"placeholder", "Search helmets"}})}})}}));
            own.push_back(text_block("An imported helmet is added to the game the next time you press Play.", "muted"));
        }
        blocks.push_back(card("Imported helmets", std::move(own)));
    } else if (tab == "myteam") {
        // ---- the player's own team in a MyTeam career: both its drivers, the one you drive included
        const J::Value mt = g.myteam();
        std::string th = team_helmet(cfg, kMyTeam);
        J::Value team = J::Value::array();
        team.push_back(text_block("Your MyTeam drivers keep their own helmets unless you set one here. The helmet below "
                                  "is worn by every driver you sign who has no helmet of their own set further down.",
                                  "muted"));
        team.push_back(select_form("team:" + std::string(kMyTeam), "Every MyTeam driver wears", J::Value(th),
                                   with(J::Value::array({option("", "Their own helmet")}))));
        team.push_back(J::Value::object({{"buttons", J::Value::array({import_button(
                                              "import-team:" + std::string(kMyTeam), "Import helmet for your MyTeam drivers…")})}}));
        blocks.push_back(card("Your team", std::move(team)));
        // the drivers with a race seat: a MyTeam team's entry list also holds the game's placeholder drivers
        // (Arron Barnes, Noah Visser) with no car, who never race. 0.7.0 listed them as drivers 1 and 2.
        std::vector<std::pair<std::string, bool>> ds;  // driver, driven by the player
        for (bool seated_only : {true, false}) {
            if (auto *a = mt["drivers"].if_array())
                for (auto &d : *a)
                    if (d["id"].is_string() && !d["id"].as_string().empty() && (!seated_only || d["seated"].as_bool(false)))
                        ds.push_back({std::string(d["id"].as_string()), d["player"].as_bool(false)});
            if (!ds.empty())
                break;
        }
        if (ds.empty())
            blocks.push_back(text_block("Your MyTeam drivers show here once you have loaded a MyTeam career with the game "
                                        "running through the launcher.",
                                        "muted"));
        else {
            std::string mc(mt["career"].as_string());
            bool now = running && !mc.empty() && cat["myteam"]["career"].as_string() == mc;
            blocks.push_back(text_block(now ? "Your drivers in the loaded MyTeam career."
                                            : "Your drivers when you last played a MyTeam career.",
                                        "muted"));
            for (size_t i = 0; i < ds.size(); i++) {
                const std::string &d = ds[i].first;
                std::string key = d + "|" + kMyTeam;
                const J::Value &pick = cfg["picks"][key];
                J::Value opts = J::Value::array(
                    {option("", th.empty() ? std::string("Their own helmet") : "Team helmet: " + g.helmet_label(th))});
                if (!th.empty())
                    opts.push_back(option("own", "Their own helmet"));
                J::Value inner = J::Value::array();
                if (ds[i].second)
                    inner.push_back(text_block("The driver you were driving.", "muted"));
                inner.push_back(select_form("pick:" + key, "Helmet at your team", pick.is_string() ? pick : J::Value(""),
                                            with(std::move(opts))));
                inner.push_back(J::Value::object({{"buttons", J::Value::array({import_button(
                                                      "import:" + key, "Import helmet for " + name_parts(d).second + "…")})}}));
                blocks.push_back(card("Driver " + std::to_string(i + 1) + " · " + driver_label(d) +
                                          (ds[i].second ? " (You)" : ""),
                                      std::move(inner)));
            }
        }
    } else if (tab == "free") {
        // ---- every driver in the database who isn't in a team now: a helmet he wears wherever he races
        std::set<std::string> in_team;
        for (auto &[t, ds] : lu.drivers)
            in_team.insert(ds.begin(), ds.end());
        if (lu.live)
            in_team.insert(lu.myteam.begin(), lu.myteam.end());
        J::Value rows = J::Value::array();
        if (auto *a = cat["all_drivers"].if_array())
            for (auto &x : *a) {
                std::string d(x.as_string());
                if (d.empty() || in_team.count(d) || d == "game/default_driver")
                    continue;
                const J::Value &pick = cfg["drivers"][d];
                std::string p = pick.is_string() ? std::string(pick.as_string()) : std::string();
                if (p == "auto")
                    p.clear();
                auto [f, l] = name_parts(d);
                std::string shown = p.empty() ? std::string("Team default") : p == "own" ? std::string("Their own helmet") : g.helmet_label(p);
                J::Value detail = J::Value::array();
                detail.push_back(text_block("Not in a team now. The helmet below is worn at whichever team they join, "
                                            "unless a pick on the Teams tab sets one for them there.",
                                            "muted"));
                J::Value buttons = J::Value::array();
                // the full helmet list only in the panel being edited: in every row it made this tab 4.7 MB (0.8.3)
                if (d == g_fa_editing) {
                    J::Value opts = J::Value::array({option("", "Team default (the team's newcomer helmet)"), option("own", "Their own helmet")});
                    detail.push_back(select_form("fa:" + d, "Helmet when they join a team", J::Value(p), with(std::move(opts))));
                } else {
                    detail.push_back(J::Value::object({{"facts", J::Value::array({J::Value::object(
                                                                      {{"label", "Helmet when they join a team"}, {"value", shown}})})}}));
                    buttons.push_back(J::Value::object({{"id", "fa-edit:" + d}, {"label", "Change helmet…"}, {"icon", "pencil"}}));
                }
                buttons.push_back(import_button("import-fa:" + d, "Import helmet for " + l + "…"));
                detail.push_back(J::Value::object({{"buttons", std::move(buttons)}}));
                rows.push_back(J::Value::object(
                    {{"id", d},
                     {"title", f + l},
                     {"subtitle", d},
                     {"cells", J::Value::array({J::Value::object({{"text", f + l},
                                                                  {"spans", J::Value::array({J::Value::object({{"text", f}}),
                                                                                             J::Value::object({{"text", l}, {"bold", true}})})}}),
                                                J::Value::object({{"text", shown}, {"tone", p.empty() ? "muted" : "neutral"}}),
                                                J::Value::object({{"text", p.empty() ? "Default" : "Edited"},
                                                                  {"tone", p.empty() ? "muted" : "accent"},
                                                                  {"pill", true}})})},
                     {"detail", std::move(detail)}}));
            }
        if (rows.if_array()->empty())
            blocks.push_back(text_block("The free agents are listed once the game has run with this addon and you have "
                                        "visited Team HQ in a career.",
                                        "muted"));
        else
            blocks.push_back(J::Value::object(
                {{"table", J::Value::object({{"id", "free"},
                                             {"columns", J::Value::array({
                                                 J::Value::object({{"key", "driver"}, {"title", "Driver"}, {"sort", true}}),
                                                 J::Value::object({{"key", "helmet"}, {"title", "Helmet when they join a team"}}),
                                                 J::Value::object({{"key", "status"}, {"title", "Status"}, {"width", 100}}),
                                             })},
                                             {"rows", std::move(rows)},
                                             {"sort", J::Value::object({{"column", "driver"}, {"desc", false}})},
                                             {"search", J::Value::object({{"placeholder", "Search free agents"}})}})}}));
    } else {
        // ---- the teams table; a team's panel edits its newcomer helmet and its drivers' helmets
        J::Value rows = J::Value::array();
        for (auto &t : lu.teams) {
            std::string tname = team_name(t), th = team_helmet(cfg, t);
            const auto &ds = lu.drivers.count(t) ? lu.drivers.at(t) : std::vector<std::string>{};
            bool edited = !th.empty();
            auto driver_cell = [&](size_t i) {
                if (i >= ds.size())
                    return J::Value::object({{"text", "—"}, {"tone", "muted"}});
                auto [f, l] = name_parts(ds[i]);
                J::Value spans = J::Value::array({J::Value::object({{"text", f}}), J::Value::object({{"text", l}, {"bold", true}})});
                std::string home = lu.home.count(ds[i]) ? lu.home.at(ds[i]) : t;
                if (lu.player.count(ds[i])) {
                    spans.push_back(J::Value::object({{"text", "  (You)"}, {"tone", "accent"}, {"bold", true}}));
                    return J::Value::object({{"text", f + l + " (You)"}, {"spans", std::move(spans)}});
                }
                if (lu.live && home != t)
                    spans.push_back(J::Value::object(
                        {{"text", "  from " + (home.empty() ? std::string("free agents") : team_name(home))}, {"tone", "muted"}}));
                return J::Value::object({{"text", f + l}, {"spans", std::move(spans)}});
            };
            J::Value detail = J::Value::array();
            detail.push_back(text_block(lu.live ? "The loaded career's line-up." : "The original line-up.", "muted"));
            detail.push_back(select_form("team:" + t, "Drivers who join " + tname + " wear", J::Value(th),
                                         with(J::Value::array({option("", "Default: " + g.helmet_label(dflt))})),
                                         "When the automatic setting is on and a driver moves here."));
            detail.push_back(J::Value::object(
                {{"buttons", J::Value::array({import_button("import-team:" + t, "Import helmet for new " + tname + " drivers…")})}}));
            for (size_t i = 0; i < ds.size(); i++) {
                const std::string &d = ds[i];
                if (lu.player.count(d)) {
                    detail.push_back(card("Driver " + std::to_string(i + 1) + " · " + driver_label(d) + " (You)",
                                          J::Value::array({text_block("The driver you race as. AI Helmets never changes "
                                                                      "your helmet: set it in the game, or with Helmet "
                                                                      "Exchange's Helmets tab.",
                                                                      "muted")})));
                    continue;
                }
                std::string key = d + "|" + t, home = lu.home.count(d) ? lu.home.at(d) : t;
                const J::Value &pick = cfg["picks"][key];
                edited |= pick.is_string() && !pick.as_string().empty();
                bool at_home = home == t;
                J::Value opts = J::Value::array();
                if (at_home)
                    opts.push_back(option("", "Their own helmet"));
                else {
                    opts.push_back(option("", "Team default: " + g.helmet_label(th.empty() ? dflt : th)));
                    opts.push_back(option("own", home.empty() ? std::string("Their own helmet")
                                                              : "Their own helmet (" + team_name(home) + ")"));
                }
                J::Value inner = J::Value::array();
                inner.push_back(text_block(at_home ? "Home team"
                                                   : "Joined from " + (home.empty() ? std::string("the free agents") : team_name(home)),
                                           "muted"));
                inner.push_back(select_form("pick:" + key, "Helmet at " + tname,
                                            pick.is_string() ? pick : J::Value(""), with(std::move(opts))));
                inner.push_back(J::Value::object(
                    {{"buttons", J::Value::array({import_button("import:" + key, "Import helmet for " + name_parts(d).second + "…")})}}));
                detail.push_back(card("Driver " + std::to_string(i + 1) + " · " + driver_label(d), std::move(inner)));
            }
            rows.push_back(J::Value::object(
                {{"id", t},
                 {"title", tname},
                 {"subtitle", t},
                 {"cells", J::Value::array({J::Value::object({{"text", tname}}), driver_cell(0), driver_cell(1),
                                            J::Value::object({{"text", g.helmet_label(th.empty() ? dflt : th)},
                                                              {"tone", th.empty() ? "muted" : "neutral"}}),
                                            J::Value::object({{"text", edited ? "Edited" : "Default"},
                                                              {"tone", edited ? "accent" : "muted"},
                                                              {"pill", true}})})},
                 {"detail", std::move(detail)}}));
        }
        blocks.push_back(J::Value::object(
            {{"table", J::Value::object({{"id", "teams"},
                                         {"columns", J::Value::array({
                                             J::Value::object({{"key", "team"}, {"title", "Team"}, {"sort", true}}),
                                             J::Value::object({{"key", "d1"}, {"title", "Driver 1"}}),
                                             J::Value::object({{"key", "d2"}, {"title", "Driver 2"}}),
                                             J::Value::object({{"key", "newcomers"}, {"title", "Newcomers wear"}}),
                                             J::Value::object({{"key", "status"}, {"title", "Status"}, {"width", 100}}),
                                         })},
                                         {"rows", std::move(rows)},
                                         {"search", J::Value::object({{"placeholder", "Search teams and drivers"}})}})}}));
    }

    J::Value notices = J::Value::array();
    if (lu.live)
        notices.push_back(J::Value::object({{"text", "Showing the loaded career's line-up. It goes back to the original "
                                                     "line-ups when the game closes. Changes show at the next Team HQ or session start."},
                                            {"tone", "accent"}}));
    else if (running)
        notices.push_back(J::Value::object({{"text", "The game is running: the career's line-up shows here after its first "
                                                     "Team HQ. Changes show at the next Team HQ or session start."},
                                            {"tone", "muted"}}));

    J::Value import = import_button("import", "Import helmet…");
    import["kind"]["pick"]["multiple"] = true;
    return J::Value::object({{"subtitle", std::to_string(custom.size()) + " imported helmet(s), " + std::to_string(picks) +
                                              " driver pick(s)"},
                             {"actions", J::Value::array({std::move(import)})},
                             {"notices", std::move(notices)},
                             {"body", J::Value::object({{"blocks", std::move(blocks)}})},
                             {"footer", "AI Helmets. Click a team to set its drivers' helmets."},
                             // a tick about once a second while the page is open: it redraws when the game starts or
                             // stops or the career's line-up changes (0.8.2 and earlier only redrew on a tab change)
                             {"tick", true}});
}

// ---------------------------------------------------------------------------------------------------------
// Events

std::string key_from(const fs::path &file)
{
    std::string stem = to_utf8(file.stem()), key;
    for (char c : stem) {
        unsigned char u = static_cast<unsigned char>(c);
        key += std::isalnum(u) ? char(std::tolower(u)) : (c == '-' ? '-' : '_');
    }
    return key.empty() ? "helmet" : key;
}

// <first>_<last>_helmet.erp -> that driver's newest helmet row, game/<year>-<team>_<first>_<last> (from the
// catalogue's helmet rows, else the stock grid's); anything else -> the default shell.
std::string shell_from(const std::string &key, const J::Value &cat)
{
    const std::string suffix = "_helmet";
    if (key.size() <= suffix.size() || key.compare(key.size() - suffix.size(), suffix.size(), suffix) != 0)
        return kDefaultShell;
    std::string driver = key.substr(0, key.size() - suffix.size()), best;
    auto consider = [&](const std::string &ref) {
        std::string year, team, who;
        if (dated_helmet(ref, year, team, who) && who == driver && team.find('-') == std::string::npos && ref > best)
            best = ref;  // newest year wins; f2 rows ("2024-f2-...") are skipped
    };
    if (auto *a = cat["helmets"].if_array())
        for (auto &h : *a)
            consider(std::string(h.as_string()));
    for (auto &gh : kGridHelmets)
        consider(gh.helmet);
    return best.empty() ? std::string(kDefaultShell) : best;
}

// The driver helmet row a helmet archive was made from, read from its own resources: a driver's helmet archive
// names its folder in every path ("eaid://character_package/drivers/male/2026-redbull_max_verstappen/textures/
// max_verstappen_helmet_d.tif" -> game/2026-redbull_max_verstappen). Empty when it names none the game has. The
// file name rule above only knows "<first>_<last>_helmet.erp", and overtake.gg files are often called
// "f1_25_max_verstappen_helmet.erp".
std::string row_of_archive(const fs::path &file, const J::Value &cat)
{
    std::set<std::string> rows;
    if (auto *a = cat["helmets"].if_array())
        for (auto &h : *a)
            if (h.is_string())
                rows.insert(std::string(h.as_string()));
    for (auto &gh : kGridHelmets)
        rows.insert(gh.helmet);
    try {
        erp::Archive a = erp::load(file);
        for (auto &r : a.resources) {
            const std::string &n = r.name;
            if (n.find("_helmet_d.tif") == std::string::npos)
                continue;
            for (const char *dir : {"eaid://character_package/drivers/male/", "eaid://character_package/drivers/female/"}) {
                size_t len = std::strlen(dir);
                if (n.compare(0, len, dir) != 0)
                    continue;
                size_t slash = n.find('/', len);
                if (slash == std::string::npos)
                    continue;
                std::string row = "game/" + n.substr(len, slash - len);
                if (rows.count(row))
                    return row;
            }
        }
    } catch (...) {
    }
    return {};
}

// What an imported file is: "ok" (a helmet archive), "zip" (a mod package), "career" (a repaint of the game's
// helmet template, such as a markdown_system.erp mod) or "other" (not an ERP).
std::string helmet_file_kind(const fs::path &path)
{
    std::ifstream f(path, std::ios::binary);
    char head[0x30] = {};
    f.read(head, sizeof head);
    if (f.gcount() >= 4 && std::memcmp(head, "PK\x03\x04", 4) == 0)
        return "zip";
    if (f.gcount() < 0x30 || std::memcmp(head, "ERPK", 4) != 0)
        return "other";
    uint32_t version = 0;
    uint64_t data_off = 0;
    std::memcpy(&version, head + 4, 4);
    std::memcpy(&data_off, head + 0x20, 8);
    if (data_off < 0x38 || data_off > (64u << 20))
        return "other";
    std::string idx(data_off, '\0');
    f.seekg(0);
    f.read(idx.data(), std::streamsize(data_off));
    if (size_t(f.gcount()) != data_off)
        return "other";
    size_t pos = 0x30;
    auto u32 = [&](size_t at) { uint32_t v = 0; std::memcpy(&v, idx.data() + at, 4); return v; };
    uint32_t count = u32(pos);
    pos += 8;
    bool career = false, helmet = false, helmet_tex = false, helmet_model = false;
    for (uint32_t i = 0; i < count && pos + 6 < idx.size(); i++) {
        pos += 4;
        uint16_t len = 0;
        std::memcpy(&len, idx.data() + pos, 2);
        pos += 2;
        if (pos + len > idx.size())
            break;
        std::string name = idx.substr(pos, len);
        career |= name.find("charname_helmet") != std::string::npos || name.find("markdown/") != std::string::npos ||
                  (name.find("livery_package") != std::string::npos && name.find("_helmet_d.tif") != std::string::npos);
        helmet |= name.find("_helmet") != std::string::npos;
        helmet_tex |= name.find("_helmet_d.tif") != std::string::npos;
        helmet_model |= name.find("_helmet_attachments.idf") != std::string::npos ||
                        name.find("kitpart_assets/generic/helmets/") != std::string::npos;
        pos += len + 16 + (version >= 4 ? 6 : 4);
        if (pos >= idx.size())
            break;
        uint8_t n = uint8_t(idx[pos]);
        pos += 1 + size_t(n) * (4 + 8 + 8 + 4 + 1 + 8) + 16;
    }
    // a driver's body archive (suit, gloves and boots, with his helmet's textures but no helmet model) can't be
    // worn as a helmet: its textures are converted onto his stock helmet, as a career-helmet mod's are
    if (!career && helmet_tex && !helmet_model)
        career = true;
    return career ? "career" : helmet ? "ok" : "other";
}

std::string kind_problem(const std::string &kind)
{
    if (kind == "zip")
        return "a mod package (zip), not a helmet archive";
    if (kind == "career")
        return "a \"career helmet\" mod (a repaint of the game's helmet template, e.g. markdown_system.erp)";
    if (kind == "other")
        return "not a helmet archive";
    return {};
}

// ---- importing: helmet archives, career-helmet mods (converted), and packs of either ---------------------

std::string unique_key(const std::string &base)
{
    std::set<std::string> taken;
    for (auto &h : g.custom())
        taken.insert(std::string(h["key"].as_string()));
    std::string key = base;
    for (int s = 2; taken.count(key) && s < 1000; s++)
        key = base + "_" + std::to_string(s);
    return key;
}

// A free same-length folder name for a helmet converted onto shell_token: a001-ferrari_lewis_hamilton, a002-…,
// a001ica_hamilton20a for replica_hamilton20a.
std::string next_token(const std::string &shell_token)
{
    std::set<std::string> used;
    for (auto &h : g.custom())
        used.insert(std::string(h["token"].as_string()));
    for (int i = 1; i < 1000; i++) {
        std::string t = convert::token_for(shell_token, i);
        if (t.empty())
            break;
        if (!used.count(t))
            return t;
    }
    return {};
}

// A stock helmet archive (game-relative path) from the game's clean files, else the game folder; empty if absent.
fs::path shell_archive(const std::string &rel)
{
    std::error_code ec;
    J::Value p = call("game_original", J::Value::object({{"rel", rel}}));
    if (p.is_string() && fs::is_regular_file(from_utf8(p.as_string()), ec))
        return from_utf8(p.as_string());
    J::Value d = call("game_dir");
    if (d.is_string() && fs::is_regular_file(from_utf8(d.as_string()) / rel, ec))
        return from_utf8(d.as_string()) / rel;
    return {};
}

// "replica_hamilton20a" -> "Lewis Hamilton (2020)" etc. for messages: the catalogue's label when it has the row.
std::string shell_label(const convert::Shell &s)
{
    std::string l = g.helmet_label("game/" + s.token);
    return l.empty() ? s.token : l;
}

// Unpacks a zip, 7z or rar pack with Windows' own tar (bsdtar, which reads all three).
bool extract(const fs::path &src, const fs::path &dir)
{
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(sys) + L"\\tar.exe\" -xf \"" + src.wstring() + L"\" -C \"" + dir.wstring() + L"\"";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, 5 * 60 * 1000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
}

// A free tag for the mip folder of a helmet archive used as it is ("b001": its own mips go under
// b001-redbull_max_verstappen; converted helmets use their token's "a001").
std::string next_mip_tag()
{
    std::set<std::string> used;
    for (auto &h : g.custom())
        used.insert(std::string(h["mip_tag"].as_string()));
    for (int i = 1; i < 1000; i++) {
        char b[8];
        std::snprintf(b, sizeof b, "b%03d", i);
        if (!used.count(b))
            return b;
    }
    return {};
}

// Keeps the pack's mip files a helmet's textures now name, in helmets/<key>.mips/, for the build to put at their
// new paths. Returns the config entry's "mips" list.
J::Value store_mips(const std::string &key, const std::vector<convert::MipMove> &moves)
{
    J::Value list = J::Value::array();
    std::error_code ec;
    fs::path dir = g.helmets_dir() / from_utf8(key + ".mips");
    fs::remove_all(dir, ec);
    for (auto &m : moves) {
        std::string name = m.to.substr(m.to.rfind('/') + 1);
        fs::create_directories(dir, ec);
        fs::copy_file(m.file, dir / from_utf8(name), fs::copy_options::overwrite_existing, ec);
        if (!ec)
            list.push_back(J::Value::object({{"file", key + ".mips/" + name}, {"target", m.to}}));
    }
    return list;
}

std::string mips_note(size_t n)
{
    return n ? " and its " + std::to_string(n) + " high-resolution mip file(s)" : std::string();
}

// Adds one archive as a helmet: a helmet archive is copied, a career-helmet mod is converted. Returns its ref.
// mips: the .mipmaps files of the pack it came in (their textures' streamed high-resolution levels).
std::string add_archive(const fs::path &file, const std::string &base, const std::string &label, const J::Value &cat,
                        std::vector<std::string> &done, const std::string &reuse_key = {},
                        const std::vector<convert::MipFile> &mips = {}, bool convert_it = false)
{
    std::string kind = helmet_file_kind(file);
    if (convert_it && kind == "ok")
        kind = "career";  // a driver's helmet archive from a pack: converted onto its own stock helmet
    std::error_code ec;
    fs::create_directories(g.helmets_dir(), ec);
    if (kind == "ok") {
        std::string key = reuse_key.empty() ? unique_key(base) : reuse_key, out = key + ".erp";
        fs::copy_file(file, g.helmets_dir() / out, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            done.push_back(label + ": " + ec.message());
            return {};
        }
        std::string own = row_of_archive(file, cat), shell = own.empty() ? shell_from(base, cat) : own;
        J::Value entry = J::Value::object({{"key", key}, {"file", out}, {"from", shell}, {"name", title_case(base)}});
        size_t nmips = 0;
        if (!mips.empty()) {
            // its textures stream their top levels from the stock driver's mip files: point them at the pack's
            std::string tag = next_mip_tag();
            try {
                std::vector<convert::MipMove> moves;
                if (!tag.empty())
                    moves = convert::retarget_archive(file, g.helmets_dir() / out, mips, tag);
                if (!moves.empty()) {
                    entry["mips"] = store_mips(key, moves);
                    entry["mip_tag"] = tag;
                    nmips = moves.size();
                }
            } catch (const std::exception &e) {
                done.push_back(label + ": its mip files weren't used (" + e.what() + ")");
            }
        }
        g.config["custom"].push_back(std::move(entry));
        done.push_back("Imported " + label + mips_note(nmips) + " as " + title_case(base) + ", built on " +
                       g.helmet_label(shell) + (own.empty() ? "" : " (the helmet it was made from)"));
        return std::string(kAddon) + "/" + key;
    }
    if (kind == "career") {
        std::string key = reuse_key.empty() ? unique_key(base) : reuse_key, out = key + ".erp";
        try {
            erp::Archive mod = erp::load(file);  // read whole before the output may replace it
            // the stock helmet the mod was painted for (its visor attachments have their own UVs), else Hamilton's
            convert::Detected det = convert::detect(mod);
            convert::Shell shell;
            fs::path shell_file;
            std::string note;
            if (det.shell) {
                shell_file = shell_archive(det.shell->rel());
                if (!shell_file.empty())
                    shell = *det.shell;
                else
                    note = " (made on " + det.shell->token + ", which isn't in the game folder)";
            }
            if (shell_file.empty())
                shell_file = shell_archive(shell.rel());
            if (shell_file.empty()) {
                done.push_back(label + ": Lewis Hamilton's 2026 helmet isn't in the game folder, and helmets that can't be "
                                       "matched are built on it");
                return {};
            }
            std::string token = next_token(shell.token);
            if (token.empty()) {
                done.push_back(label + ": no free name for another helmet on " + shell.token + " (999 in use)");
                return {};
            }
            std::string stem = convert::stem_for(shell.stem, shell.token, token);
            auto r = convert::convert(mod, erp::load(shell_file), shell.token, token, g.helmets_dir() / out, &mips);
            J::Value mip_list = store_mips(key, r.mips);
            g.config["custom"].push_back(J::Value::object({{"key", key},
                                                           {"mips", std::move(mip_list)},
                                                           {"file", out},
                                                           {"name", title_case(base)},
                                                           {"token", token},
                                                           {"shell", shell.token},
                                                           {"stem", stem},
                                                           {"shell_stem", shell.stem},
                                                           {"pkg", shell.pkg},
                                                           {"dir", shell.dir}}));
            std::string how = det.shell && note.empty() ? " (detected from its " + det.from + ")" : note;
            done.push_back("Converted " + label + " into " + title_case(base) + ": " + std::to_string(r.textures) +
                           " texture(s)" + mips_note(r.mips.size()) + " on " + shell_label(shell) + "'s helmet" + how);
            return std::string(kAddon) + "/" + key;
        } catch (const std::exception &e) {
            done.push_back(label + ": couldn't convert it (" + e.what() + ")");
            return {};
        }
    }
    done.push_back(label + " is not a helmet archive or a helmet mod");
    return {};
}

// One picked file: an archive, or a pack holding archives.
std::vector<std::string> import_file(const fs::path &src, const J::Value &cat, std::vector<std::string> &done,
                                     const std::string &reuse_key = {})
{
    std::vector<std::string> refs;
    std::string kind = helmet_file_kind(src), base = key_from(src), label = to_utf8(src.filename());
    if (kind == "ok" || kind == "career") {
        std::string r = add_archive(src, base, label, cat, done, reuse_key);
        if (!r.empty())
            refs.push_back(r);
        return refs;
    }
    fs::path dir = g.cache_dir / "extract" / base;
    if (!extract(src, dir)) {
        done.push_back(label + " is not a helmet archive, and not a pack this PC can unpack (zip, 7z, rar)");
        return refs;
    }
    std::vector<fs::path> found;
    std::vector<convert::MipFile> mips;  // the pack's streamed mip levels (F1 25/character_package/.../*.mipmaps)
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        std::string ext = to_utf8(it->path().extension());
        for (char &c : ext)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        if (it->is_regular_file(ec) && ext == ".mipmaps") {
            std::string rel = to_utf8(it->path().lexically_relative(dir));
            for (char &c : rel)
                if (c == '\\')
                    c = '/';
            mips.push_back({convert::lower(rel), it->path()});
        }
        if (it->is_regular_file(ec) && ext == ".erp") {
            std::string k = helmet_file_kind(it->path());
            if (k == "ok" || k == "career")
                found.push_back(it->path());
        }
    }
    if (found.empty())
        done.push_back(label + " holds no helmet archive or helmet mod");
    // A real driver's helmet comes as several archives (his helmet archive, his body archive with the same helmet
    // textures, ...), all on one stock helmet: they become one helmet, made from the helmet archive when the pack
    // has one (it carries the visor textures too), converted onto that stock helmet under a folder of its own.
    // Archives that name no stock helmet (career-helmet mods) stay one helmet each.
    struct Group {
        std::vector<fs::path> files;
        fs::path pick;
        bool model = false;
    };
    std::map<std::string, Group> groups;
    std::vector<std::string> order;
    for (size_t i = 0; i < found.size(); i++) {
        std::string id;
        bool model = false;
        try {
            erp::Archive a = erp::load(found[i]);
            auto det = convert::detect(a);
            if (det.shell)
                id = det.shell->rel();
            model = det.from == "visor attachments model";
        } catch (...) {
        }
        if (id.empty())
            id = "#" + std::to_string(i);  // one helmet of its own
        // two whole helmets on one stock helmet (two SERPs designs, both on replica_hamilton20a) stay two
        if (model && groups.count(id) && groups[id].model)
            id += "#" + std::to_string(i);
        auto [it, fresh] = groups.try_emplace(id);
        if (fresh)
            order.push_back(id);
        Group &grp = it->second;
        grp.files.push_back(found[i]);
        if (grp.pick.empty() || (model && !grp.model)) {
            grp.pick = found[i];
            grp.model = model;
        }
    }
    for (auto &id : order) {
        Group &grp = groups[id];
        bool one = order.size() == 1;
        std::string b = one ? base : base + "_" + key_from(grp.pick);
        std::string l = label + (found.size() == 1 ? "" : " (" + to_utf8(grp.pick.filename()) + ")");
        std::string r = add_archive(grp.pick, b, l, cat, done, one ? reuse_key : std::string(), mips,
                                    grp.files.size() > 1);
        if (r.empty())
            continue;
        refs.push_back(r);
        if (grp.files.size() > 1) {
            std::string others;
            for (auto &f : grp.files)
                if (f != grp.pick)
                    others += (others.empty() ? "" : ", ") + to_utf8(f.filename());
            done.push_back("  " + others + (grp.files.size() > 2 ? " hold" : " holds") +
                           " the same helmet, so it isn't a helmet of its own");
        }
    }
    fs::remove_all(dir, ec);
    return refs;
}

// The imported helmets 0.5.x took in as they were (packs and career-helmet mods, white in game), converted in
// place: each keeps its key, so the teams and drivers using it keep it.
void convert_pending(std::vector<std::string> &done)
{
    const J::Value cat = g.catalogue();
    J::Value kept = J::Value::array();
    std::vector<J::Value> todo;
    bool fixed = false;
    for (auto &h : g.custom()) {
        fs::path f = g.helmets_dir() / std::string(h["file"].as_string());
        std::string k = helmet_file_kind(f);
        if (k == "ok" && !h["token"].is_string() && h["from"].as_string() == kDefaultShell) {
            // imported before "Made for" was read from the archive: put it on the helmet it was made from
            std::string own = row_of_archive(f, cat);
            J::Value x = h;
            if (!own.empty() && own != kDefaultShell) {
                x["from"] = own;
                done.push_back(std::string(h["name"].as_string()) + " is now built on " + g.helmet_label(own) +
                               ", the helmet it was made from");
                fixed = true;
            }
            kept.push_back(std::move(x));
        } else if (k == "ok" || h["token"].is_string())
            kept.push_back(h);
        else
            todo.push_back(h);
    }
    g.config["custom"] = std::move(kept);
    size_t i = 0;
    for (auto &h : todo) {
        std::string key(h["key"].as_string());
        send(J::Value::object({{"method", "progress"},
                               {"params", J::Value::object({{"done", double(i++) / double(std::max<size_t>(todo.size(), 1))},
                                                            {"text", "Converting " + std::string(h["name"].as_string(key))}})}}));
        fs::path stored = g.helmets_dir() / std::string(h["file"].as_string());
        fs::path work = g.cache_dir / "convert" / key / std::string(h["file"].as_string());
        std::error_code ec;
        fs::create_directories(work.parent_path(), ec);
        fs::copy_file(stored, work, fs::copy_options::overwrite_existing, ec);
        size_t before = g.custom().size();
        auto refs = import_file(work, cat, done, key);
        if (refs.empty() || g.custom().size() == before) {
            g.config["custom"].push_back(h);  // keep it as it was
            continue;
        }
        if (auto *a = g.config["custom"].if_array())  // keep its name
            for (auto &x : *a)
                if (x["key"].as_string() == key)
                    x["name"] = h["name"];
        fs::remove(work, ec);
    }
    if (!todo.empty() || fixed)
        g.save(false);  // run by the build only: data_dir's copy
}

// Imports helmet files; returns the refs of the helmets added. pick_key, when set, gets the (first) helmet.
J::Value import(const J::Value &paths, const std::string &target = {})
{
    g.load();
    const J::Value cat = g.catalogue();
    std::vector<std::string> done;
    auto *a = paths.if_array();
    size_t i = 0, n = a ? a->size() : 0;
    std::string first_ref;
    for (size_t k = 0; k < n; k++) {
        fs::path src = from_utf8((*a)[k].as_string());
        send(J::Value::object({{"method", "progress"},
                               {"params", J::Value::object({{"done", double(i++) / double(std::max<size_t>(n, 1))},
                                                            {"text", "Importing " + to_utf8(src.filename())}})}}));
        std::vector<std::string> refs = import_file(src, cat, done);
        if (first_ref.empty() && !refs.empty())
            first_ref = refs.front();
    }
    // target: "import:<driver>|<team>" (his helmet at that team), "import-team:<team>" (the team's newcomer
    // helmet), "import-fa:<driver>" (a free agent's helmet wherever he races)
    if (!first_ref.empty()) {
        const std::string later = " from the next Play (imported helmets are added when the game starts)";
        if (target.rfind("import:", 0) == 0) {
            std::string key = target.substr(7);
            auto bar = key.find('|');
            g.config["picks"][key] = first_ref;
            done.push_back(driver_label(key.substr(0, bar)) + " wears it at " + team_name(key.substr(bar + 1)) + later);
        } else if (target.rfind("import-team:", 0) == 0) {
            std::string team = target.substr(12);
            g.config["teams"][team]["helmet"] = first_ref;
            done.push_back("Drivers who join " + team_name(team) + " wear it" + later);
        } else if (target.rfind("import-fa:", 0) == 0) {
            std::string d = target.substr(10);
            g.config["drivers"][d] = first_ref;
            done.push_back(driver_label(d) + " wears it at whichever team they join" + later);
        }
    }
    g.save();
    return J::Value::object({{"notice", [&] {
                                  std::string s;
                                  for (auto &l : done)
                                      s += (s.empty() ? "" : "\n") + l;
                                  return s.empty() ? std::string("Nothing imported") : s;
                              }()}});
}

J::Value remove_helmet(const std::string &key)
{
    g.load();
    J::Value kept = J::Value::array();
    std::string file;
    for (auto &h : g.custom())
        if (h["key"].as_string() == key)
            file = std::string(h["file"].as_string());
        else
            kept.push_back(h);
    g.config["custom"] = std::move(kept);
    std::string ref = std::string(kAddon) + "/" + key;
    if (auto *o = g.config["teams"].if_object())
        for (auto &[t, v] : *o)
            if (v["helmet"].as_string() == ref)
                v["helmet"] = "";
    for (const char *map : {"picks", "drivers"}) {
        std::vector<std::string> drop;
        if (auto *o = g.config[map].if_object())
            for (auto &[d, v] : *o)
                if (v.as_string() == ref)
                    drop.push_back(d);
        for (auto &d : drop)
            g.config[map].erase(d);
    }
    if (g.config["default_helmet"].as_string() == ref)
        g.config["default_helmet"] = "";
    if (!file.empty()) {
        std::error_code ec;
        fs::remove(g.helmets_dir() / file, ec);
        fs::remove_all(g.helmets_dir() / from_utf8(key + ".mips"), ec);
    }
    g.save();
    return J::Value::object({{"notice", "Removed " + key}});
}

J::Value submit(const J::Value &s)
{
    g.load();
    std::string form(s["form"].as_string());
    auto *values = s["values"].if_object();
    if (!values)
        return J::Value("none");
    for (auto &[id, v] : *values) {
        std::string value(v.as_string());
        if (form == "default" && id == "helmet")
            g.config["default_helmet"] = value == kDefaultBase ? std::string() : value;
        else if (form.rfind("team:", 0) == 0 && id == "helmet")
            g.config["teams"][form.substr(5)]["helmet"] = value;
        else if (form.rfind("pick:", 0) == 0 && id == "helmet") {
            std::string key = form.substr(5);
            if (value.empty())
                g.config["picks"].erase(key);
            else
                g.config["picks"][key] = value;
        } else if (form.rfind("fa:", 0) == 0 && id == "helmet") {
            std::string d = form.substr(3);
            if (value.empty())
                g.config["drivers"].erase(d);
            else
                g.config["drivers"][d] = value;
        } else if (form == "custom" && id.rfind("from:", 0) == 0) {
            std::string key = id.substr(5);
            if (auto *a = g.config["custom"].if_array())
                for (auto &h : *a)
                    if (h["key"].as_string() == key)
                        h["from"] = value;
        }
    }
    if (!g.save())
        return J::Value::object({{"notice", "Could not save config.json"}});
    return J::Value("reload");
}

J::Value event(const J::Value &ev)
{
    if (auto *t = ev.find("tab")) {
        std::string next((*t)["tab"].as_string());
        if (next == "teams" || next == "helmets" || next == "free" || next == "myteam")
            tab = next;
        return J::Value("reload");
    }
    if (auto *p = ev.find("picked")) {
        std::string id((*p)["id"].as_string());
        return import((*p)["paths"], id);
    }
    if (auto *c = ev.find("click")) {
        std::string id((*c)["id"].as_string());
        if (id.rfind("remove:", 0) == 0)
            return remove_helmet(id.substr(7));
        if (id.rfind("fa-edit:", 0) == 0) {
            g_fa_editing = id.substr(8);
            return J::Value("reload");
        }

        return J::Value("none");
    }
    if (auto *s = ev.find("submit"))
        return submit(*s);
    return J::Value("none");
}

// ---------------------------------------------------------------------------------------------------------
// Build: hand config.json and the imported helmets to the game folder.

J::Value build(const J::Value &params)
{
    g.load();
    g.save(false);  // always (Driver Manager's drivers may have changed), and never into the game folder here
    // helmets an earlier version imported as they were (packs, career helmet mods) are converted now
    std::vector<std::string> converted;
    convert_pending(converted);
    // file.put is always copied into the game folder at build; the "file" feature (experimental on most
    // profiles) only adds the runtime overlay. 0.5.2 and earlier returned nothing unless "file" was stable, so
    // config.json and the imported helmets never reached the game.
    (void)params;
    J::Value ops = J::Value::array(), warnings = J::Value::array();
    auto put = [&](const std::string &rel) {
        ops.push_back(J::Value::object({{"op", "file.put"},
                                        {"source", J::Value::object({{"root", "data"}, {"path", rel}})},
                                        {"target", std::string(kGameDir) + rel}}));
    };
    put("config.json");
    size_t n = 0;
    for (auto &h : g.custom()) {
        std::string file(h["file"].as_string());
        std::error_code ec;
        if (file.empty() || !fs::is_regular_file(g.helmets_dir() / file, ec)) {
            warnings.push_back("helmet " + std::string(h["key"].as_string()) + ": helmets/" + file + " is missing");
            continue;
        }
        if (h["token"].is_string() && !h["token"].as_string().empty()) {
            // a converted helmet: its archive goes into the game's character folders under its own name, with an
            // asset group of its own on Hamilton's helmet group as the template. The archive is a whole copy of
            // his, so the group lists only it, as Helmet Exchange's groups do. (0.6.0 listed his stock archive
            // first, as the core does for its own helmet groups; the group then named that archive only, and the
            // game reported the converted helmets' attachments model missing.)
            // (on the stock helmet the mod was made for; entries converted before that keep Hamilton's folders)
            std::string token(h["token"].as_string()), stem(h["stem"].as_string("lewis_hamilton_helmet"));
            std::string shell(h["shell"].as_string(convert::kShellToken)), shell_stem(h["shell_stem"].as_string(stem));
            std::string at = std::string(h["pkg"].as_string("character_package")) + "/" +
                             std::string(h["dir"].as_string("drivers/male")) + "/";
            std::string erp = "2025_asset_groups/" + at + token + "/" + stem + ".erp";
            ops.push_back(J::Value::object({{"op", "file.put"},
                                            {"source", J::Value::object({{"root", "data"}, {"path", "helmets/" + file}})},
                                            {"target", erp}}));
            ops.push_back(J::Value::object(
                {{"op", "group.register"},
                 {"group", "asset_groups/" + at + token + "/" + stem + ".assetgroup"},
                 {"erp", erp},
                 {"template", "asset_groups/" + at + shell + "/" + shell_stem + ".assetgroup"}}));
        } else
            put("helmets/" + file);
        // the pack's mip files, at the helmet's own paths its textures now name
        if (auto *m = h["mips"].if_array())
            for (auto &x : *m) {
                std::string src(x["file"].as_string()), target(x["target"].as_string());
                if (src.empty() || target.empty() || !fs::is_regular_file(g.helmets_dir() / from_utf8(src), ec)) {
                    warnings.push_back("helmet " + std::string(h["key"].as_string()) + ": helmets/" + src + " is missing");
                    continue;
                }
                ops.push_back(J::Value::object({{"op", "file.put"},
                                                {"source", J::Value::object({{"root", "data"}, {"path", "helmets/" + src}})},
                                                {"target", target}}));
            }
        n++;
    }
    size_t picks = 0;
    for (const char *m : {"picks", "drivers"})
        if (auto *o = g.config[m].if_object())
            picks += o->size();
    J::Value lines = J::Value::array({std::to_string(n) + " helmet(s) of your own, " + std::to_string(picks) + " driver pick(s)"});
    for (auto &l : converted)
        lines.push_back(l);
    return J::Value::object({{"ops", std::move(ops)}, {"lines", std::move(lines)}, {"warnings", std::move(warnings)}});
}

J::Value summary()
{
    g.load();
    size_t picks = 0;
    for (const char *m : {"picks", "drivers"})
        if (auto *o = g.config[m].if_object())
            picks += o->size();
    size_t own = g.custom().size();
    if (!picks && !own)
        return J::Value();
    return J::Value::object({{"label", "AI HELMETS"},
                             {"items", J::Value::array({std::to_string(own) + " own helmet(s)",
                                                        std::to_string(picks) + " pick(s)"})}});
}

}  // namespace

int main()
{
#if defined(_WIN32)
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::string line;
    while (read_line(line)) {
        if (line.empty())
            continue;
        J::Error err;
        J::Value msg = J::parse(line, &err);
        if (!err.ok() || !msg.is_object())
            continue;
        std::string method(msg["method"].as_string());
        if (!msg.contains("id"))
            continue;  // notifications (profile): nothing to do
        J::Value reply = J::Value::object({{"id", msg["id"]}});
        std::string invalidate;  // a section to redraw, told after the reply (a notification: no id)
        try {
            const J::Value &p = msg["params"];
            if (method == "hello") {
                g.data_dir = from_utf8(p["data_dir"].as_string());
                g.cache_dir = from_utf8(p["cache_dir"].as_string());
                g.launcher = std::string(p["launcher"].as_string());
                std::error_code ec;
                fs::create_directories(g.data_dir, ec);
                reply["result"] = J::Value::object({{"api", 1}});
            } else if (method == "view") {
                reply["result"] = view();
            } else if (method == "event") {
                reply["result"] = event(p["event"]);
            } else if (method == "build") {
                reply["result"] = build(p);
            } else if (method == "summary") {
                reply["result"] = summary();
            } else if (method == "tick") {
                // redraw the section when what it shows has changed since it was drawn
                std::string sig = g.page_signature();
                if (sig != g_shown) {
                    g_shown = sig;
                    invalidate = std::string(p["section"].as_string("helmets"));
                }
                reply["result"] = nullptr;
            } else if (method == "import") {
                J::Value r = import(p["paths"]);
                reply["result"] = J::Value::array({r["notice"]});
            } else if (method == "command") {
                reply["result"] = "Open the AI Helmets page in the launcher's sidebar.";
            } else {
                reply["error"] = "unknown method " + method;
            }
        } catch (const std::exception &e) {
            reply = J::Value::object({{"id", msg["id"]}, {"error", e.what()}});
        }
        send(reply);
        if (!invalidate.empty())
            send(J::Value::object({{"method", "invalidate"}, {"params", J::Value::object({{"section", invalidate}})}}));
    }
    return 0;
}

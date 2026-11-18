// AI Helmets: helmets for AI drivers.
//
//  * Team placeholders: one helmet per team, the game's customisable stock design tinted with the team's
//    colours (or a helmet of your own picked for that team on the launcher page).
//  * Custom helmets: your own .erp files, imported on the launcher page, registered as helmets of their own.
//  * Manual picks: any helmet for any AI driver, everywhere (a boot layer on the driver's look).
//  * Automatic mode: at Team HQ the plugin reads the career's line-up; a driver racing for a team other than
//    the one he started the career with wears that team's placeholder (a career-scope layer, applied from
//    the next session load). He gets his own helmet back if he returns.
//
// Config: config.json in this addon's storage (swofty/addons/sedz.ai-helmets/), written by the launcher
// page. The plugin writes catalogue.json there for the page: the drivers, teams and helmets it saw.

#include <windows.h>

#include <swofty/db.hpp>
#include <swofty/events.hpp>
#include <swofty/mem.hpp>
#include <swofty/registry.hpp>
#include <swofty/storage.hpp>
#include <swofty/career.hpp>
#include <swofty/systems/career.hpp>
#include <swofty/systems/drivers.hpp>
#include <swofty/systems/teams.hpp>
#include <swofty/systems/wearables.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

using namespace swofty;
namespace J = swofty::json;

SWOFTY_SETTING(bool, auto_team_helmets, true);
SWOFTY_SETTING(bool, verbose, false);

namespace {

constexpr const char *kAddon = "sedz.ai-helmets";
// A helmet row label from customisation.custom_avatar.helmets ("Simplicity"). 0.1.0 used
// "helmet_tier01_livery03", a label the game doesn't have, and the core refused every placeholder.
constexpr const char *kDefaultBase = "game/tier01_livery76";
// The shell an imported helmet starts from when the page named none (or named a 0.1.0 "game/<driver>").
constexpr const char *kDefaultShell = "game/2026-ferrari_lewis_hamilton";

// ---------------------------------------------------------------------------------------------------------
// Teams and their placeholder colours (colour_1..4 of the customisable design). The launcher page can
// override each team's colours or swap in a helmet of your own.

struct TeamDef {
    const char *key;   // the stock team row, as game/<key>
    const char *name;
    const char *colours[4];
};

constexpr TeamDef kTeams[] = {
    // 2026 season
    {"mclaren_26", "McLaren", {"#FF8000", "#111111", "#47C7FC", "#FFFFFF"}},
    {"mercedes_26", "Mercedes", {"#00D2BE", "#111111", "#C0C0C0", "#FFFFFF"}},
    {"redbull_26", "Red Bull Racing", {"#1B2A5C", "#DA291C", "#FFC906", "#FFFFFF"}},
    {"ferrari_26", "Ferrari", {"#DC0000", "#FFF200", "#111111", "#FFFFFF"}},
    {"williams_26", "Williams", {"#005AFF", "#041E42", "#FFFFFF", "#00A3E0"}},
    {"tororosso_26", "Racing Bulls", {"#6692FF", "#FFFFFF", "#1A1A1A", "#E30118"}},
    {"forceindia_26", "Aston Martin", {"#006F62", "#CEDC00", "#111111", "#FFFFFF"}},
    {"haas_26", "Haas", {"#B6BABD", "#E6002B", "#111111", "#FFFFFF"}},
    {"sauber_26", "Audi", {"#1A1A1A", "#FF2D00", "#C0C0C0", "#FFFFFF"}},
    {"renault_26", "Alpine", {"#0090FF", "#FF87BC", "#0A1A3A", "#FFFFFF"}},
    {"gm_cadillac_26", "Cadillac", {"#1E1E1E", "#D0D2DB", "#FFFFFF", "#C8102E"}},
    // 2025 season
    {"mclaren", "McLaren (2025)", {"#FF8000", "#111111", "#47C7FC", "#FFFFFF"}},
    {"mercedes", "Mercedes (2025)", {"#00D2BE", "#111111", "#C0C0C0", "#FFFFFF"}},
    {"redbull", "Red Bull Racing (2025)", {"#1B2A5C", "#DA291C", "#FFC906", "#FFFFFF"}},
    {"ferrari", "Ferrari (2025)", {"#DC0000", "#FFF200", "#111111", "#FFFFFF"}},
    {"williams", "Williams (2025)", {"#005AFF", "#041E42", "#FFFFFF", "#00A3E0"}},
    {"tororosso", "Racing Bulls (2025)", {"#6692FF", "#FFFFFF", "#1A1A1A", "#E30118"}},
    {"forceindia", "Aston Martin (2025)", {"#006F62", "#CEDC00", "#111111", "#FFFFFF"}},
    {"haas", "Haas (2025)", {"#B6BABD", "#E6002B", "#111111", "#FFFFFF"}},
    {"sauber", "Kick Sauber (2025)", {"#52E252", "#111111", "#FFFFFF", "#1A1A1A"}},
    {"renault", "Alpine (2025)", {"#0090FF", "#FF87BC", "#0A1A3A", "#FFFFFF"}},
};

std::string placeholder_key(std::string_view team) { return "team_" + std::string(team); }
std::string own_ref(std::string_view key) { return std::string(kAddon) + "/" + std::string(key); }

// ---------------------------------------------------------------------------------------------------------
// Config

struct Config {
    std::string base = kDefaultBase;                // newcomers' helmet for a team that names none
    std::map<std::string, std::string> team_helmet;  // team key -> newcomers' helmet ref ("" = base)
    std::map<std::string, std::string> picks;        // "game/<driver>|<team key>" -> helmet ref | "own"
    std::map<std::string, std::string> drivers;      // 0.1-0.4 picks, any team: driver ref -> "own" | helmet ref
    // drivers another addon writes into the database (Driver Manager's dm_<id>): driver ref -> the helmet his look
    // holds, which the launcher page reads from that addon's data
    std::map<std::string, std::string> addon_drivers;
    struct Custom {
        std::string key, file, from, name;
        std::string token, shell, stem;  // a converted helmet mod: its own folder name, built on shell
        std::string pkg, dir;            // the shell's package and folder (default character_package, drivers/male)
    };
    std::vector<Custom> custom;
};

Config g_config;
std::mutex g_lock;

std::string str(const J::Value &v, std::string_view fallback = {}) { return std::string(v.as_string(fallback)); }

std::string g_config_text;  // config.json as last read, to notice the page's live edits

bool parse_config(const std::string &text, Config &c)
{
    J::Error err;
    J::Value j = J::parse(text, &err);
    if (!err.ok() || !j.is_object()) {
        swofty::log("[ai-helmets] config.json is not valid JSON (%s); keeping the previous settings", err.message.c_str());
        return false;
    }
    for (const char *k : {"default_helmet", "base_design"})
        if (j[k].is_string() && !j[k].as_string().empty()) {
            c.base = str(j[k]);
            break;
        }
    if (c.base.find("/team_") != std::string::npos)  // a 0.1-0.4 tinted placeholder: gone in 0.5.0
        c.base = kDefaultBase;
    if (auto *teams = j["teams"].if_object())
        for (auto &[key, t] : *teams) {
            std::string h = str(t["helmet"]);
            if (!h.empty() && h.find("/team_") == std::string::npos)
                c.team_helmet[key] = h;
        }
    if (auto *picks = j["picks"].if_object())
        for (auto &[key, pick] : *picks)
            if (pick.is_string() && !pick.as_string().empty() && key.find('|') != std::string::npos)
                c.picks[key] = str(pick);
    if (auto *drivers = j["drivers"].if_object())
        for (auto &[ref, pick] : *drivers)
            if (pick.is_string() && !pick.as_string().empty() && pick.as_string() != "auto" &&
                pick.as_string().find("/team_") == std::string::npos)
                c.drivers[ref] = str(pick);
    if (auto *ad = j["addon_drivers"].if_object())
        for (auto &[ref, h] : *ad)
            if (h.is_string() && !h.as_string().empty())
                c.addon_drivers[ref] = str(h);
    if (auto *custom = j["custom"].if_array())
        for (auto &h : *custom) {
            Config::Custom x{str(h["key"]),   str(h["file"]),  str(h["from"]), str(h["name"]),
                             str(h["token"]), str(h["shell"]), str(h["stem"]), str(h["pkg"]), str(h["dir"])};
            if (!x.key.empty() && !x.file.empty())
                c.custom.push_back(std::move(x));
        }
    return true;
}

void load_config()
{
    std::string text;
    if (storage::read("config.json", text) != storage::Result::ok || text.empty()) {
        swofty::log("[ai-helmets] no config.json yet: newcomers wear %s, no picks", kDefaultBase);
        return;
    }
    Config c;
    if (!parse_config(text, c))
        return;
    g_config = std::move(c);
    g_config_text = text;
    swofty::log("[ai-helmets] config: %zu custom helmet(s), %zu team helmet(s), %zu pick(s); newcomers default to %s",
                g_config.custom.size(), g_config.team_helmet.size(), g_config.picks.size() + g_config.drivers.size(),
                g_config.base.c_str());
}

// The launcher page writes config.json into the running game's addon folder too, so a change shows at the next
// Team HQ or session start. Helmets imported while the game runs need a restart (they register at boot).
void reload_config()
{
    std::string text;
    if (storage::read("config.json", text) != storage::Result::ok || text.empty() || text == g_config_text)
        return;
    Config c;
    if (!parse_config(text, c))
        return;
    c.custom = g_config.custom;  // what was registered at boot
    g_config = std::move(c);
    g_config_text = text;
    swofty::log("[ai-helmets] config.json changed: %zu team helmet(s), %zu pick(s), newcomers default to %s",
                g_config.team_helmet.size(), g_config.picks.size() + g_config.drivers.size(), g_config.base.c_str());
}

// The helmet a team's newcomers wear: the one picked for the team, else the default (Simplicity).
std::string team_placeholder(const std::string &team)
{
    auto it = g_config.team_helmet.find(team);
    if (it != g_config.team_helmet.end() && !it->second.empty())
        return it->second;
    return g_config.base;
}

// The helmet a driver wears at a team ("" = his own): his pick for that team, else an any-team pick from
// before 0.5.0, else (after a move, with the automatic setting on) the team's newcomer helmet.
std::string want_for(const std::string &driver, const std::string &team, bool moved)
{
    if (auto p = g_config.picks.find(driver + "|" + team); p != g_config.picks.end())
        return p->second == "own" ? std::string() : p->second;
    if (auto d = g_config.drivers.find(driver); d != g_config.drivers.end())
        return d->second == "own" ? std::string() : d->second;
    if (moved && auto_team_helmets)
        return team_placeholder(team);
    return {};
}

// MyTeam: the player's own team. Its drivers are picked on the page's MyTeam tab, under the team key "myteam"
// (the MyTeam team's database row differs from career to career). A MyTeam driver wears his pick there, else
// the helmet set for any MyTeam driver, else his own: the automatic newcomer helmet never applies, since every
// MyTeam driver has moved from somewhere.
constexpr const char *kMyTeam = "myteam";

std::string want_for_myteam(const std::string &driver)
{
    if (auto p = g_config.picks.find(driver + "|" + kMyTeam); p != g_config.picks.end())
        return p->second == "own" ? std::string() : p->second;
    auto t = g_config.team_helmet.find(kMyTeam);
    return t != g_config.team_helmet.end() ? t->second : std::string();
}

// A pick outside a career (no line-up): the any-team picks only.
std::string pick_of(const std::string &driver)
{
    auto it = g_config.drivers.find(driver);
    if (it == g_config.drivers.end() || it->second == "own")
        return {};
    return it->second;
}

std::set<std::string> g_boot_picked;  // drivers whose pick went on as a boot layer

// A helmet shell ref: a stock helmet row is "game/<year>-<team>_<driver>". 0.1.0 wrote "game/<driver>",
// which is no helmet row; those take the default shell.
std::string shell_ref(const std::string &from)
{
    if (from.empty())
        return kDefaultShell;
    if (from.rfind("game/", 0) == 0) {
        std::string label = from.substr(5);
        bool dated = label.size() > 5 && std::isdigit(static_cast<unsigned char>(label[0])) && label[4] == '-';
        bool known = dated || label.rfind("tier0", 0) == 0 || label.rfind("unassigned_", 0) == 0 ||
                     label.rfind("replica_", 0) == 0 || label.rfind("classic-", 0) == 0 ||
                     label.rfind("novelty_", 0) == 0;
        if (!known)
            return kDefaultShell;
    }
    return from;
}

// looks.json: {"game/<driver>": ["game/<look>", ...]}, written by the plugin from the game database.
std::map<std::string, std::vector<std::string>> read_looks_cache()
{
    std::map<std::string, std::vector<std::string>> out;
    std::string text;
    if (storage::read("looks.json", text) != storage::Result::ok || text.empty())
        return out;
    J::Value v = J::parse(text);
    if (auto *o = v["drivers"].if_object())
        for (auto &[driver, list] : *o)
            if (auto *a = list.if_array())
                for (auto &l : *a)
                    if (l.is_string() && !l.as_string().empty())
                        out[driver].push_back(str(l));
    return out;
}

// ---------------------------------------------------------------------------------------------------------
// Registration (early stage, once per game start)

void register_helmets()
{
    size_t custom = 0, picks = 0;
    for (auto &h : g_config.custom) {
        Ref<wearables::Helmet> ref;
        if (!h.token.empty() && !h.shell.empty() && !h.stem.empty()) {
            // A converted helmet mod (the launcher page built its archive on the stock helmet it was made for, a
            // driver's or a design's such as replica_hamilton20a, under the folder name h.token, and the build
            // registered that archive's asset group): a copy of the shell's helmet row that points at the
            // converted archive's group, material context and attachments. Paths use the game's package
            // placeholders: character_package -> [CHARACTER_PACKAGE], livery_package -> [LIVERY_PACKAGE].
            wearables::Helmet rec{
                .from = wearables::helmets["game/" + h.shell],
                .name = Text::literal((h.name.empty() ? h.key : h.name) + " (AIH)"),
            };
            std::string pkg = h.pkg.empty() ? "character_package" : h.pkg;
            for (char &c : pkg)
                c = char(std::toupper(static_cast<unsigned char>(c)));
            pkg = "[" + pkg + "]";
            std::string dir = (h.dir.empty() ? std::string("drivers/male") : h.dir) + "/" + h.token + "/";
            rec.extra = J::Value::object();
            rec.extra["asset_group"] = "asset_groups/" + pkg + "/" + dir + h.stem + ".assetgroup";
            rec.extra["material_override"] = h.token;
            rec.extra["attachments_model"] = "eaid://" + pkg + "/" + dir + "idf/" + h.stem + "_attachments";
            ref = wearables::helmets.add(h.key, rec);
        } else
            ref = wearables::helmets.add(h.key, {
                                                    .archive = file("helmets/" + h.file),
                                                    .from = wearables::helmets[shell_ref(h.from)],
                                                    .name = Text::literal((h.name.empty() ? h.key : h.name) + " (AIH)"),
                                                });
        if (ref.empty())
            swofty::log("[ai-helmets] custom helmet %s (%s) refused: %s", h.key.c_str(), h.file.c_str(), last_error().c_str());
        else
            custom++;
    }
    // Manual picks used to be boot layers on the driver's looks. A layer on a stock look makes the core add a
    // row (copied from the driver-pool row) rather than change his, so 0.4.0 applies picks at runtime instead,
    // in the look records in memory, the same way as the team placeholders.
    picks = g_config.picks.size() + g_config.drivers.size();
    swofty::log("[ai-helmets] registered %zu custom helmet(s); %zu pick(s) apply at runtime", custom, picks);
}

// ---------------------------------------------------------------------------------------------------------
// The career's line-up: its entrant rows (swofty game headers, "L2 experimental": CareerEntity +0x2e0/+0x2e8,
// 0x88-byte EntrantRecord). Read-only.

constexpr size_t kEntrantsBegin = 0x2e0, kEntrantsEnd = 0x2e8, kEntrantBusy = 0x2f8;
constexpr size_t kRecSize = 0x88, kOffFlags = 0x48, kOffDriver = 0x50, kOffTeam = 0x58, kOffVehicle = 0x5c,
                 kOffTeamPos = 0x80;
constexpr uint32_t kNoKey = 0x3fff0000u, kHumanBit = 0x10000000u;
constexpr size_t kMaxEntrants = 256;

struct Seat {
    uint32_t driver_stat = 0, team_stat = 0, vehicle = 0;
    int position = -1;
    bool human = false, has_car = false, has_team = false;
};

bool none(uint32_t key) { return (key & 0xffff0000u) == kNoKey; }

bool read_lineup(std::vector<Seat> &out)
{
    out.clear();
    auto e = reinterpret_cast<uintptr_t>(career::entity());
    if (!e || !mem::object_like(e))
        return false;
    uint8_t busy = 0;
    if (mem::read(e + kEntrantBusy, busy) && busy)
        return false;
    uintptr_t b = 0, en = 0;
    if (!mem::read(e + kEntrantsBegin, b) || !mem::read(e + kEntrantsEnd, en) || !b || en <= b || (en - b) % kRecSize ||
        (en - b) / kRecSize > kMaxEntrants || !mem::readable(b, en - b))
        return false;
    for (uintptr_t at = b; at < en; at += kRecSize) {
        uint32_t flags = 0, driver = 0, team = 0, vehicle = 0;
        uint16_t tp = 0;
        if (!mem::read(at + kOffFlags, flags) || !mem::read(at + kOffDriver, driver) || !mem::read(at + kOffTeam, team) ||
            !mem::read(at + kOffVehicle, vehicle) || !mem::read(at + kOffTeamPos, tp))
            return false;
        Seat s;
        s.driver_stat = driver >> 16;
        s.team_stat = team >> 16;
        s.has_team = !none(team);
        s.has_car = !none(vehicle);
        s.vehicle = vehicle;
        s.position = (tp >> 8) ? (tp & 0xff) : -1;
        s.human = (flags & kHumanBit) != 0;
        out.push_back(s);
    }
    return true;
}

// The game database's own tables, read once it has loaded (the same rows the core's career operations read):
// stat id -> "game/<label>" for drivers and teams, each driver's looks, and the helmet rows. 0.1.0 asked the
// registries instead; they list addon records only, so it found no stock driver at all.
std::unordered_map<uint32_t, std::string> g_driver_of_stat, g_team_of_stat;
std::map<std::string, std::vector<std::string>> g_looks;  // driver ref -> its look refs
std::vector<std::string> g_helmet_rows;                    // "game/<label>" of every helmet row
std::map<std::string, uint32_t> g_helmet_id;               // "game/<label>" -> the helmet's item id
// Drivers another addon made (Driver Manager, Silly Season): "game/<row label>" -> the helmet of the look his
// identity names. Their helmets aren't named like their looks, so the name rule finds none.
std::map<std::string, std::string> g_addon_helmet;
// Drivers whose look shares its helmet with other drivers (More Driver Models gives each driver it adds a copy of
// the free-agent look, mdm_<driver>, all wearing unassigned_livery18): "game/<driver>" -> where that look's row is.
// Their helmet is changed in that one look record, not in every record holding the helmet.
struct LookPos {
    int folder = -1;
    size_t index = 0;
    std::string look;
    uint32_t key = 0;  // the helmet the row names, (id << 16) | 1
    int64_t stat = -1;  // the row's stat_id
};
std::map<std::string, LookPos> g_shared_look;
// Every look row: its label, row path and the helmet it names ((id << 16) | 1, 0 when unknown), to find how the
// in-memory look records name their rows.
struct LookRowInfo {
    std::string label, path;
    uint32_t key = 0;
};
std::vector<LookRowInfo> g_look_rows;
bool g_maps = false;

bool db_int(const std::string &key, int64_t &out) { return db::int_at(key, &out) == db::Result::ok; }

bool db_text(const std::string &key, std::string &out)
{
    const char *s = nullptr;
    if (db::string_at(key, &s) != db::Result::ok || !s)
        return false;
    size_t n = strnlen(s, 128);
    if (!n || n >= 128)
        return false;
    out.assign(s, n);
    return true;
}

// Calls f(row path, label) for each row of table.stem_N that has a label.
template <class F> size_t each_row(const std::string &table, const std::string &stem, F f)
{
    int64_t count = 0;
    if (!db_int(table + "._" + stem + "_instancecount_", count) || count <= 0 || count > 8192)
        return 0;
    size_t n = 0;
    for (int64_t i = 0; i < count; ++i) {
        std::string row = table + "." + stem + "_" + std::to_string(i), label;
        if (db_text(row + "._metadata_", label)) {
            f(row, label);
            n++;
        }
    }
    return n;
}

int64_t stat_of_row(const std::string &row, const char *field)
{
    int64_t v = -1;
    if (db_int(row + "." + field, v))
        return v;
    std::string s;
    if (db_text(row + "." + field, s)) {
        char *end = nullptr;
        long long x = std::strtoll(s.c_str(), &end, 10);
        if (end && *end == 0)
            return x;
    }
    return -1;
}

bool ends_with(const std::string &s, const std::string &tail)
{
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

void write_looks_cache()
{
    J::Value o = J::Value::object();
    for (auto &[driver, looks] : g_looks) {
        J::Value a = J::Value::array();
        for (auto &l : looks)
            a.push_back(l);
        o[driver] = std::move(a);
    }
    J::Value v = J::Value::object();
    v["version"] = 1;
    v["drivers"] = std::move(o);
    std::string text = J::dump(v, 1), old;
    if (storage::read("looks.json", old) == storage::Result::ok && old == text)
        return;
    storage::write("looks.json", text);
}

void build_maps()
{
    if (g_maps)
        return;
    if (!db::available()) {
        if (verbose)
            swofty::log("[ai-helmets] the game database isn't readable yet");
        return;
    }
    std::unordered_map<uint32_t, std::string> drivers_, teams_;
    std::map<std::string, uint32_t> driver_stat;
    each_row("entrants.drivers", "driver", [&](const std::string &row, const std::string &label) {
        int64_t s = stat_of_row(row, "stat_id");
        if (s >= 0 && s < 0x3fff) {
            drivers_.emplace(uint32_t(s), "game/" + label);
            driver_stat.emplace("game/" + label, uint32_t(s));
        }
    });
    each_row("entrants.teams", "team", [&](const std::string &row, const std::string &label) {
        int64_t s = stat_of_row(row, "stat_id");
        if (s >= 0 && s < 0x3fff)
            teams_.emplace(uint32_t(s), "game/" + label);
    });
    // looks: linked to a driver by linked_driver_stat_id, or named <year>-<team>_<driver>
    struct LookRow {
        std::string path, label, helmet, link;  // link: linked_driver_stat_id as text (a driver's label), if it is one
        int64_t linked;
        int folder;
        size_t index;
        int64_t stat;
    };
    std::vector<LookRow> rows;
    for (int folder = 0; folder < 4; ++folder)
        each_row("entrants.driver_customisations.folder_" + std::to_string(folder),
                 "preloaded_driver_customisation_properties", [&](const std::string &row, const std::string &label) {
                     LookRow r{row, label, {}, {}, stat_of_row(row, "linked_driver_stat_id"), folder,
                               size_t(std::strtoull(row.c_str() + row.rfind('_') + 1, nullptr, 10)),
                               stat_of_row(row, "stat_id")};
                     db_text(row + ".helmet", r.helmet);
                     // the field is an Id, text: read as an integer it gives a number that is no stat id (every
                     // row read 1163996856), so its text is read first
                     std::string link;
                     if (db_text(row + ".linked_driver_stat_id", link) && driver_stat.count("game/" + link)) {
                         r.link = link;
                         r.linked = -1;
                     }
                     rows.push_back(std::move(r));
                 });
    std::map<std::string, std::vector<std::string>> looks;
    for (auto &[driver, stat] : driver_stat) {
        std::string tail = "_" + driver.substr(5);
        std::vector<std::string> &out = looks[driver];
        // 0.2.0 trusted linked_driver_stat_id when the row had one; it matched no driver, so the name rule
        // now counts on its own
        for (auto &r : rows)
            if (r.linked == int64_t(stat) ||
                (ends_with(r.label, tail) && r.label.find('-') < r.label.size() - tail.size()))
                out.push_back("game/" + r.label);
        if (out.empty())
            looks.erase(driver);
    }
    // Looks another addon adds, linked to their driver by his label (More Driver Models' mdm_<driver>, a season
    // pack's bdx_..._look): their helmet is the driver's own when no other look wears it. More Driver Models gives
    // its drivers copies of the free-agent look, all with one helmet: those are changed in their own record only.
    std::map<std::string, std::set<std::string>> wearers;  // helmet label -> the drivers (or unlinked looks) wearing it
    for (auto &r : rows)
        if (!r.helmet.empty())
            wearers[r.helmet].insert(!r.link.empty() ? "game/" + r.link : "look:" + r.label);
    std::map<std::string, LookPos> shared_look;
    std::map<std::string, std::string> shared_helmet;  // driver -> the helmet label of his shared look
    size_t linked_looks = 0;
    for (auto &r : rows) {
        if (r.link.empty() || r.helmet.empty())
            continue;
        std::string driver = "game/" + r.link;
        bool alone = wearers[r.helmet].size() == 1;
        if (alone) {
            auto &out = looks[driver];
            std::string ref = "game/" + r.helmet;
            if (std::find(out.begin(), out.end(), ref) == out.end()) {
                out.push_back(ref);
                linked_looks++;
            }
        }
        else if (!shared_look.count(driver)) {
            shared_look[driver] = {r.folder, r.index, r.label, 0, r.stat};
            shared_helmet[driver] = r.helmet;
        }
    }
    std::vector<std::string> helmets;
    std::map<std::string, uint32_t> helmet_ids;
    each_row("customisation.custom_avatar.helmets", "Helmet", [&](const std::string &row, const std::string &label) {
        helmets.push_back("game/" + label);
        int64_t id = stat_of_row(row, "id");
        if (id > 0 && id < 0xffff)
            helmet_ids["game/" + label] = uint32_t(id);
    });
    for (auto &[d, pos] : shared_look)
        if (auto h = helmet_ids.find("game/" + shared_helmet[d]); h != helmet_ids.end())
            pos.key = (h->second << 16) | 1u;
    std::vector<LookRowInfo> look_rows;
    for (auto &r : rows) {
        uint32_t key = 0;
        if (auto h = helmet_ids.find("game/" + r.helmet); h != helmet_ids.end())
            key = (h->second << 16) | 1u;
        look_rows.push_back({r.label, r.path, key});
    }
    if (!shared_look.empty() || linked_looks)
        swofty::log("[ai-helmets] %zu addon look(s) with a helmet of their driver's own; %zu driver(s) whose look shares "
                    "its helmet (changed in their own look record)",
                    linked_looks, shared_look.size());
    g_helmet_id = std::move(helmet_ids);

    if (drivers_.empty() || teams_.empty()) {
        swofty::log("[ai-helmets] game database: %zu driver(s), %zu team(s) read; will retry at the next Team HQ",
                    drivers_.size(), teams_.size());
        return;
    }
    g_driver_of_stat = std::move(drivers_);
    g_team_of_stat = std::move(teams_);
    g_shared_look = std::move(shared_look);
    g_look_rows = std::move(look_rows);
    g_looks = std::move(looks);
    g_helmet_rows = std::move(helmets);
    g_maps = true;
    size_t with_looks = 0, n_looks = 0;
    for (auto &[d, l] : g_looks) {
        with_looks += !l.empty();
        n_looks += l.size();
    }
    swofty::log("[ai-helmets] game database: %zu driver(s), %zu team(s), %zu look row(s) (%zu on %zu driver(s)), %zu helmet row(s)",
                g_driver_of_stat.size(), g_team_of_stat.size(), rows.size(), n_looks, with_looks, g_helmet_rows.size());
    for (size_t i = 0; i < rows.size() && i < 3; i++)
        swofty::log("[ai-helmets]   look row %s: linked_driver_stat_id %lld", rows[i].label.c_str(), (long long)rows[i].linked);
    for (const char *d : {"game/lando_norris", "game/lewis_hamilton"})
        if (auto it = g_looks.find(d); it != g_looks.end())
            swofty::log("[ai-helmets]   %s: %zu look(s), first %s", d, it->second.size(), it->second.front().c_str());
    // addon drivers: identity -> stat id (his row) -> look -> helmet
    g_addon_helmet.clear();
    for (auto &id : drivers::identities.all()) {
        std::string ref(id.id());
        if (ref.rfind("game/", 0) == 0)
            continue;
        auto stat = drivers::identities.stat_id(id);
        auto rec = drivers::identities.get(id);
        if (!stat || !rec || rec->look.empty())
            continue;
        auto d = g_driver_of_stat.find(uint32_t(*stat));
        auto look = drivers::looks.get(rec->look);
        if (d == g_driver_of_stat.end() || !look || look->helmet.empty() || g_looks.count(d->second))
            continue;
        g_addon_helmet[d->second] = std::string(look->helmet.id());
    }
    if (!g_addon_helmet.empty())
        swofty::log("[ai-helmets] %zu driver(s) from other addons, with their helmets from their looks", g_addon_helmet.size());
    write_looks_cache();
}

std::string team_key_of(const std::string &team_ref)
{
    auto slash = team_ref.find('/');
    return slash == std::string::npos ? team_ref : team_ref.substr(slash + 1);
}

// A team key without its season and maker prefix: "gm_cadillac_26" -> "cadillac", "ferrari" -> "ferrari".
std::string team_core(std::string key)
{
    key = team_key_of(key);
    auto us = key.rfind('_');
    if (us != std::string::npos && us + 1 < key.size() &&
        std::all_of(key.begin() + us + 1, key.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
        key.resize(us);
    if (key.rfind("gm_", 0) == 0)
        key = key.substr(3);
    return key;
}

// A driver who starts every career as a free agent: one whose only looks are F2 ones ("2025-f2-camposracing_pepe_marti"),
// or one Driver Manager made. At any F1 team he has joined from the free agents.
bool always_newcomer(const std::string &driver)
{
    if (g_config.addon_drivers.count(driver))
        return true;
    auto it = g_looks.find(driver);
    if (it == g_looks.end() || it->second.empty())
        return false;
    for (auto &ref : it->second) {
        std::string l = ref.substr(ref.find('/') + 1);
        if (!(l.size() > 8 && std::isdigit(static_cast<unsigned char>(l[0])) && l.compare(4, 4, "-f2-") == 0))
            return false;
    }
    return true;
}

bool same_team(const std::string &team_key, const std::string &home);

// A driver's plain team rows of one season ("2025-tororosso_liam_lawson" -> "tororosso"), each with how many of
// his rows that season are that team's, its special liveries included ("tororossomiami", "redbulljapan").
std::map<std::string, int> season_rows(const std::vector<std::string> &looks, const std::string &driver,
                                       const std::string &year)
{
    std::string tail = "_" + driver.substr(driver.find('/') + 1);
    std::vector<std::string> codes;
    for (auto &ref : looks) {
        std::string l = ref.substr(ref.find('/') + 1);
        if (l.size() < 6 || l.compare(0, 4, year) != 0 || l[4] != '-' || !ends_with(l, tail))
            continue;
        std::string code = l.substr(5, l.size() - 5 - tail.size());
        if (!code.empty() && code.find('-') == std::string::npos)
            codes.push_back(code);
    }
    std::map<std::string, int> out;
    for (auto &c : codes) {
        // a plain row is one no shorter row of his is a prefix of ("redbulljapan" is a Red Bull livery)
        bool livery = false;
        for (auto &o : codes)
            if (o.size() < c.size() && c.compare(0, o.size(), o) == 0)
                livery = true;
        if (!livery)
            out[c] = 0;
    }
    for (auto &c : codes)
        for (auto &[team, n] : out)
            if (c.compare(0, team.size(), team) == 0)
                n++;
    return out;
}

// His team of one season: the one with the most rows, liveries included. Liam Lawson has 2025 rows for Red Bull
// (where he started 2025) and Racing Bulls (where he raced after, with its Miami and Silverstone liveries), so
// Racing Bulls; Yuki Tsunoda has the reverse, with Red Bull's Japan livery. A tie goes to the shorter name.
std::string season_team_of(const std::vector<std::string> &looks, const std::string &driver, const std::string &year)
{
    std::string best;
    int most = 0;
    for (auto &[team, n] : season_rows(looks, driver, year))
        if (n > most || (n == most && team.size() < best.size())) {
            best = team;
            most = n;
        }
    return best;
}

// The team a driver's own helmet belongs to, from his look rows ("2026-mclaren_lando_norris" -> "mclaren"):
// the season of the team he races for now (a _26 key is 2026, else 2025), else his newest. F2 rows
// ("2024-f2-...") are skipped. A season row for the team he races for wins (Lawson is at home at Red Bull and
// at Racing Bulls in 2025); else the season's team above. Empty for a driver with no such row (rookies,
// addon drivers).
std::string home_team(const std::string &driver, const std::string &current_team)
{
    auto it = g_looks.find(driver);
    if (it == g_looks.end())
        return {};
    std::string want_year = ends_with(current_team, "_26") ? "2026" : "2025";
    auto rows = season_rows(it->second, driver, want_year);
    std::string at_team;
    if (!current_team.empty())
        for (auto &[team, n] : rows)
            if (same_team(current_team, team) && (at_team.empty() || team.size() < at_team.size()))
                at_team = team;
    if (!at_team.empty())
        return at_team;
    if (!rows.empty())
        return season_team_of(it->second, driver, want_year);
    // no row this season: his newest season's team
    std::string tail = "_" + driver.substr(driver.find('/') + 1), newest;
    for (auto &ref : it->second) {
        std::string l = ref.substr(ref.find('/') + 1);
        if (l.size() >= 6 && l[4] == '-' && std::isdigit(static_cast<unsigned char>(l[0])) && ends_with(l, tail) &&
            l.substr(0, 4) > newest && !season_rows(it->second, driver, l.substr(0, 4)).empty())
            newest = l.substr(0, 4);
    }
    return newest.empty() ? std::string() : season_team_of(it->second, driver, newest);
}

// Does a team key belong to the team a look names? ("williams_26" and "williams"; "gm_cadillac_26" and "cadillac")
bool same_team(const std::string &team_key, const std::string &home)
{
    std::string core = team_core(team_key);
    return core == home || (!core.empty() && home.rfind(core, 0) == 0) || (!home.empty() && core.rfind(home, 0) == 0);
}

// Every look a driver can wear: his rows in the database, else the look his identity record names (an
// addon's driver).
std::vector<Ref<drivers::Look>> looks_of(const std::string &driver)
{
    std::vector<Ref<drivers::Look>> out;
    auto it = g_looks.find(driver);
    if (it != g_looks.end())
        for (auto &l : it->second)
            out.push_back(drivers::looks[l]);
    if (out.empty())
        if (auto id = drivers::identities.get(drivers::identities[driver]); id && !id->look.empty())
            out.push_back(id->look);
    return out;
}

// ---------------------------------------------------------------------------------------------------------
// Per-career state: each driver's original team (the first team we saw him race for in this career) and the
// layers applied this session.

std::string g_career;
std::map<std::string, std::string> g_original;  // driver ref -> team key
std::map<std::string, std::string> g_applied;   // driver ref -> team look he was put in (saved with the career)
std::set<std::string> g_put_this_run;           // drivers set_look reached since this career loaded
std::map<std::string, std::string> g_team_looks; // team key -> 0.3.0's unlinked team look (kept, unused)
std::map<std::pair<std::string, std::string>, std::string> g_driver_looks;  // (driver, team) -> his look there
std::set<std::string> g_no_look_logged;
bool g_loaded_state = false;

std::string state_path(const std::string &career)
{
    std::string safe;
    for (char ch : career)
        safe += (std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_') ? ch : '_';
    return "careers/" + safe + ".json";
}

void load_state()
{
    g_original.clear();
    g_loaded_state = true;
    J::Value v;
    if (auto stored = career::saves::data(kAddon).get(g_career))
        v = stored->value;
    else {
        std::string text;
        if (storage::read(state_path(g_career), text) == storage::Result::ok)
            v = J::parse(text);
    }
    if (auto *o = v["original"].if_object())
        for (auto &[d, t] : *o)
            g_original[d] = str(t);
    g_applied.clear();
    g_put_this_run.clear();
    if (auto *o = v["looks"].if_object())
        for (auto &[d, l] : *o)
            if (l.is_string() && !l.as_string().empty())
                g_applied[d] = str(l);
    swofty::log("[ai-helmets] career %s: %zu driver(s) with a known home team", g_career.c_str(), g_original.size());
}

void save_state()
{
    J::Value v = J::Value::object();
    J::Value o = J::Value::object();
    for (auto &[d, t] : g_original)
        o[d] = t;
    v["original"] = std::move(o);
    J::Value looks = J::Value::object();
    for (auto &[d, l] : g_applied)
        looks[d] = l;
    v["looks"] = std::move(looks);
    if (career::saves::data(kAddon).set(g_career, v) != career::Result::ok)
        storage::write(state_path(g_career), J::dump(v));
}

// 0.2.x put career-scope layers on the driver's looks at Team HQ. The core refuses any driver.look layer after
// boot ("driver.look applies at boot"), so nothing ever changed. 0.3.0 registers one look per team at boot
// (the team's placeholder helmet with that team's own suit and gloves) and puts a moved driver into it with
// drivers::set_look, a career operation for the loaded career.

// His own look this season: "game/<year>-<home>_<driver>", the same row home_team() picks.
std::string own_look(const std::string &driver, const std::string &current_team)
{
    std::string home = home_team(driver, current_team);
    auto it = g_looks.find(driver);
    if (home.empty() || it == g_looks.end())
        return {};
    std::string tail = "-" + home + "_" + driver.substr(driver.find('/') + 1), want_year = ends_with(current_team, "_26") ? "2026" : "2025";
    std::string best;
    for (auto &ref : it->second) {
        std::string l = ref.substr(ref.find('/') + 1);
        if (l.size() > 4 && l.compare(4, std::string::npos, tail) == 0) {
            if (l.compare(0, 4, want_year) == 0)
                return ref;
            if (ref > best)
                best = ref;
        }
    }
    return best;
}

bool put_look(const std::string &driver, const std::string &look)
{
    auto r = drivers::set_look(drivers::identities[driver], drivers::looks[look]);
    if (r != Result::ok)
        swofty::log("[ai-helmets] set_look %s -> %s: %s (%s)", driver.c_str(), look.c_str(), result_name(r),
                    last_error().c_str());
    return r == Result::ok;
}

// ---------------------------------------------------------------------------------------------------------
// Runtime helmets: the look records in memory.
//
// Looks can only be added or layered at boot, and switching a driver into a look of ours crashed (0.3.0) or hung
// (0.3.1) the game. 0.4.0 changes the helmet inside the look records the game already has, at runtime, the way
// Helmet Exchange (MIT, giggles) changes the player's helmet: the EntrantsSystem (found in the game manager's
// systems map under k_Live_EntrantsSystemKey) holds 4 lists of look records at +0x298 (one per 0x10); each list
// is a begin/end pair at +0x8/+0x10 of 16-byte entries whose +0x8 is a record; a record keeps its helmet as the
// dword (helmet id << 16) | 1 within its first 0x400 bytes. A driver's own helmets are only in his own looks,
// so every dword holding one of them is set to the new helmet, and put back when he goes home. These records
// live as long as the game, so the patches are checked and re-made at every Team HQ and session start.

constexpr uintptr_t kMgrSystems = 0x30, kMapSlots = 0x18, kMapInfo = 0x20;
constexpr size_t kMapMax = 4096;
constexpr uintptr_t kLookLists = 0x298, kListBegin = 0x8, kListEnd = 0x10, kEntryRecord = 0x8;
constexpr size_t kLookTypes = 4, kListStride = 0x10, kEntrySize = 0x10, kMaxEntries = 0x1000, kRecordScan = 0x400;
constexpr size_t kHelmetField = 0x18;  // the look record's helmet, (id << 16) | 1

uint32_t helmet_key(uint32_t id) { return (id << 16) | 1u; }

bool plausible(uintptr_t p) { return p >= 0x10000 && !(p & 7); }

bool writable(uintptr_t p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<void *>(p), &mbi, sizeof mbi) || mbi.State != MEM_COMMIT ||
        (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
        return false;
    DWORD pr = mbi.Protect & 0xff;
    if (pr != PAGE_READWRITE && pr != PAGE_WRITECOPY && pr != PAGE_EXECUTE_READWRITE && pr != PAGE_EXECUTE_WRITECOPY)
        return false;
    return p + n <= reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
}

uintptr_t entrants_system()
{
    static uintptr_t gm = 0, key = 0;
    static bool tried = false;
    if (!tried) {
        tried = true;
        if (swofty::api && swofty::api->resolve) {
            gm = swofty::api->resolve("g_Game_Manager");
            key = swofty::api->resolve("k_Live_EntrantsSystemKey");
        }
        if (!gm || !key)
            swofty::log("[ai-helmets] this game build doesn't resolve g_Game_Manager/k_Live_EntrantsSystemKey: helmets "
                        "can't change at runtime");
    }
    if (!gm || !key)
        return 0;
    uintptr_t mgr = 0, map = 0, base = 0, info = 0;
    if (!mem::read(gm, mgr) || !plausible(mgr) || !mem::read(mgr + kMgrSystems, map) || !plausible(map) ||
        !mem::read(map + kMapSlots, base) || !mem::read(map + kMapInfo, info))
        return 0;
    if (!plausible(base) || info <= base || (info - base) % 16 || (info - base) / 16 > kMapMax)
        return 0;
    size_t n = (info - base) / 16;
    if (!mem::readable(base, n * 16) || !mem::readable(info, n))
        return 0;
    for (size_t i = 0; i < n; i++) {
        uintptr_t kv[2];
        std::memcpy(kv, reinterpret_cast<const void *>(base + i * 16), sizeof kv);
        uint8_t used = *reinterpret_cast<const uint8_t *>(info + i);
        if (kv[0] == key && plausible(kv[1]) && used)
            return kv[1];
    }
    return 0;
}

// Calls f(record) for each look record; returns how many were readable.
template <class F> size_t each_look_record(uintptr_t es, F f)
{
    size_t n = 0;
    for (size_t t = 0; t < kLookTypes; t++) {
        uintptr_t list = 0, begin = 0, end = 0;
        if (!mem::read(es + kLookLists + t * kListStride, list) || !plausible(list) || !mem::readable(list, 0x18) ||
            !mem::read(list + kListBegin, begin) || !mem::read(list + kListEnd, end))
            continue;
        if (!plausible(begin) || end <= begin || (end - begin) % kEntrySize || (end - begin) / kEntrySize > kMaxEntries ||
            !mem::readable(begin, end - begin))
            continue;
        for (uintptr_t e = begin; e < end; e += kEntrySize) {
            uintptr_t rec = *reinterpret_cast<const uintptr_t *>(e + kEntryRecord);
            if (!plausible(rec) || !mem::readable(rec, kRecordScan))
                continue;
            f(rec);
            n++;
        }
    }
    return n;
}

struct Patch {
    uintptr_t at;
    uint32_t from, to;
};
std::map<std::string, std::vector<Patch>> g_patches;  // driver -> the dwords we changed
std::map<std::string, std::string> g_patch_helmet;    // driver -> the helmet ref patched in
std::set<std::string> g_patch_failed;                 // failures already logged
bool g_logged_layout = false;

// The fields other drivers' picks hold now: field -> {its stock value, ours}. A field whose value the game has since
// rewritten isn't ours any more and is left out.
struct Owned {
    uint32_t from, to;
    std::string driver;
};
std::map<uintptr_t, Owned> owned_fields(const std::string &except = {})
{
    std::map<uintptr_t, Owned> m;
    for (auto &[d, ps] : g_patches) {
        if (d == except)
            continue;
        for (auto &p : ps) {
            uint32_t cur = 0;
            if (mem::read(p.at, cur) && cur == p.to)
                m[p.at] = {p.from, p.to, d};
        }
    }
    return m;
}

void unpatch(const std::string &driver)
{
    auto it = g_patches.find(driver);
    if (it == g_patches.end())
        return;
    for (auto &p : it->second) {
        uint32_t cur = 0;
        if (mem::read(p.at, cur) && cur == p.to && writable(p.at, 4))
            *reinterpret_cast<volatile uint32_t *>(p.at) = p.from;
    }
    g_patches.erase(it);
    g_patch_helmet.erase(driver);
}

void unpatch_all()
{
    while (!g_patches.empty())
        unpatch(g_patches.begin()->first);
    g_patch_helmet.clear();
}

// The item id of a helmet ref: a game row's from the database, an addon helmet's from the registry.
uint32_t helmet_id_of(const std::string &ref)
{
    if (ref.rfind("game/", 0) == 0) {
        auto it = g_helmet_id.find(ref);
        return it == g_helmet_id.end() ? 0 : it->second;
    }
    auto s = wearables::helmets.stat_id(wearables::helmets[ref]);
    return s && *s > 0 && *s < 0xffff ? uint32_t(*s) : 0;
}

// A driver's own helmets: the helmet rows named like his look rows ("2026-mclaren_lando_norris").
// How many look records hold this helmet now (one record per look row).
size_t records_holding(uint32_t id)
{
    uintptr_t es = entrants_system();
    if (!es)
        return 0;
    uint32_t key = helmet_key(id);
    size_t n = 0;
    // a field another driver's pick holds counts as its stock helmet, not as the pick
    auto owned = owned_fields();
    each_look_record(es, [&](uintptr_t rec) {
        auto o = owned.find(rec + kHelmetField);
        n += (o != owned.end() ? o->second.from : *reinterpret_cast<const uint32_t *>(rec + kHelmetField)) == key;
    });
    return n;
}

// A driver from another addon wears the helmet of his look: from his identity (an addon that registers drivers),
// or from the launcher page (Driver Manager writes database rows). It is changed the same way as anyone's (every
// record holding it), so it is taken as his only when his look is the one record holding it: a stock design
// another look also wears, another driver's helmet, or a helmet this addon puts on drivers is refused. Call with
// his own patch taken off. why_not says why.
uint32_t addon_own_helmet(const std::string &driver, std::string &why_not)
{
    std::string ref;
    if (auto it = g_addon_helmet.find(driver); it != g_addon_helmet.end())
        ref = it->second;
    else if (auto c = g_config.addon_drivers.find(driver); c != g_config.addon_drivers.end())
        ref = c->second;
    if (ref.empty())
        return 0;
    uint32_t id = helmet_id_of(ref);
    if (!id) {
        why_not = "his look's helmet " + ref + " has no item id";
        return 0;
    }
    for (auto &[d, looks] : g_looks)
        for (auto &l : looks)
            if (auto h = g_helmet_id.find(l); h != g_helmet_id.end() && h->second == id) {
                why_not = "he wears " + d + "'s helmet";
                return 0;
            }
    for (auto *m : {&g_addon_helmet, &g_config.addon_drivers})
        for (auto &[d, h] : *m)
            if (d != driver && helmet_id_of(h) == id) {
                why_not = "he shares his helmet (" + ref + ") with " + d;
                return 0;
            }
    std::set<std::string> ours{g_config.base};
    for (auto &[k, v] : g_config.team_helmet)
        ours.insert(v);
    for (auto &[k, v] : g_config.picks)
        ours.insert(v);
    for (auto &[k, v] : g_config.drivers)
        ours.insert(v);
    for (auto &r : ours)
        if (!r.empty() && r != "own" && helmet_id_of(r) == id) {
            why_not = "his helmet (" + ref + ") is one AI Helmets puts on other drivers";
            return 0;
        }
    size_t held = records_holding(id);
    if (held != 1) {
        why_not = held ? "his helmet (" + ref + ") is in " + std::to_string(held) +
                             " looks, so other drivers wear it too: give him a helmet of his own in Driver Manager"
                       : "his helmet (" + ref + ") is in none of the look records";
        return 0;
    }
    return id;
}

std::set<uint32_t> own_helmet_ids(const std::string &driver, std::string *why_not = nullptr)
{
    std::set<uint32_t> ids;
    auto it = g_looks.find(driver);
    if (it != g_looks.end())
        for (auto &look : it->second)
            if (auto h = g_helmet_id.find(look); h != g_helmet_id.end())
                ids.insert(h->second);
    if (ids.empty()) {
        std::string why;
        if (uint32_t id = addon_own_helmet(driver, why))
            ids.insert(id);
        else if (why_not && !why.empty())
            *why_not = why;
    }
    return ids;
}

// The look record of one look row. The EntrantsSystem's look lists (16-byte entries, +0x8 the record) don't follow
// the database's folders, and the records hold no stat id, so how a record names its row is found from the rows:
// a pointer to the row's label, the label inline, or a hash of the label or of the row path (FNV-1a 32/64, djb2),
// at one place in the record's first 0x200 bytes. A place is taken when it names many rows and the records it names
// carry those rows' helmets, nearly all of them. Rows that wear one helmet (More Driver Models' 67 copies of the
// free-agent look) can't be told apart by helmet, so nothing is changed without it. Returns 0, with why, when no
// place is found or his record holds another helmet than its row names.
uint32_t fnv32(const std::string &s)
{
    uint32_t h = 2166136261u;
    for (unsigned char c : s)
        h = (h ^ c) * 16777619u;
    return h;
}
uint64_t fnv64(const std::string &s)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char c : s)
        h = (h ^ c) * 0x100000001b3ull;
    return h;
}
uint32_t djb2(const std::string &s)
{
    uint32_t h = 5381;
    for (unsigned char c : s)
        h = h * 33 + c;
    return h;
}

// kinds of names: 0-5 are dwords (fnv32/fnv64 low/fnv64 high/djb2 of the label; fnv32/fnv64 low of the path),
// 6-7 qwords (fnv64 of the label, of the path), 8 a pointer to the label, 9 the label inline
constexpr int kNameKinds = 10;
const char *kNameKind[kNameKinds] = {"fnv32(label)", "fnv64(label) low", "fnv64(label) high", "djb2(label)",
                                     "fnv32(path)", "fnv64(path) low", "fnv64(label)", "fnv64(path)",
                                     "pointer to the label", "the label inline"};
struct NamePlace {
    int kind = -1;
    size_t off = 0;
};
NamePlace g_name_place;  // found once, then checked every time

// The row (index into g_look_rows) a record names at place w, or -1.
int64_t row_named(uintptr_t rec, const NamePlace &w, const std::vector<std::unordered_map<uint64_t, size_t>> &by_hash,
                  const std::unordered_map<std::string, size_t> &by_label)
{
    if (w.kind < 8) {
        uint64_t v = w.kind >= 6 ? *reinterpret_cast<const uint64_t *>(rec + w.off)
                                 : *reinterpret_cast<const uint32_t *>(rec + w.off);
        auto it = by_hash[size_t(w.kind)].find(v);
        return it == by_hash[size_t(w.kind)].end() ? -1 : int64_t(it->second);
    }
    const char *str = nullptr;
    if (w.kind == 8) {
        uintptr_t q = *reinterpret_cast<const uintptr_t *>(rec + w.off);
        if (!plausible(q) || !mem::readable(q, 64))
            return -1;
        str = reinterpret_cast<const char *>(q);
    } else {
        str = reinterpret_cast<const char *>(rec + w.off);
    }
    size_t n = strnlen(str, 64);
    if (n < 3 || n >= 64)
        return -1;
    auto it = by_label.find(std::string(str, n));
    return it == by_label.end() ? -1 : int64_t(it->second);
}

uintptr_t shared_look_record(uintptr_t es, const LookPos &pos, std::string &why)
{
    if (!pos.key) {
        why = "its row's helmet has no item id";
        return 0;
    }
    std::vector<std::unordered_map<uint64_t, size_t>> by_hash(8);
    std::unordered_map<std::string, size_t> by_label;
    int64_t want = -1;
    for (size_t i = 0; i < g_look_rows.size(); i++) {
        auto &r = g_look_rows[i];
        uint64_t l64 = fnv64(r.label), p64 = fnv64(r.path);
        uint64_t v[8] = {fnv32(r.label), l64 & 0xffffffffu, l64 >> 32, djb2(r.label), fnv32(r.path), p64 & 0xffffffffu,
                         l64, p64};
        for (size_t k = 0; k < 8; k++)
            by_hash[k].emplace(v[k], i);
        by_label.emplace(r.label, i);
        if (r.label == pos.look)
            want = int64_t(i);
    }
    if (want < 0) {
        why = "its row isn't in the database's looks";
        return 0;
    }
    // every look record
    std::vector<uintptr_t> recs;
    for (size_t t = 0; t < kLookTypes; t++) {
        uintptr_t list = 0, begin = 0, end = 0;
        if (!mem::read(es + kLookLists + t * kListStride, list) || !plausible(list) || !mem::readable(list, 0x18) ||
            !mem::read(list + kListBegin, begin) || !mem::read(list + kListEnd, end))
            continue;
        if (!plausible(begin) || end <= begin || (end - begin) % kEntrySize || (end - begin) / kEntrySize > kMaxEntries ||
            !mem::readable(begin, end - begin))
            continue;
        for (uintptr_t e = begin; e < end; e += kEntrySize) {
            uintptr_t rec = *reinterpret_cast<const uintptr_t *>(e + kEntryRecord);
            if (plausible(rec) && mem::readable(rec, kRecordScan))
                recs.push_back(rec);
        }
    }
    // helmets this addon has changed (and the game hasn't rewritten since) count as what they were
    std::map<uintptr_t, uint32_t> was;
    for (auto &[at, o] : owned_fields())
        was[at] = o.from;
    auto helmet_of = [&](uintptr_t rec) {
        if (auto w = was.find(rec + kHelmetField); w != was.end())
            return w->second;
        return *reinterpret_cast<const uint32_t *>(rec + kHelmetField);
    };
    // how good a place is: rows it names (once each), and of those, records carrying their row's helmet
    auto score = [&](const NamePlace &w, size_t &named, size_t &agree) {
        std::set<int64_t> rows;
        named = agree = 0;
        for (uintptr_t rec : recs) {
            int64_t r = row_named(rec, w, by_hash, by_label);
            if (r < 0 || !rows.insert(r).second)
                continue;
            named++;
            uint32_t k = g_look_rows[size_t(r)].key;
            agree += k && helmet_of(rec) == k;
        }
    };
    auto good = [](size_t named, size_t agree) { return named >= 20 && agree + std::max<size_t>(2, named / 50) >= named; };
    size_t named = 0, agree = 0;
    if (g_name_place.kind < 0 || (score(g_name_place, named, agree), !good(named, agree))) {
        g_name_place = {};
        NamePlace best;
        size_t best_named = 0, best_agree = 0;
        for (int k = 0; k < kNameKinds; k++)
            for (size_t off = 0; off + 8 <= 0x200; off += (k >= 6 ? 8 : 4)) {
                NamePlace w{k, off};
                size_t n = 0, a = 0;
                score(w, n, a);
                if (good(n, a) && n > best_named)
                    best = w, best_named = n, best_agree = a;
            }
        if (best.kind >= 0) {
            g_name_place = best;
            named = best_named, agree = best_agree;
            swofty::log("[ai-helmets] look records name their rows by %s at +0x%zx (%zu of %zu records, %zu carry "
                        "their row's helmet)",
                        kNameKind[best.kind], best.off, best_named, recs.size(), best_agree);
        }
    }
    if (g_name_place.kind < 0) {
        why = "the look records' names weren't found";
        static bool dumped = false;
        if (!dumped) {
            dumped = true;
            // a few records, for working out the layout: two that wear his row's helmet, and the first two
            std::vector<uintptr_t> show;
            for (uintptr_t rec : recs)
                if (helmet_of(rec) == pos.key && show.size() < 2)
                    show.push_back(rec);
            for (size_t i = 0; i < recs.size() && i < 2; i++)
                show.push_back(recs[i]);
            swofty::log("[ai-helmets] look records: no place names their rows (%zu records, %zu look rows); records:",
                        recs.size(), g_look_rows.size());
            for (uintptr_t rec : show) {
                std::string hex;
                for (size_t i = 0; i < 0x100; i += 8) {
                    char b[20];
                    std::snprintf(b, sizeof b, "%016llx ", (unsigned long long)*reinterpret_cast<const uint64_t *>(rec + i));
                    hex += b;
                }
                swofty::log("[ai-helmets]   %llx (helmet %08x): %s", (unsigned long long)rec, helmet_of(rec), hex.c_str());
            }
        }
        return 0;
    }
    uintptr_t found = 0;
    size_t n = 0;
    for (uintptr_t rec : recs)
        if (row_named(rec, g_name_place, by_hash, by_label) == want) {
            found = rec;
            n++;
        }
    if (n != 1) {
        why = n ? "two look records name its row" : "no look record names its row";
        return 0;
    }
    if (helmet_of(found) != pos.key) {
        why = "his look record holds another helmet than its row names";
        return 0;
    }
    if (was.count(found + kHelmetField)) {
        why = "his look record holds another driver's pick";
        return 0;
    }
    if (!writable(found + kHelmetField, 4)) {
        why = "his look record can't be written";
        return 0;
    }
    return found;
}

// Puts helmet ref on the driver in every look record that holds one of his own helmets. Returns the number of
// fields changed; 0 with why set when it couldn't.
size_t patch(const std::string &driver, const std::string &helmet, std::string &why)
{
    uintptr_t es = entrants_system();
    if (!es) {
        why = "no EntrantsSystem";
        return 0;
    }
    uint32_t to_id = helmet_id_of(helmet);
    if (!to_id) {
        why = "no item id for " + helmet;
        return 0;
    }
    // put back what we changed before, then start over (the records may have been rebuilt since); first, so an
    // addon driver's helmet is counted in the records as it is without our change
    unpatch(driver);
    std::string own_why;
    std::set<uint32_t> own = own_helmet_ids(driver, &own_why);
    auto shared = g_shared_look.find(driver);
    if (own.empty() && shared == g_shared_look.end()) {
        why = own_why.empty() ? "no helmet of his own in the database" : own_why;
        return 0;
    }
    std::set<uint32_t> keys;
    for (uint32_t id : own)
        keys.insert(helmet_key(id));
    uint32_t to = helmet_key(to_id);
    std::vector<Patch> done;
    std::set<size_t> offsets;
    // Fields another driver's pick holds are his, even when they now carry one of this driver's helmets (Colapinto
    // given Ocon's 2024 helmet): taking them made Ocon's pick land on Colapinto too, and left Colapinto with none
    // of his own helmets to find at the next refresh.
    auto owned = owned_fields(driver);
    // 0.4.0 scanned the record's first 0x400 bytes like Helmet Exchange; on the first in-game run every one of 42
    // matches was at +0x18, so only that field is written now (no coincidental match elsewhere in a record).
    size_t records = each_look_record(es, [&](uintptr_t rec) {
        for (size_t off = kHelmetField; off <= kHelmetField; off += 4) {
            uint32_t v = *reinterpret_cast<const uint32_t *>(rec + off);
            if (owned.count(rec + off))
                continue;
            if (keys.count(v) && writable(rec + off, 4)) {
                *reinterpret_cast<volatile uint32_t *>(rec + off) = to;
                done.push_back({rec + off, v, to});
                offsets.insert(off);
            }
        }
    });
    // a look whose helmet other drivers wear too (More Driver Models' mdm_<driver>): his own record only
    if (shared != g_shared_look.end()) {
        std::string rwhy;
        uintptr_t rec = shared_look_record(es, shared->second, rwhy);
        if (rec) {
            uint32_t v = *reinterpret_cast<const uint32_t *>(rec + kHelmetField);
            *reinterpret_cast<volatile uint32_t *>(rec + kHelmetField) = to;
            done.push_back({rec + kHelmetField, v, to});
            offsets.insert(kHelmetField);
        } else if (done.empty()) {
            why = "his look " + shared->second.look + " shares its helmet with other drivers, and " + rwhy;
            return 0;
        }
    }
    if (done.empty()) {
        why = "his helmet is in none of the " + std::to_string(records) + " look record(s)";
        return 0;
    }
    if (!g_logged_layout || verbose) {
        g_logged_layout = true;
        std::string offs;
        for (size_t o : offsets)
            offs += (offs.empty() ? "+0x" : ", +0x") + [&] {
                char b[16];
                std::snprintf(b, sizeof b, "%zx", o);
                return std::string(b);
            }();
        swofty::log("[ai-helmets] look records: %zu read; %s: %zu field(s) at %s", records, driver.c_str(), done.size(),
                    offs.c_str());
    }
    // a field the game rewrote since another driver's pick took it is this driver's now: drop it from that pick, so
    // putting that driver's helmet back never writes over this one
    std::set<uintptr_t> mine;
    for (auto &p : done)
        mine.insert(p.at);
    for (auto &[d, ps] : g_patches)
        if (d != driver)
            ps.erase(std::remove_if(ps.begin(), ps.end(), [&](const Patch &p) { return mine.count(p.at) != 0; }),
                     ps.end());
    g_patches[driver] = std::move(done);
    g_patch_helmet[driver] = helmet;
    return g_patches[driver].size();
}

// ---------------------------------------------------------------------------------------------------------
// The launcher page's catalogue

void write_catalogue(const std::vector<Seat> &seats, uint32_t myteam_stat, const std::vector<bool> &mine)
{
    J::Value cat = J::Value::object();
    cat["version"] = 1;
    cat["career"] = g_career;
    cat["live"] = true;  // the page shows this line-up while the game runs, the original one otherwise
    J::Value drivers = J::Value::array();
    for (auto &s : seats) {
        auto d = g_driver_of_stat.find(s.driver_stat);
        if (d == g_driver_of_stat.end())
            continue;
        auto t = s.has_team ? g_team_of_stat.find(s.team_stat) : g_team_of_stat.end();
        J::Value row = J::Value::object();
        row["id"] = d->second;
        row["team"] = t != g_team_of_stat.end() ? team_key_of(t->second) : std::string();
        row["seated"] = s.has_car && s.has_team && s.position >= 0;
        auto o = g_original.find(d->second);
        // the player's seat is listed for the page (marked, read-only there) but never changed here
        row["original"] = s.human ? row["team"] : J::Value(o != g_original.end() ? o->second : std::string());
        if (s.human)
            row["player"] = true;
        drivers.push_back(std::move(row));
    }
    cat["drivers"] = std::move(drivers);
    // MyTeam: the player's team and its drivers, for the page's MyTeam tab
    if (myteam_stat) {
        J::Value mt = J::Value::object();
        auto t = g_team_of_stat.find(myteam_stat);
        mt["career"] = g_career;
        mt["team"] = t != g_team_of_stat.end() ? team_key_of(t->second) : std::string();
        J::Value list = J::Value::array();
        for (size_t i = 0; i < seats.size(); i++) {
            auto &s = seats[i];
            auto d = g_driver_of_stat.find(s.driver_stat);
            if (!mine[i] || d == g_driver_of_stat.end())
                continue;
            J::Value row = J::Value::object();
            row["id"] = d->second;
            row["seated"] = s.has_car && s.position >= 0;
            if (s.human)
                row["player"] = true;
            list.push_back(std::move(row));
        }
        mt["drivers"] = std::move(list);
        // its own file too, which a driver career doesn't overwrite: the page's MyTeam tab shows the last one
        std::string mtext = J::dump(mt, 1);
        static std::string mlast;
        if (mtext != mlast && storage::write("myteam.json", mtext) == storage::Result::ok)
            mlast = mtext;
        cat["myteam"] = std::move(mt);
    }
    // every driver in the database, for the page's Free Agents tab
    std::vector<std::string> all;
    for (auto &[stat, ref] : g_driver_of_stat)
        all.push_back(ref);
    std::sort(all.begin(), all.end());
    J::Value all_v = J::Value::array();
    for (auto &d : all)
        all_v.push_back(d);
    cat["all_drivers"] = std::move(all_v);
    // the stock line-ups, from each driver's own helmet rows ("2026-mclaren_lando_norris" -> mclaren_26), so
    // the page shows the real grids when the game is closed (the career's line-up lacks the player's seat)
    J::Value stock = J::Value::object();
    for (const char *year : {"2026", "2025"}) {
        J::Value teams = J::Value::object();
        for (auto &[driver, looks] : g_looks) {
            std::string best = season_team_of(looks, driver, year);
            if (best.empty())
                continue;
            std::string key = (best == "cadillac" ? std::string("gm_cadillac") : best) + (std::string(year) == "2026" ? "_26" : "");
            if (!teams[key].is_array())
                teams[key] = J::Value::array();
            teams[key].push_back(driver);
        }
        stock[year] = std::move(teams);
    }
    cat["stock"] = std::move(stock);
    // the game's helmet rows, then addon helmets (not ours: the page lists those itself)
    J::Value helmets = J::Value::array();
    std::set<std::string> seen;
    for (auto &id : g_helmet_rows)
        if (seen.insert(id).second)
            helmets.push_back(id);
    std::string own = std::string(kAddon) + "/";
    for (auto &h : wearables::helmets.all()) {
        std::string id(h.id());
        if (id.rfind("game/", 0) != 0 && id.rfind(own, 0) != 0 && seen.insert(id).second)
            helmets.push_back(id);
    }
    cat["helmets"] = std::move(helmets);
    std::string text = J::dump(cat, 1);
    static std::string last;
    if (text != last && storage::write("catalogue.json", text) == storage::Result::ok)
        last = text;
}

// MyTeam's seats among those with its team stat. In a 2025-season MyTeam the player's team shares a 2025 team
// row's stat (Red Bull's, Ferrari's...), so that team's drivers and reserves match it too: two seats hold each
// team position. They are told apart by car (the player's seat's vehicle) when that splits them, else by
// taking, for each team position, the seat nearest the player's in the entrant list (the game lists MyTeam's
// entrants after the real team's). how says which, empty when the team stat is MyTeam's alone.
std::vector<bool> myteam_seats(const std::vector<Seat> &seats, uint32_t myteam, std::string &how)
{
    how.clear();
    std::vector<bool> in(seats.size(), false);
    if (!myteam)
        return in;
    auto seated = [&](size_t i) { return seats[i].has_car && seats[i].position >= 0; };
    std::vector<size_t> group;
    std::set<int> positions;
    bool shared = false;
    size_t human = seats.size();
    for (size_t i = 0; i < seats.size(); i++) {
        if (!seats[i].has_team || seats[i].team_stat != myteam)
            continue;
        group.push_back(i);
        if (seats[i].human && human == seats.size())
            human = i;
        if (seated(i) && !positions.insert(seats[i].position).second)
            shared = true;
    }
    if (!shared || human == seats.size()) {
        for (size_t i : group)
            in[i] = true;
        if (shared)
            how = "shared with another team, and no player seat to tell them apart";
        return in;
    }
    size_t n_seated = 0;
    std::vector<size_t> same_car;
    for (size_t i : group)
        if (seated(i)) {
            n_seated++;
            if (seats[i].vehicle == seats[human].vehicle)
                same_car.push_back(i);
        }
    if (same_car.size() >= 2 && same_car.size() < n_seated) {
        for (size_t i : same_car)
            in[i] = true;
        how = "shared with another team, told apart by car";
        return in;
    }
    std::map<int, size_t> best;
    auto dist = [&](size_t i) { return i > human ? i - human : human - i; };
    for (size_t i : group)
        if (seated(i)) {
            auto b = best.find(seats[i].position);
            if (b == best.end() || dist(i) < dist(b->second))
                best[seats[i].position] = i;
        }
    for (auto &[pos, i] : best)
        in[i] = true;
    how = "shared with another team, told apart by entrant order";
    return in;
}

// ---------------------------------------------------------------------------------------------------------
// Team HQ: compare the line-up with each driver's original team and put the placeholders on.

void refresh(const char *why)
{
    std::lock_guard<std::mutex> l(g_lock);
    std::string career_id = career::saves::current();
    if (career_id.empty()) {
        career::State st{};
        if (career::state(&st) == career::Result::ok)
            career_id = st.key;
    }
    if (career_id.empty())
        return;
    if (career_id != g_career) {
        unpatch_all();
        g_applied.clear();
        g_career = career_id;
        g_loaded_state = false;
    }
    if (!g_loaded_state)
        load_state();
    reload_config();
    build_maps();
    if (!g_maps)
        return;

    std::vector<Seat> seats;
    if (!read_lineup(seats)) {
        if (verbose)
            swofty::log("[ai-helmets] %s: line-up not readable yet", why);
        return;
    }

    // MyTeam: the player's team (the core's player_team, as a stat id or a (stat << 16) | 1 key; else the human
    // seat's team). Both its seats are handled, the one the player drives included.
    uint32_t myteam = 0;
    std::vector<bool> mine(seats.size(), false);  // the seats that are MyTeam's
    std::string how;
    {
        career::State st{};
        if (career::state(&st) == career::Result::ok && career::mode(st) == career::Mode::my_team) {
            uint32_t cand[] = {st.player_team, st.player_team >> 16};
            for (uint32_t c : cand)
                for (auto &s : seats)
                    if (!myteam && c && s.has_team && s.team_stat == c)
                        myteam = c;
            if (!myteam)
                for (auto &s : seats)
                    if (!myteam && s.human && s.has_team)
                        myteam = s.team_stat;
            mine = myteam_seats(seats, myteam, how);
            static std::string logged;
            if (logged != g_career) {
                logged = g_career;
                auto t = g_team_of_stat.find(myteam);
                std::string who, others;
                for (size_t i = 0; i < seats.size(); i++) {
                    auto &s = seats[i];
                    if (!myteam || !s.has_team || s.team_stat != myteam)
                        continue;
                    auto d = g_driver_of_stat.find(s.driver_stat);
                    char car[64];
                    std::snprintf(car, sizeof car, " #%zu car 0x%08x", i, s.vehicle);
                    std::string &list = mine[i] ? who : others;
                    list += (list.empty() ? "" : ", ") +
                            (d != g_driver_of_stat.end() ? d->second : "stat " + std::to_string(s.driver_stat)) +
                            (s.human ? " (player)" : "") +
                            (s.has_car && s.position >= 0 ? " [seat " + std::to_string(s.position) + "]" : " [no race seat]") +
                            car;
                }
                swofty::log("[ai-helmets] MyTeam career: player_team %u, team stat %u (%s%s%s); drivers: %s%s%s", st.player_team,
                            myteam, t != g_team_of_stat.end() ? t->second.c_str() : "no team row", how.empty() ? "" : ", ",
                            how.c_str(), who.empty() ? "none found" : who.c_str(),
                            others.empty() ? "" : "; the other team's: ", others.c_str());
            }
        }
    }

    bool dirty = false;
    size_t movers = 0, changed = 0;
    std::set<std::string> seen;
    std::map<std::string, std::string> wanted, teams_of;  // seated AI driver -> helmet he should wear ("" = own)
    // A driver with no F1 helmet of his own (an F2 driver, a reserve, an addon's rookie) seen for the first time
    // after the career's first check joined from the free agents: he wears the team's newcomer helmet. At the
    // first check every seated driver counts as at home, so an addon's own grid drivers keep their helmets.
    bool career_started = !g_original.empty();
    for (size_t i = 0; i < seats.size(); i++) {
        auto &s = seats[i];
        if (mine[i]) {
            // a MyTeam driver, the one the player drives included: his MyTeam pick, else his own helmet
            auto d = g_driver_of_stat.find(s.driver_stat);
            if (d == g_driver_of_stat.end() || !s.has_car || s.position < 0)
                continue;
            seen.insert(d->second);
            std::string want = want_for_myteam(d->second);
            wanted[d->second] = want;
            if (!want.empty())
                teams_of[d->second] = std::string("your MyTeam team");
            continue;
        }
        if (s.human || !s.has_car || !s.has_team || s.position < 0)
            continue;  // the player, reserves and free agents
        auto d = g_driver_of_stat.find(s.driver_stat);
        auto t = g_team_of_stat.find(s.team_stat);
        if (d == g_driver_of_stat.end() || t == g_team_of_stat.end())
            continue;
        const std::string &driver = d->second;
        std::string team = team_key_of(t->second);
        seen.insert(driver);
        // His home team is the one his own helmet is made for. 0.2.0 took the first team it saw him at, so a
        // driver moved before the addon's first Team HQ (at career start, by an editor) never counted as moved.
        // The first team seen is the fallback for drivers without a dated look (rookies, addon drivers).
        std::string home = home_team(driver, team);
        bool moved;
        auto it = g_original.find(driver);
        if (!home.empty()) {
            moved = !same_team(team, home);
            // as a team key of the current season, for the page ("mclaren" -> "mclaren_26" in 2026)
            std::string shown = team;
            if (moved) {
                shown = (home == "cadillac" ? std::string("gm_cadillac") : home);
                if (ends_with(team, "_26"))
                    shown += "_26";
            }
            if (it == g_original.end() || it->second != shown) {
                it = g_original.insert_or_assign(driver, shown).first;
                dirty = true;
            }
        } else if (always_newcomer(driver)) {
            // an F2 driver or a Driver Manager driver is never at home at an F1 team. 1.0.0 took the team of the
            // career's first check as his home, so one put in a seat before it (by Career Editor) kept his helmet
            if (it == g_original.end() || !it->second.empty()) {
                it = g_original.insert_or_assign(driver, std::string()).first;
                dirty = true;
            }
            moved = true;
        } else {
            if (it == g_original.end()) {
                it = g_original.emplace(driver, career_started ? std::string() : team).first;
                dirty = true;
            }
            moved = it->second != team;
        }

        // want: the helmet he should wear now ("" = his own): his manual pick, else his new team's placeholder
        std::string want = want_for(driver, team, moved);
        movers += moved;
        if (verbose)
            swofty::log("[ai-helmets]   %s: %s, home %s%s", driver.c_str(), team.c_str(), it->second.c_str(),
                        want.empty() ? "" : (" -> " + want).c_str());
        wanted[driver] = want;
        if (!want.empty())
            teams_of[driver] = team + " (home " + it->second + ")";
    }
    // The records live for the whole game: re-make every patch (a rebuilt record loses ours), and put back the
    // helmets of drivers who went home or left the grid.
    for (auto it = g_patch_helmet.begin(); it != g_patch_helmet.end();) {
        auto w = wanted.find(it->first);
        if (w == wanted.end() || w->second.empty()) {
            std::string driver = it->first;
            ++it;
            unpatch(driver);
            swofty::log("[ai-helmets] %s: own helmet again", driver.c_str());
            changed++;
        } else
            ++it;
    }
    for (auto &[driver, want] : wanted) {
        if (want.empty())
            continue;
        auto prev = g_patch_helmet.find(driver);
        bool fresh = prev == g_patch_helmet.end() || prev->second != want;
        std::string why_not;
        size_t n = patch(driver, want, why_not);
        if (!n) {
            if (g_patch_failed.insert(driver + ">" + want + ":" + why_not).second)
                swofty::log("[ai-helmets] %s -> %s: not applied (%s)", driver.c_str(), want.c_str(), why_not.c_str());
            continue;
        }
        if (fresh) {
            swofty::log("[ai-helmets] %s races for %s: wears %s (%zu field(s))", driver.c_str(), teams_of[driver].c_str(),
                        want.c_str(), n);
            changed++;
        }
    }
    if (dirty)
        save_state();
    write_catalogue(seats, myteam, mine);
    if (verbose || changed)
        swofty::log("[ai-helmets] %s: %zu seat(s) read, %zu driver(s) away from their original team, %zu change(s)",
                    why, seats.size(), movers, changed);
}

void on_hub(const SwoftyEvent *, void *) { refresh("Team HQ"); }

// The game picks the helmet asset groups to load from the look records when a session loads. 0.6.2 changed the
// helmets at Team HQ and at session start; a save loaded straight into a session (or a move made after Team HQ)
// had its helmets changed after that pick, so a converted helmet's group was never loaded and the game reported
// its attachments model missing in the scene after the session. The helmets are now changed when the career has
// loaded and as each session load begins, before the pick.
void on_session_start(const SwoftyEvent *, void *);
void on_career_loaded(const SwoftyEvent *, void *) { refresh("career loaded"); }

void on_flow_state(const SwoftyEvent *ev, void *)
{
    if (!ev || !ev->data)
        return;
    const auto *f = static_cast<const SwoftyFlowState *>(ev->data);
    if (std::strncmp(f->name, "StateLoadSession", sizeof f->name) != 0)
        return;
    on_session_start(nullptr, nullptr);  // the same work: the career's line-up, or the picks outside a career
}

// A move made after our Team HQ handler ran (another addon's, later in the same hub) is caught at the next
// session start: its placeholder then shows from the session after.
void on_session_start(const SwoftyEvent *, void *)
{
    career::State st{};
    if (career::state(&st) == career::Result::ok && st.loaded) {
        refresh("session start");
        return;
    }
    // outside a career: manual picks only
    std::lock_guard<std::mutex> l(g_lock);
    reload_config();
    build_maps();
    if (!g_maps)
        return;
    for (auto &[driver, pick] : g_config.drivers) {
        if (pick == "auto" || pick == "own")
            continue;
        std::string why_not;
        bool fresh = !g_patch_helmet.count(driver);
        size_t n = patch(driver, pick, why_not);
        if (!n && g_patch_failed.insert(driver + ">" + pick + ":" + why_not).second)
            swofty::log("[ai-helmets] pick %s -> %s: not applied (%s)", driver.c_str(), pick.c_str(), why_not.c_str());
        else if (n && fresh)
            swofty::log("[ai-helmets] pick: %s wears %s (%zu field(s))", driver.c_str(), pick.c_str(), n);
    }
}

void on_unloaded(const SwoftyEvent *, void *)
{
    std::lock_guard<std::mutex> l(g_lock);
    unpatch_all();  // the records outlive the career: put every helmet back
    g_applied.clear();
    g_put_this_run.clear();
    g_career.clear();
    g_loaded_state = false;
}

// One look per team, at boot (driver.look takes records at boot only): the team's placeholder helmet (or your
// helmet for that team) with the team's own suit and gloves, "game/<year>-<team>" ("2026-cadillac").
void register_team_looks()
{
    std::set<std::string> keys;
    for (auto &t : kTeams)
        keys.insert(t.key);
    for (auto &[k, h] : g_config.team_helmet)
        if (!h.empty())
            keys.insert(k);
    size_t n = 0;
    for (auto &team : keys) {
        std::string helmet = team_placeholder(team);
        if (helmet.empty())
            continue;
        std::string kit = "game/" + std::string(ends_with(team, "_26") ? "2026-" : "2025-") + team_core(team);
        auto ref = drivers::looks.add("look_" + team, {
                                                          .helmet = wearables::helmets[helmet],
                                                          .suit = wearables::suits[kit],
                                                          .gloves = wearables::gloves[kit],
                                                          .pose = "Pose01",
                                                          .podium_emote = "Default",
                                                      });
        if (ref.empty())
            swofty::log("[ai-helmets] team look for %s refused: %s", team.c_str(), last_error().c_str());
        else {
            g_team_looks[team] = own_ref("look_" + team);
            n++;
        }
    }
    swofty::log("[ai-helmets] registered %zu legacy team look(s) (0.3.0's; kept so careers that point at them load)", n);
}

// 0.3.0 put moved drivers in the team looks above. Those rows copy the game's driver-pool row and are linked to
// no driver, and the game crashed spawning a driver in one (Game_DriverActorSpawner_MakeSceneSetup, after a
// race). 0.3.1 gives each driver a look per team of his season instead: a copy of his own look row (his
// driver link, badge, livery, voice), with only the helmet, suit and gloves changed.
//
// The database isn't readable this early, so the drivers come from the previous run: looks.json (each
// driver's look rows) and catalogue.json (the career line-up seen at the last Team HQ), else the stock grid.

constexpr size_t kMaxDriverLooks = 16;  // keep the preloaded look table close to its stock size

std::string key_part(const std::string &ref)
{
    std::string s = ref.substr(ref.find('/') + 1), out;
    for (char c : s)
        out += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
    return out;
}

void register_driver_looks()
{
    g_looks = read_looks_cache();
    if (g_looks.empty()) {
        swofty::log("[ai-helmets] no looks.json yet: driver looks register from the next game start");
        return;
    }
    // 0.3.1 registered a look for every grid driver at every other team (220 rows). Look rows are preloaded,
    // and with them the career's next session load hung. 0.3.2 registers only the looks the last line-up needs:
    // one per driver who has moved, for the team he races for now. A move seen at Team HQ gets its look at the
    // next game start.
    std::map<std::string, std::string> grid;  // moved driver -> his current team
    std::string text;
    if (storage::read("catalogue.json", text) == storage::Result::ok)
        if (auto *a = J::parse(text)["drivers"].if_array())
            for (auto &d : *a) {
                std::string team = str(d["team"]), orig = str(d["original"]);
                if (d["seated"].as_bool(false) && d["id"].is_string() && !team.empty() && !orig.empty() &&
                    team != orig)
                    grid[str(d["id"])] = team;
            }
    if (grid.size() > kMaxDriverLooks) {
        swofty::log("[ai-helmets] %zu moved drivers in the last line-up; registering looks for the first %zu",
                    grid.size(), kMaxDriverLooks);
        grid.erase(std::next(grid.begin(), kMaxDriverLooks), grid.end());
    }
    size_t n = 0, refused = 0, skipped = 0;
    for (auto &[driver, current] : grid) {
        std::string own = own_look(driver, current), home = home_team(driver, current);
        if (own.empty()) {
            skipped++;
            continue;
        }
        for (auto &t : kTeams) {
            std::string team = t.key;
            if (team != current || same_team(team, home))
                continue;
            std::string helmet = team_placeholder(team);
            if (helmet.empty())
                continue;
            std::string kit = "game/" + std::string(ends_with(team, "_26") ? "2026-" : "2025-") + team_core(team);
            drivers::Look look{
                .helmet = wearables::helmets[helmet],
                .suit = wearables::suits[kit],
                .gloves = wearables::gloves[kit],
                .pose = "Pose01",
                .podium_emote = "Default",
            };
            look.extra = J::Value::object();
            look.extra["from"] = own;  // the core copies this row, then sets helmet, suit and gloves
            std::string key = "dl_" + key_part(driver) + "__" + team;
            auto ref = drivers::looks.add(key, look);
            if (ref.empty()) {
                refused++;
                if (refused <= 3)
                    swofty::log("[ai-helmets] look %s refused: %s", key.c_str(), last_error().c_str());
            } else {
                g_driver_looks[{driver, team}] = own_ref(key);
                n++;
            }
        }
    }
    swofty::log("[ai-helmets] registered %zu driver look(s) for %zu moved driver(s) in the last line-up (%zu without "
                "an own look row, %zu refused)",
                n, grid.size(), skipped, refused);
}

}  // namespace

SWOFTY_ON_CHANGE(auto_team_helmets, bool on)
{
    swofty::log("[ai-helmets] automatic team helmets %s (applies at the next Team HQ)", on ? "on" : "off");
}

SWOFTY_REGISTER
{
    load_config();
    register_helmets();
    register_team_looks();
}

SWOFTY_ON_LOAD
{
    if (!career::available()) {
        swofty::log("[ai-helmets] the core has no career table: automatic team helmets are off");
        return;
    }
    events::subscribe(SWOFTY_EVENT_CAREER_HUB, on_hub);
    events::subscribe(SWOFTY_EVENT_SESSION_START, on_session_start);
    events::subscribe(SWOFTY_EVENT_CAREER_LOADED, on_career_loaded);
    events::subscribe(SWOFTY_EVENT_FLOW_STATE, on_flow_state);
    events::subscribe(SWOFTY_EVENT_CAREER_UNLOADED, on_unloaded);
    swofty::log("[ai-helmets] ready: automatic team helmets %s", auto_team_helmets ? "on" : "off");
    {
        std::lock_guard<std::mutex> l(g_lock);
        build_maps();  // retried at Team HQ if too early
    }
    // The last run's line-up is not this run's: the page shows original line-ups until the next Team HQ.
    std::string text;
    if (storage::read("catalogue.json", text) == storage::Result::ok && !text.empty()) {
        J::Value cat = J::parse(text);
        if (cat.is_object() && cat["live"].as_bool(false)) {
            cat["live"] = false;
            storage::write("catalogue.json", J::dump(cat, 1));
        }
    }
}

SWOFTY_ADDON()

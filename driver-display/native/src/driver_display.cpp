// Driver Display: a steering wheel LCD for F1 25. Its layout and template (ui/, built by tools/build_ui.py) bind to
// HUD leaves under ui_menu_data.hud_rdash_ that this plugin writes every frame (put / put_flag), plus the game's
// own speed_str and laps_of_fuel_format.
//
// Data: the launcher's live state and race control / tyre damage libraries first; the game's UDP packets (read
// from the game's own sendto calls by udp_tap.hpp, nothing listens on the port) for what only they carry, and the
// launcher's telemetry when the tap has no packets.
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <swofty/events.hpp>
#include <swofty/hooks.hpp>
#include <swofty/hud.hpp>
#include <swofty/career.hpp>
#include <swofty/live.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>
#include <swofty/services.hpp>
#include <swofty/systems/race_control.hpp>
#include <swofty/systems/sessions.hpp>
#include <swofty/systems/speech.hpp>
#include <swofty/systems/tyre_damage.hpp>
#include <swofty/telemetry.hpp>
#include <swofty/view.hpp>
#include "ip_link.hpp"
#include "fp_combos.hpp"
#include "mc_combos.hpp"
#include "me_combos.hpp"
#include "rb_combos.hpp"
#include "am_combos.hpp"
#include "wi_combos.hpp"
#include "au_combos.hpp"
#include "hs_combos.hpp"
#include "al_combos.hpp"
#include <ctime>

#include "packet_values.hpp"
#include "split_leaves.hpp"
#include "team_logos.hpp"
#include "udp_tap.hpp"

SWOFTY_SETTING(bool, verbose, false);
SWOFTY_SETTING(int, wear_amber, 40);
SWOFTY_SETTING(int, wear_red, 70);
SWOFTY_SETTING(std::string_view, delta_ref, "previous"); // the delta against the previous lap or the best one
SWOFTY_SETTING(std::string_view, flag_screen, "brief"); // flags over the whole screen: "brief" (4 s) or "full"
SWOFTY_SETTING(bool, undercut, true);    // 1.2.2: undercut / overcut calls
SWOFTY_SETTING(int, undercut_window, 3); // seconds: the gap within which a stop next to you counts
SWOFTY_SETTING(bool, radio_tags, true); // 1.2.3: the engineer's important calls as tags across the bottom row
SWOFTY_SETTING(bool, pace_targets, true); // 1.2.7: CATCH / HOLD lap time calls
SWOFTY_SETTING(bool, start_lights, true); // 1.2.15: the start lights screen and its GO (off: the normal page)
SWOFTY_SETTING(std::string_view, speed_unit, "kph"); // 1.2.18: the Ferrari page's speed: "kph" or "mph"
SWOFTY_SETTING(int, page_action, 1); // "UDP Action" number from the game's controls (any device), 0 = off

namespace {

// ------------------------------------------------------------------ HUD leaves
// A leaf is written when its value changes and every 2 s anyway: when the game rebuilds its view model
// (a new session) the leaves start empty again.
struct Leaf {
    std::string path, last;
    int last_b = -1; // 0 / 1 a bool written, 2 a text written
    double age = 99.0;
    int logged = 0;
    int split = -1;                  // put_flag: -1 not looked up yet, 0 a plain switch, 1 with _n / _f copies
    Leaf *n = nullptr, *f = nullptr; // its copies when split
    Leaf *t = nullptr, *m = nullptr; // 1.2.27: any team's pages, the McLaren's
    Leaf *e = nullptr;               // 1.2.31: the Mercedes's
    Leaf *b = nullptr;               // 1.2.37: the Red Bull's
    Leaf *am = nullptr;              // 1.3.1: the Aston Martin's (suffix _am)
    Leaf *wi = nullptr;              // 1.3.4: the Williams's (suffix _wi)
    Leaf *au = nullptr;              // 1.3.7: the Audi's (suffix _au)
    Leaf *hs = nullptr;              // 1.3.9: the Haas's (suffix _hs)
    Leaf *al = nullptr;              // 1.3.10: the Alpine's (suffix _al)
};

struct NameHash {
    using is_transparent = void;
    size_t operator()(std::string_view v) const noexcept { return std::hash<std::string_view>{}(v); }
};

double g_step = 0.0;
// the player's car was handed to the human (session.player.control): the end of the race intro / cutscenes
std::atomic<bool> g_to_human{false};
std::atomic<int64_t> g_player_vehicle{-1}; // the player's vehicle id (live state), for the control event
int g_page = 0; // the dash page shown: 0 race, 1 car
bool g_pop_on = false; // a pop-up box covers the info panel
bool g_overtake = false;
char g_track_flag = 'g'; // 1.2.37: 'r' red flag, 'y' yellow / SC / VSC, 'b' blue flag, 'g' clear (no blinking)
bool g_held_have = false;
double g_held_delta = 0.0; // overtake available or active (2026): ERS SOC shows blue

// the leaf by its name (1.2.1: a hash lookup; 1.2.0 and before built its path and searched every leaf, ~450 times
// a frame). Leaves live for the whole game.
Leaf &leaf(std::string_view name)
{
    static std::unordered_map<std::string, Leaf *, NameHash, std::equal_to<>> all;
    if (auto it = all.find(name); it != all.end())
        return *it->second;
    Leaf *l = new Leaf{"ui_menu_data.hud_rdash_" + std::string(name)};
    all.emplace(std::string(name), l);
    return *l;
}

// (1.2.4 held fast numbers for 0.2 s against the blur of the game's anti-aliasing / upscaling on the wheel
// screen: still blurred and no longer smooth, so 1.2.5 took it out; the game's own wheel screen blurs the same)
// 1.2.33: the 2 s refresh of unchanged values is spread out: at most kRefreshPerTick a tick (20 a second). With
// the team pages every split switch has five copies, and 1.2.31's refreshes all fell due in one tick: ~400 sets
// every 2 s against the launcher's 512-entry hud set queue ("set queue full ... dropping the oldest"), which
// dropped real updates and froze parts of the display. A changed value is always written at once.
constexpr int kRefreshPerTick = 60;
int g_refresh_left = kRefreshPerTick;
// 1.2.36: every write counts against a per-tick budget too. Getting into a team's car turns hundreds of switches
// over at once (1.2.35's log: 467 dropped in that one tick); a write past the budget waits for the next tick (its
// leaf keeps the old value, so it is retried), and switching over takes a few ticks instead of losing writes.
constexpr int kSetsPerTick = 300;
int g_sets_left = kSetsPerTick;

bool refresh_due(double age)
{
    if (age < 2.0 || g_refresh_left <= 0)
        return false;
    g_refresh_left--;
    return true;
}

void put(const char *name, const std::string &v)
{
    Leaf &l = leaf(name);
    l.age += g_step;
    if (l.last_b == 2 && v == l.last && !refresh_due(l.age))
        return;
    if (g_sets_left <= 0)
        return; // the next tick (1.2.36)
    g_sets_left--;
    auto r = swofty::hud::set(l.path, v);
    if (r != swofty::hud::Result::ok) {
        if (l.logged++ < 1)
            swofty::log("[driver-display] hud %s -> %s", l.path.c_str(), swofty::hud::name(r));
        return;
    }
    l.last = v;
    l.last_b = 2;
    l.age = 0.0;
}

void put(const char *name, const char *v)
{
    put(name, std::string(v ? v : ""));
}
void put(const char *, bool) = delete; // a bool goes through put_flag

// ------------------------------------------------------------------ real-driver rev lights (1.2.9)
// The user's report (and a player's): a created driver's steering wheel LEDs show the classic bar (the first five
// green as RPM lights), a real driver's in the same car keep the first group for single mode lights (straight
// mode, overtake). Found by static disassembly of Steam 1.0.145: Car_SteeringWheelLeds_Update (a launcher binding)
// keeps one entry per car (vehicle id order) from +0x70, 0xA8 apart, the count at +0x1180, and builds each car's 15
// LEDs as three groups of five from the entry's group settings at +0x68 / +0x6C / +0x70. The game fills those from
// the car's driver record (+0x588..+0x590) when its lookup finds one, else 2 / 2 / 2: the created driver's.
// Before each update the player's settings are copied from the team-mate's entry (a real driver in the same car;
// else the setting most other cars have). The offsets are our own (from the launcher's notes and the disassembly),
// so every read is guarded and anything unexpected leaves the game's own lights: a game patch that moves them
// turns this off (logged once), never the rest of the dash.
SWOFTY_SETTING(bool, real_rev_lights, true);
namespace revlights {
constexpr uintptr_t kEntries = 0x70, kStride = 0xA8, kCount = 0x1180, kGroups = 0x68;
std::atomic<int> g_stable{-1};   // the player's vehicle id once steady for a second (1.2.12)
std::atomic<bool> g_created{false}; // the career's player driver is a created one (1.2.12)
std::atomic<int> g_applied{0};   // copies made this session (for the log)
std::atomic<bool> g_off{false};  // a check failed: hands off until the session changes
std::atomic<uint32_t> g_gen{0};  // the session the checks ran in
struct Triple {
    uint32_t v[3];
    bool operator==(const Triple &o) const { return v[0] == o.v[0] && v[1] == o.v[1] && v[2] == o.v[2]; }
    bool sane() const { return v[0] < 0x8000 && v[1] < 0x8000 && v[2] < 0x8000; } // 15 flag bits are used
};
bool read3(uintptr_t at, Triple &t)
{
    return swofty::mem::read(at, t.v[0]) && swofty::mem::read(at + 4, t.v[1]) && swofty::mem::read(at + 8, t.v[2]);
}
bool write3(uintptr_t at, const Triple &t)
{
    if (!swofty::mem::readable(at, 12))
        return false;
    __try {
        auto *p = reinterpret_cast<volatile uint32_t *>(at);
        p[0] = t.v[0], p[1] = t.v[1], p[2] = t.v[2];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
void off(const char *why)
{
    if (!g_off.exchange(true))
        swofty::log("[driver-display] rev lights: left to the game (%s)", why);
}
void apply(void *self)
{
    if (!bool(real_rev_lights) || g_off.load() || !self)
        return;
    uintptr_t sys = reinterpret_cast<uintptr_t>(self);
    uint64_t count = 0;
    if (!swofty::mem::read(sys + kCount, count))
        return off("the LED system can't be read");
    if (count == 0)
        return; // between sessions
    if (count > 32)
        return off("the LED entry count isn't a car count");
    auto at = [&](uint64_t i) { return sys + kEntries + i * kStride + kGroups; };

    // 1.2.12: a car changed that turned out not to be the player's (a session load reported car 1 as the player for
    // one frame) gets its own settings back, when it still holds the copy
    static int wrote_car = -1;
    static Triple wrote{}, wrote_orig{};
    int me = g_stable.load(); // the player's car once it has been the same for a second (1.2.12)
    if (wrote_car >= 0 && wrote_car != me) {
        Triple now{};
        if (uint64_t(wrote_car) < count && read3(at(uint64_t(wrote_car)), now) && now == wrote) {
            write3(at(uint64_t(wrote_car)), wrote_orig);
            swofty::log("[driver-display] rev lights: car %d (not the player's) gets its own %u/%u/%u back", wrote_car,
                        wrote_orig.v[0], wrote_orig.v[1], wrote_orig.v[2]);
        }
        wrote_car = -1;
    }
    if (me < 0 || uint64_t(me) >= count)
        return;
    Triple mine{};
    if (!read3(at(uint64_t(me)), mine) || !mine.sane())
        return off("the player's LED settings don't look like LED settings");
    // 1.2.12: only a created player driver (the career's player driver id is a created one: the launcher's career
    // state). A real driver keeps the game's lights, his own style included (Leclerc's 49 flashes the greens: the
    // game's, which 1.2.9-1.2.11 took for a created driver's because it isn't the common style)
    if (!g_created.load())
        return;
    // the player's style as the game gave it (never our own copy), read again when the game rebuilds the entry
    static uint32_t seen_gen = ~0u;
    static Triple created{};
    static bool have_created = false;
    bool ours = wrote_car == me && mine == wrote;
    if (seen_gen != g_gen.load()) {
        seen_gen = g_gen.load();
        if (!ours)
            created = mine, have_created = true;
    } else if (!ours && !(have_created && mine == created)) {
        created = mine, have_created = true;
    }
    if (!have_created)
        return;
    // the most common real-driver style: the other cars' settings, the player's own (created) style skipped
    Triple best{};
    int best_n = 0, from = -1;
    for (uint64_t i = 0; i < count; i++) {
        Triple t{};
        if (int(i) == me || !read3(at(i), t) || !t.sane() || t == created)
            continue;
        int n = 0;
        for (uint64_t k = 0; k < count; k++) {
            Triple u{};
            if (int(k) != me && read3(at(k), u) && u == t)
                n++;
        }
        if (n > best_n)
            best = t, best_n = n, from = int(i);
    }
    if (from < 0 || best == mine)
        return;
    if (!write3(at(uint64_t(me)), best))
        return off("the player's LED settings can't be written");
    if (wrote_car != me)
        wrote_orig = created;
    wrote_car = me, wrote = best;
    static int last_from = -2;
    if (from != last_from || g_applied.fetch_add(1) == 0) {
        last_from = from;
        swofty::log("[driver-display] rev lights: created player's car %d %u/%u/%u -> %u/%u/%u (car %d's, the most common "
                    "real-driver style)", me, created.v[0], created.v[1], created.v[2], best.v[0], best.v[1], best.v[2], from);
    }
}
void (*orig)(void *, void *, void *, void *) = nullptr;
// the update's own arguments pass straight through (its prototype isn't in the launcher's bindings)
void wrap(void *self, void *a, void *b, void *c)
{
    apply(self);
    orig(self, a, b, c);
}
const swofty::detail::mixin reg{"Car_SteeringWheelLeds_Update", SWOFTY_MIXIN_WRAP, 0, (void *)&wrap, (void **)&orig};

// the frame side (the flow thread): the team-mate by team badge, a new session re-arms the checks
void frame()
{
    auto ses = swofty::live::session();
    uint32_t gen = ses ? ses->generation : 0;
    if (gen != g_gen.load()) {
        g_gen = gen;
        g_off = false;
        g_applied = 0;
    }
    // the player's car, published once it has been the same for a second (a session load can name another car for
    // a frame)
    static int cand = -1;
    static double held = 0.0;
    auto me = swofty::live::player();
    int v = me ? int(me->vehicle) : -1;
    if (v != cand) {
        cand = v;
        held = 0.0;
        g_stable = -1;
    } else if ((held += g_step) >= 1.0) {
        g_stable = v;
    }
    // a created player driver: the career's player driver id is a created one (a stock driver's is its stat id in the
    // high half, 0x70000; a created driver's carries its own table in the low half, 0x40001)
    swofty::career::State cs{};
    bool created = false;
    if (swofty::career::state(&cs) == swofty::career::Result::ok && cs.loaded && cs.has_player)
        created = cs.player_driver >= 0x10000 && (cs.player_driver & 0xFFFF) != 0;
    if (created != g_created.load()) {
        g_created = created;
        swofty::log("[driver-display] rev lights: %s (career player driver %#x)", created ? "a created player driver" :
                    "the player isn't a created driver: the game's lights", cs.player_driver);
    }
}
} // namespace revlights

// the 2026 Ferrari is the player's car (and its correction is on): the switches in kSplitLeaves show their _f copies
bool g_fer = false;  // the 2026 Ferrari's own pages are on (g_team == 'f')
char g_team = 0;     // 1.2.27: whose own pages are on: 'f' the 2026 Ferrari, 'm' the 2026 McLaren, 'e' the 2026
                     // Mercedes (1.2.31), 0 none
bool g_fp = false;  // 1.2.18: a Ferrari page is showing (1.2.22: every page on the 2026 Ferrari)
char g_fps = 0;     // 1.2.22: its layout: 'r' race, 'q' practice / qualifying, 's' race under a SC, 'd' car, 'p' pit

void put_flag_one(Leaf &l, bool v)
{
    l.age += g_step;
    // 1.2.34: a switch that is off is never refreshed (a reset of the game's HUD data leaves it off too); that is
    // every switch of the team pages not in use, and every hidden copy, so only what shows is refreshed
    if (l.last_b == int(v) && (!v || !refresh_due(l.age)))
        return;
    if (g_sets_left <= 0)
        return; // the next tick (1.2.36)
    g_sets_left--;
    if (swofty::hud::set(l.path, v) != swofty::hud::Result::ok)
        return;
    l.last_b = int(v);
    l.age = 0.0;
}

// A switch. One the layout draws in copies (kSplitLeaves) is written as <name>_n (no team's own pages), <name>_t (any
// team's), <name>_f (the Ferrari's) and <name>_m (the McLaren's) too.
void put_flag(const char *name, bool v) // a separate name: a char buffer would otherwise pick a bool overload over std::string (0.1.x wrote every text as true)
{
    // 1.2.18: the shared bottom row (its lines, ERS SOC, the mode badge) gives way to the Ferrari's own page 0;
    // 1.2.20: these are plain switches (no bent Ferrari copies any more)
    static const std::unordered_set<std::string_view> fp_hide{"all",      "mid_show",   "soc_ok",      "soc_low",
                                                              "soc_b",    "badge_dark", "badge_light", "badge_n"};
    if (g_fp && fp_hide.count(name))
        v = false;
    Leaf &l = leaf(name);
    put_flag_one(l, v);
    if (l.split < 0) {
        static const std::unordered_set<std::string_view> split(std::begin(kSplitLeaves), std::end(kSplitLeaves));
        l.split = split.count(name) ? 1 : 0;
        if (l.split) {
            l.n = &leaf(std::string(name) + "_n");
            l.f = &leaf(std::string(name) + "_f");
            l.t = &leaf(std::string(name) + "_t");
            l.m = &leaf(std::string(name) + "_m");
            l.e = &leaf(std::string(name) + "_e");
            l.b = &leaf(std::string(name) + "_b");
            l.am = &leaf(std::string(name) + "_am");
            l.wi = &leaf(std::string(name) + "_wi");
            l.au = &leaf(std::string(name) + "_au");
            l.hs = &leaf(std::string(name) + "_hs");
            l.al = &leaf(std::string(name) + "_al");
        }
    }
    if (l.split) {
        put_flag_one(*l.n, v && !g_team);
        put_flag_one(*l.t, v && g_team);
        put_flag_one(*l.f, v && g_team == 'f');
        put_flag_one(*l.m, v && g_team == 'm');
        put_flag_one(*l.e, v && g_team == 'e');
        put_flag_one(*l.b, v && g_team == 'b');
        put_flag_one(*l.am, v && g_team == 'a');
        put_flag_one(*l.wi, v && g_team == 'w');
        put_flag_one(*l.au, v && g_team == 'u');
        put_flag_one(*l.hs, v && g_team == 'h');
        put_flag_one(*l.al, v && g_team == 'l');
    }
}

// ------------------------------------------------------------------ the car's state
struct Reading {
    char src = 0; // 'L' live, 'T' telemetry, 0 none
    int speed_kmh = -1, rpm = -1, soc = -1, lap = -1, pos = -1, gap_ms = -1;
    int gear = -99; // -1 reverse, 0 neutral
    float lap_m = -1.f;
    float tel_last_s = -1.f; // the game's own last lap time (telemetry only)
    float fuel_laps = -999.f; // laps of fuel margin, as the game's MFD shows it (telemetry only)
    float tyre[4] = {-1.f, -1.f, -1.f, -1.f}; // FL FR RL RR, deg C (telemetry only)
    float wear[4] = {-1.f, -1.f, -1.f, -1.f}; // FL FR RL RR, % (telemetry only, as Wheel Dash had it)
    bool in_pit = false;
    float game_t = -1.f; // the game's session time (telemetry), seconds
    int ers_mode = -1;
    int gap_b_ms = -1;   // the car behind's gap to this one
    int sc = 0;          // 0 none, 1 safety car, 2 virtual safety car, 3 formation lap
    bool red_flag = false;
    bool sector_yellow = false; // the sector the car is in is under yellow (live state)
    // penalty seconds, unserved drive throughs / stop-gos, warnings, track limit warnings; the result status (2
    // running, 3 finished); pit stops; the visual tyre compound and its age; the chequered flag is out
    int pen_s = -1, dt = -1, sg = -1, warns = -1, tl = -1, result = -1, stops = -1, tyre_vis = -1, tyre_age = -1;
    bool chequered = false;
};

void pit_extras(const swofty::telemetry::CarState &c, Reading &r);

// the telemetry's tyre temperatures keep the UDP order, RL RR FL FR (checked against the raw log line)
void tyres_from(const swofty::telemetry::CarState &c, Reading &r)
{
    static int logged = 0;
    if (logged++ < 2)
        swofty::log("[driver-display] telemetry tyre_temp raw [%.0f %.0f %.0f %.0f]", double(c.tyre_temp[0]),
                    double(c.tyre_temp[1]), double(c.tyre_temp[2]), double(c.tyre_temp[3]));
    r.tyre[2] = c.tyre_temp[0];
    r.tyre[3] = c.tyre_temp[1];
    r.tyre[0] = c.tyre_temp[2];
    r.tyre[1] = c.tyre_temp[3];
    // wear, same UDP order; all zero means no damage packet has arrived (fresh tyres read 0.x, rarely all 0)
    if (c.tyre_wear[0] > 0.f || c.tyre_wear[1] > 0.f || c.tyre_wear[2] > 0.f || c.tyre_wear[3] > 0.f) {
        r.wear[2] = c.tyre_wear[0];
        r.wear[3] = c.tyre_wear[1];
        r.wear[0] = c.tyre_wear[2];
        r.wear[1] = c.tyre_wear[3];
    }
}

Reading from_live()
{
    Reading r;
    auto car = swofty::live::player();
    if (!car)
        return r;
    using swofty::live::has;
    r.src = 'L';
    if (has(*car, SWOFTY_LIVE_HAVE_SPEED)) r.speed_kmh = int(std::lround(car->speed * 3.6f));
    if (has(*car, SWOFTY_LIVE_HAVE_RPM)) r.rpm = int(std::lround(car->rpm));
    if (has(*car, SWOFTY_LIVE_HAVE_GEAR)) r.gear = car->gear;
    if (has(*car, SWOFTY_LIVE_HAVE_ERS_STORE) && car->ers_capacity > 0.f)
        r.soc = int(std::lround(car->ers_store / car->ers_capacity * 100.f));
    if (has(*car, SWOFTY_LIVE_HAVE_PROGRESS)) {
        r.lap = car->lap; // laps completed (0 on the first lap: 0.1.3 log)
        r.lap_m = car->lap_m;
    }
    r.in_pit = swofty::live::in_pit(*car);
    if (auto o = swofty::live::order(); o.player_rank >= 0)
        if (auto b = o.behind(car->vehicle); b && has(*b, SWOFTY_LIVE_HAVE_GAP_AHEAD) && b->gap_ahead_ms != SWOFTY_LIVE_NO_GAP)
            r.gap_b_ms = b->gap_ahead_ms;
    if (auto s = swofty::live::session()) {
        auto sc = swofty::live::safety_car(*s);
        r.sc = sc == swofty::live::SafetyCar::unknown ? 0 : int(sc);
        r.red_flag = s->red_flag != 0;
        // yellow_sectors: one bit per sector of the track under yellow; the car's sector from its live state
        if (has(*s, SWOFTY_LIVE_SHAVE_YELLOW) && has(*car, SWOFTY_LIVE_HAVE_SECTOR) && car->sector < 8)
            r.sector_yellow = (s->yellow_sectors >> car->sector) & 1;
    }
    if (has(*car, SWOFTY_LIVE_HAVE_PENALTIES)) {
        r.pen_s = int(std::lround(car->time_penalty_s));
        r.dt = car->drive_through;
        r.sg = car->stop_go;
        r.warns = car->warnings;
        r.tl = car->track_limit_warnings;
    }
    if (car->result_status) r.result = car->result_status;
    if (auto n = swofty::live::pit_stops(*car)) r.stops = *n;
    if (has(*car, SWOFTY_LIVE_HAVE_TYRE) && car->tyre_visual) r.tyre_vis = car->tyre_visual;
    if (auto s = swofty::live::session()) r.chequered = s->chequered != 0;
    if (has(*car, SWOFTY_LIVE_HAVE_ERS_MODE)) r.ers_mode = car->ers_mode;
    if (has(*car, SWOFTY_LIVE_HAVE_CLASS) && car->position > 0) r.pos = car->position;
    if (has(*car, SWOFTY_LIVE_HAVE_GAP_AHEAD) && car->gap_ahead_ms != SWOFTY_LIVE_NO_GAP) r.gap_ms = car->gap_ahead_ms;
    return r;
}

// the telemetry's player car, when the session state names it
bool telemetry_car(swofty::telemetry::CarState &c)
{
    swofty::telemetry::SessionState s{};
    s.version = swofty::telemetry::version;
    if (swofty::telemetry::session(&s) != swofty::telemetry::Result::ok || !s.udp_format)
        return false;
    c = {};
    c.version = swofty::telemetry::version;
    return swofty::telemetry::car(s.player_index, &c) == swofty::telemetry::Result::ok;
}

// a lap time from the telemetry in seconds; a value too big for seconds is taken as milliseconds
float seconds(float v)
{
    if (!(v > 0.f))
        return -1.f;
    return v > 1000.f ? v / 1000.f : v;
}

Reading from_telemetry()
{
    Reading r;
    swofty::telemetry::CarState c{};
    if (!telemetry_car(c))
        return r;
    r.src = 'T';
    r.speed_kmh = int(std::lround(c.speed * 3.6f)); // m/s (0.0.2's log: 62.9 in 6th gear at 10319 rpm)
    r.rpm = int(std::lround(c.rpm));
    r.gear = c.gear;
    if (c.ers_store >= 0.f) r.soc = int(std::lround(c.ers_store / 4.0e6f * 100.f)); // 4 MJ store
    r.lap = int(c.lap) - 1; // the UDP lap number is the current lap (1 on the first); laps completed, as live
    r.lap_m = c.lap_distance;
    if (c.position > 0) r.pos = c.position;
    if (c.gap_front_ms > 0) r.gap_ms = int(c.gap_front_ms);
    r.tel_last_s = seconds(c.last_lap_time);
    if (c.flags & SWOFTY_CAR_FLAG_STATUS) r.fuel_laps = c.fuel_laps; // only once a status packet filled it (0.1.3 showed +0.00)
    tyres_from(c, r);
    pit_extras(c, r);
    return r;
}

// the telemetry's tyre, stops and penalties where the reading has none (the launcher's decode of the packets)
void pit_extras(const swofty::telemetry::CarState &c, Reading &r)
{
    if (c.version < 2)
        return;
    if (c.flags & SWOFTY_CAR_FLAG_STATUS) {
        if (r.tyre_vis < 0 && c.tyre_visual) r.tyre_vis = c.tyre_visual;
        r.tyre_age = c.tyre_age_laps;
    }
    if (c.flags & SWOFTY_CAR_FLAG_LAP_DATA) {
        if (r.stops < 0) r.stops = c.pit_stops;
        if (r.pen_s < 0) {
            r.pen_s = c.penalty_seconds;
            r.dt = c.drive_through_pens;
            r.sg = c.stop_go_pens;
            r.warns = c.warnings;
            r.tl = c.corner_cut_warnings;
        }
        if (r.result < 0 && c.result_status) r.result = c.result_status;
    }
}

// the tap's lap data and car status where the reading has none (they come straight from the game's packets)
void tap_extras(Reading &r)
{
    auto &G = udp_tap::g;
    if (udp_tap::fresh(G.lap_tick) && G.stops >= 0) {
        if (r.stops < 0) r.stops = G.stops;
        if (r.pen_s < 0) {
            r.pen_s = G.pen_s;
            r.dt = G.unserved_dt;
            r.sg = G.unserved_sg;
            r.warns = G.warns;
            r.tl = G.tl_warns;
        }
        if (r.result < 0 && G.result > 0) r.result = G.result;
    }
    if (udp_tap::fresh(G.status_tick) && !G.from_launcher && G.tyre_visual > 0) {
        r.tyre_vis = G.tyre_visual;
        r.tyre_age = G.tyre_age;
    }
}

// A driver's three-letter code from the name the game's participants packet gives (the launcher's telemetry):
// the surname, written in capitals by the game ("Lewis HAMILTON" -> HAM, "Nyck DE VRIES" -> DEV), else the last
// word. Accented Latin letters lose their accent (PEREZ): the wheel's font has only plain ASCII.
std::string driver_code(const char *name)
{
    static const char latin1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty"; // U+00C0..U+00FF
    std::string plain;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(name); *p; p++) {
        if (*p < 0x80) plain += char(*p);
        else if (*p == 0xC3 && p[1] >= 0x80 && p[1] <= 0xBF) plain += latin1[*++p - 0x80]; // C3 80..BF = U+00C0..U+00FF
    }
    std::vector<std::string> words;
    std::string w;
    for (char c : plain + " ") {
        if (c == ' ' || c == '-') {
            if (!w.empty()) words.push_back(w);
            w.clear();
        } else if (std::isalpha(static_cast<unsigned char>(c))) {
            w += c;
        }
    }
    auto caps = [](const std::string &x) {
        if (x.size() < 2) return false;
        for (char c : x) if (!std::isupper(static_cast<unsigned char>(c))) return false;
        return true;
    };
    std::string surname;
    for (auto &x : words) // the capitalised words, joined (DE VRIES -> DEVRIES)
        if (caps(x)) surname += x;
    if (surname.empty() && !words.empty()) surname = words.back();
    std::string code;
    for (char c : surname) {
        if (code.size() == 3) break;
        code += char(std::toupper(static_cast<unsigned char>(c)));
    }
    return code;
}

// The codes of the cars one place ahead of and behind the player, by the positions in the telemetry's participants
// ("" where there is none, or no telemetry)
void neighbour_codes(std::string &ahead, std::string &behind)
{
    ahead.clear();
    behind.clear();
    swofty::telemetry::SessionState s{};
    s.version = swofty::telemetry::version;
    if (swofty::telemetry::session(&s) != swofty::telemetry::Result::ok || !s.udp_format)
        return;
    // each car's position from its car state (the lap data; 0.4.14-0.4.37 used the participant record's, which
    // never matched: AHEAD / BEHIND stayed words), its name from the participant record (the participants packet)
    auto position = [](uint32_t i) {
        swofty::telemetry::CarState c{};
        c.version = swofty::telemetry::version;
        if (swofty::telemetry::car(i, &c) == swofty::telemetry::Result::ok && c.position > 0)
            return int(c.position);
        swofty::telemetry::Participant p{};
        p.version = swofty::telemetry::version;
        if (swofty::telemetry::participant(i, &p) == swofty::telemetry::Result::ok && p.position > 0)
            return int(p.position);
        return 0;
    };
    int mine = position(s.player_index);
    int named = 0, placed = 0;
    for (uint32_t i = 0; i < 24; i++) {
        swofty::telemetry::Participant p{};
        p.version = swofty::telemetry::version;
        bool has_name = swofty::telemetry::participant(i, &p) == swofty::telemetry::Result::ok && p.name[0];
        int pos = position(i);
        named += has_name;
        placed += pos > 0;
        if (i == s.player_index || !has_name || mine <= 0)
            continue;
        if (pos == mine - 1) ahead = driver_code(p.name);
        else if (pos == mine + 1) behind = driver_code(p.name);
    }
    static int logged_named = -1, logged_placed = -1, logged_mine = -1;
    if (named != logged_named || placed != logged_placed || (mine > 0) != (logged_mine > 0)) {
        swofty::log("[driver-display] driver codes: %d named and %d placed of 24 (participants %u), player %u at P%d",
                    named, placed, s.participant_count, s.player_index, mine);
        logged_named = named;
        logged_placed = placed;
        logged_mine = mine;
    }
}

// The codes of the cars ahead of and behind the player from the live state: the cars by the classification order
// (where the gaps come from too), their names as the game shows them (live::driver_name, the launcher's own
// Sess_Get_CarDriverName lookup, SDK of 2026-11-19; Driver Momentum reads the same). False on a core without it.
bool live_neighbour_codes(std::string &ahead, std::string &behind)
{
    auto car = swofty::live::player();
    if (!car)
        return false;
    auto o = swofty::live::order();
    if (o.player_rank < 0)
        return false;
    bool any = false;
    if (auto a = o.ahead(car->vehicle))
        if (auto n = swofty::live::driver_name(a->vehicle); n && !n->empty()) {
            ahead = driver_code(n->c_str());
            any = true;
        }
    if (auto b = o.behind(car->vehicle))
        if (auto n = swofty::live::driver_name(b->vehicle); n && !n->empty()) {
            behind = driver_code(n->c_str());
            any = true;
        }
    static bool logged = false;
    if (any && !logged) {
        swofty::log("[driver-display] driver codes from the live state's driver names");
        logged = true;
    }
    return any;
}

// the telemetry's last lap time and fuel margin, also while the live state is the source
void telemetry_extras(Reading &r)
{
    swofty::telemetry::CarState c{};
    if (!telemetry_car(c))
        return;
    r.tel_last_s = seconds(c.last_lap_time);
    if (c.flags & SWOFTY_CAR_FLAG_STATUS) r.fuel_laps = c.fuel_laps; // only once a status packet filled it (0.1.3 showed +0.00)
    tyres_from(c, r);
    pit_extras(c, r);
}

// When the tap reads no packets but the launcher's telemetry decodes them (its own UDP hook is on), the values the
// telemetry has stand in: tyre temperatures and wear, brake temperatures, the fuel margin and the car's flag
// (packet order RL RR FL FR, as the tap's). Fuel mix, brake bias, the differential, the ERS mode and the page
// button come from the tap only.
void launcher_fill()
{
    if (udp_tap::fresh(udp_tap::g.status_tick))
        return;
    swofty::telemetry::CarState c{};
    if (!telemetry_car(c) || !(c.flags & SWOFTY_CAR_FLAG_STATUS))
        return;
    static int logged = 0;
    if (!logged++)
        swofty::log("[driver-display] the tap reads no packets: tyres, brakes, fuel and flags from the launcher's telemetry");
    uint64_t now = GetTickCount64();
    static const int order[4] = {2, 3, 0, 1};
    bool temps = false, wear = false;
    for (int i = 0; i < 4; i++) {
        temps = temps || c.tyre_temp[i] > 0.f;
        wear = wear || c.tyre_wear[i] > 0.f;
    }
    for (int i = 0; i < 4; i++) {
        if (temps) udp_tap::g.tyre_inner[order[i]] = int(std::lround(c.tyre_temp[i]));
        if (temps) udp_tap::g.brake_temp[order[i]] = int(std::lround(c.brake_temp[i]));
        if (wear) udp_tap::g.wear[order[i]] = c.tyre_wear[i];
    }
    if (temps) udp_tap::g.tel_tick = now;
    if (wear) udp_tap::g.damage_tick = now;
    udp_tap::g.fuel_laps = c.fuel_laps;
    udp_tap::g.fia_flag = c.fia_flag;
    udp_tap::g.status_tick = now;
    udp_tap::g.from_launcher = true;
}

// ------------------------------------------------------------------ lap timing and the live delta
// The lap clock runs on frame time while the game isn't paused. A lap ends when the lap counter goes up.
// The best lap's clock is kept every 5 m of lap distance; the delta is the current clock minus the best
// lap's at the same distance.
constexpr float kBin = 5.f;
constexpr int kBins = 2400; // 12 km
struct Laps {
    int lap = -1;
    double clock = 0.0;
    float last_s = -1.f, best_s = -1.f;
    std::vector<float> cur = std::vector<float>(kBins, -1.f), best = std::vector<float>(kBins, -1.f);
    bool have_delta = false;
    double delta = 0.0;
    // the current lap began on a lap counter change this plugin saw. The lap the data first arrives in does
    // not count: it started before (or, on a race grid, the clock ran from the grid; 0.1.3 timed the opening
    // lap from loading in, 107 s for a 83 s lap, and every later delta was 24 s "faster")
    bool whole = false;
    bool frozen_logged = false;
    bool on_grid = false;
    float grid_m = -1.f; // the lap distance of the grid slot
};
Laps g_laps;

void time_laps(const Reading &r, double dt)
{
    Laps &L = g_laps;
    if (r.lap < 0)
        return;
    // The clock follows the game's own session time when the launcher's telemetry has one that moves: it stops
    // in pauses and replays and goes back with a flashback by itself. Without it the clock runs on frame time,
    // stops while the game is paused and while the car is frozen (the instant replay doesn't pause the game but
    // holds the car still: the same lap distance and no speed for over a second, outside the pit lane, where a
    // stop's standing time counts).
    static float still_m = -1.f, prev_m = -1.f;
    static double still_s = 0.0, prev_gt = -1.0, gt_still = 0.0;
    bool use_gt = false;
    double gstep = 0.0;
    if (r.game_t > 0.f) {
        if (prev_gt > 0.0) {
            gstep = double(r.game_t) - prev_gt;
            gt_still = gstep == 0.0 ? gt_still + dt : 0.0;
        }
        prev_gt = r.game_t;
        // a session time stuck while frames run (the UDP feed stopped) is no clock
        use_gt = gt_still < 2.0 && gstep > -30.0 && gstep < 2.0;
    }
    static int gt_logged = -1;
    if (int(use_gt) != gt_logged) {
        gt_logged = int(use_gt);
        swofty::log("[driver-display] lap clock: %s", use_gt ? "the game's session time" : "frame time");
    }
    bool frozen = false;
    if (!use_gt) {
        if (r.lap_m >= 0.f && r.speed_kmh <= 0 && std::fabs(r.lap_m - still_m) < 0.05f && !r.in_pit) {
            still_s += dt;
            frozen = still_s > 1.0;
        } else {
            still_s = 0.0;
        }
        still_m = r.lap_m;
        if (frozen && !L.frozen_logged) {
            L.frozen_logged = true;
            swofty::log("[driver-display] lap clock held: the car is frozen at %.1f m (a replay?), pause reasons %#x",
                        double(r.lap_m), swofty::view::pause_reasons());
        } else if (!frozen) {
            L.frozen_logged = false;
        }
    }
    if (use_gt)
        L.clock += gstep;
    else if (!swofty::view::paused() && !frozen)
        L.clock += dt;

    // A flashback puts the car back on the track (same lap): the clock goes back to the time it had when the car
    // first passed that point, and what came after it is forgotten (0.1.4 kept running and the lap came out 10+ s
    // slow).
    if (r.lap == L.lap && prev_m >= 0.f && r.lap_m >= 0.f && r.lap_m < prev_m - 30.f && prev_m - r.lap_m < 2000.f) {
        int b = int(r.lap_m / kBin);
        if (b >= 0 && b < kBins) {
            // the clock goes back too, also on the game's session time: that doesn't rewind with a flashback
            // (0.2.1: the clock kept running through one and the delta came out +52 s)
            if (L.cur[b] >= 0.f)
                L.clock = L.cur[b];
            for (int i = b + 1; i < kBins; i++)
                L.cur[i] = -1.f;
            static double last_log = -10.0;
            if (L.clock < last_log - 5.0 || L.clock > last_log + 5.0) { // a flashback's rewind takes many frames
                last_log = L.clock;
                swofty::log("[driver-display] flashback: %.0f m -> %.0f m, lap clock back to %.2f", double(prev_m),
                            double(r.lap_m), L.clock);
            }
        }
    }
    prev_m = r.lap_m;

    // A race's opening lap counts from the moment the car leaves its grid slot (0.1.5 skipped it, so the first
    // lap time showed on lap 3). Only when the data arrived with the car standing still outside the pits on
    // lap 0: a garage start (practice, qualifying) is an out lap, not timed.
    // Leaving it takes real movement: 5 km/h and 3 m from the slot (0.4.27's log: "off the grid" half a second after
    // the data came, the car still on the grid; a car settling at load reads 1 km/h or so, and the lap clock ran).
    if (L.lap == 0 && r.lap == 0 && !L.whole) {
        if (r.speed_kmh <= 0 && !r.in_pit && r.lap_m >= 0.f) {
            if (!L.on_grid)
                L.grid_m = r.lap_m;
            L.on_grid = true;
        } else if (L.on_grid && r.speed_kmh >= 5 && std::fabs(r.lap_m - L.grid_m) > 3.f &&
                   r.sc != 3) { // not off on the formation lap: the real start
            L.on_grid = false;
            L.whole = true;
            L.clock = 0.0;
            std::fill(L.cur.begin(), L.cur.end(), -1.f);
            swofty::log("[driver-display] off the grid at %.1f m: the opening lap is timed from here", double(r.lap_m));
        }
    }

    if (r.lap != L.lap) {
        bool next = L.lap >= 0 && r.lap == L.lap + 1;
        if (next && L.whole) {
            L.last_s = float(L.clock);
            if (L.best_s < 0.f || L.last_s < L.best_s) {
                L.best_s = L.last_s;
                L.best = L.cur;
            }
        } else if (!next && L.lap >= 0 && r.lap == L.lap - 1) {
            // a flashback over the line, back into the previous lap: keep the best lap; this lap's start went by
            // before the rewind, so it isn't timed
            L.whole = false;
            L.lap = r.lap;
            L.clock = 0.0;
            std::fill(L.cur.begin(), L.cur.end(), -1.f);
            return;
        } else if (!next && L.lap >= 0) {
            // the counter jumped (a restart): start over
            L = Laps{};
        }
        L.whole = next;
        L.lap = r.lap;
        L.clock = 0.0;
        std::fill(L.cur.begin(), L.cur.end(), -1.f);
    }
    L.have_delta = false;
    if (r.lap_m >= 0.f) {
        int b = int(r.lap_m / kBin);
        if (b >= 0 && b < kBins) {
            if (L.cur[b] < 0.f)
                L.cur[b] = float(L.clock);
            if (L.best_s > 0.f && L.best[b] >= 0.f) {
                L.have_delta = true;
                L.delta = L.clock - L.best[b];
            }
        }
    }
}

// ------------------------------------------------------------------ the delta on the game's own lap clock
// From the game's lap data packets: the game's current lap time against the distance round the lap. The game's
// lap clock rewinds with a flashback and stops in pauses and replays, so nothing needs correcting (Wheel Dash
// worked this way; the addon's own clock above is the fallback while no packets arrive). Every 5 m the lap's time
// is written, overwriting on a second pass, so after a flashback the re-driven part replaces what was abandoned.
// A finished lap becomes the reference when the game's own last lap time for it beats the best one; an
// invalidated lap only while there is no valid reference yet.
struct GameLaps {
    int lap = -1;
    bool whole = false; // the lap was seen from its start
    bool invalid = false;
    std::vector<float> cur = std::vector<float>(kBins, -1.f), best = std::vector<float>(kBins, -1.f), held;
    float best_s = -1.f;
    bool best_invalid = false;
    bool pending = false, held_whole = false, held_invalid = false;
    uint32_t held_old_last = 0, prev_last = 0;
    int held_age = 0;
    bool have = false;
    double delta = 0.0;
    // the previous whole lap, the other reference the delta can use
    std::vector<float> prev = std::vector<float>(kBins, -1.f);
    float prev_s = -1.f;
    bool have_prev = false;
    double delta_prev = 0.0;
};
GameLaps g_game;

void game_laps()
{
    GameLaps &G = g_game;
    G.have = false;
    G.have_prev = false;
    if (!udp_tap::fresh(udp_tap::g.lap_tick))
        return;
    int lap = udp_tap::g.lap_num;
    float t = udp_tap::g.cur_lap_ms / 1000.f, d = udp_tap::g.lap_dist;
    uint32_t last = udp_tap::g.last_lap_ms;
    if (lap <= 0)
        return;
    if (lap != G.lap) {
        if (G.lap > 0 && lap == G.lap + 1) {
            G.held = G.cur;
            G.pending = true;
            G.held_whole = G.whole;
            G.held_invalid = G.invalid;
            G.held_old_last = G.prev_last;
            G.held_age = 0;
            G.whole = true;
        } else {
            // the first lap seen counts if it has only just begun (a race start, a session start)
            G.whole = G.lap < 0 && t < 2.f;
            G.pending = false;
        }
        G.lap = lap;
        G.invalid = false;
        std::fill(G.cur.begin(), G.cur.end(), -1.f);
    }
    // the finished lap's time arrives in a later packet than the lap change: wait for it (up to ~3 s)
    if (G.pending) {
        G.held_age++;
        if (last > 0 && last != G.held_old_last) {
            G.pending = false;
            float lap_s = last / 1000.f;
            if (G.held_whole) {
                G.prev = G.held;
                G.prev_s = lap_s;
            }
            bool better = G.best_s < 0.f || (G.best_invalid && !G.held_invalid) ||
                          (lap_s < G.best_s && (!G.held_invalid || G.best_invalid));
            if (G.held_whole && better) {
                G.best = G.held;
                G.best_s = lap_s;
                G.best_invalid = G.held_invalid;
                swofty::log("[driver-display] reference lap %.3f%s", double(lap_s), G.held_invalid ? " (invalid)" : "");
            }
        } else if (G.held_age > 60) {
            G.pending = false;
        }
    }
    G.prev_last = last;
    G.invalid = G.invalid || udp_tap::g.lap_invalid;
    if (d < 0.f)
        return; // before the line on a race's opening lap
    int b = int(d / kBin);
    if (b < 0 || b >= kBins)
        return;
    G.cur[b] = t;
    // against a reference lap: its time at the nearest point at or behind this one
    auto against = [&](const std::vector<float> &ref, float ref_s, bool &have, double &delta) {
        if (ref_s <= 0.f)
            return;
        for (int i = b; i >= 0 && i > b - 6; i--)
            if (ref[i] >= 0.f) {
                have = true;
                delta = double(t) - ref[i];
                return;
            }
    };
    against(G.best, G.best_s, G.have, G.delta);
    against(G.prev, G.prev_s, G.have_prev, G.delta_prev);
}

std::string laptime(float s)
{
    if (s <= 0.f)
        return "--:--.---";
    int ms = int(std::lround(s * 1000.f));
    char t[24];
    std::snprintf(t, sizeof t, "%d:%02d.%03d", ms / 60000, (ms / 1000) % 60, ms % 1000);
    return t;
}

// ------------------------------------------------------------------ the info panel's pop-ups
// Inside the bottom centre panel: red while a red flag is out, amber while a safety car / VSC is (formation
// laps too), else blue for 2 s after a change (the ERS mode). The ERS mode shows when no box is up.
const char *mix_name(int m)
{
    static const char *names[] = {"LEAN", "STD", "RICH", "MAX"};
    return m >= 0 && m < 4 ? names[m] : "-";
}

// the pop-up in the bottom middle cell: 'b' blue (a setting change), 'a' amber (a warning), 'r' red (the lap is
// invalid); the latest replaces the one up
std::string g_pop_title, g_pop_value;
char g_pop_colour = 'b';
double g_pop_left = 0.0;
void pop(const char *title, const std::string &value, char colour = 'b', double secs = 2.0)
{
    g_pop_title = title;
    g_pop_value = value;
    g_pop_colour = colour;
    g_pop_left = secs;
}

// the full screens for a penalty (white, 4 s) and the chequered flag (4 s when it comes out, 8 s at the finish)
std::string g_pen_title, g_pen_sub, g_chq_title, g_chq_sub;
double g_pen_left = 0.0, g_chq_left = 0.0;
// a warning (track limits, a collision, ...): white on black over the whole screen for 3 s (the black and white
// flag); the cell's small amber pop-up of 0.4.6-0.4.36 was hard to read
// under a safety car / VSC: below the game's SC delta (too fast), and the frame it went below (for SLOW DOWN)
bool g_sc_slow = false, g_sc_slow_rise = false;
bool g_quali = false;  // page 0's practice / qualifying layout (moved up in 1.2.0)
std::string g_lap_text = "-"; // the race page's LAP n/total, for the safety car screen (1.2.0)
bool g_fcy = false;    // 1.2.40: a safety car / VSC is out (whatever the page), for the team pages' layout
bool g_sc_scr = false; // 1.2.0: the race page's safety car layout is up (page 0, a race layout, SC / VSC out, no flag screen)
std::string g_warn_title = "WARNING", g_warn_sub;
double g_warn_left = 0.0;
void warn(const std::string &sub, const char *title = "WARNING", double secs = 3.0)
{
    g_warn_title = title;
    g_warn_sub = sub;
    g_warn_left = secs;
}

// an infringement in a few words (the F1 25 UDP specification's list), "" for one without a short name
const char *infringement(int i)
{
    switch (i) {
    case 0: case 1: return "BLOCKING";
    case 2: return "REVERSING";
    case 3: case 4: return "CAUSING A COLLISION";
    case 5: case 6: return "POSITION NOT GIVEN BACK";
    case 7: case 8: case 9: case 27: case 28: case 29: case 52: return "LEAVING THE TRACK";
    case 10: return "CROSSING THE PIT EXIT LINE";
    case 11: return "IGNORING BLUE FLAGS";
    case 12: return "IGNORING YELLOW FLAGS";
    case 13: case 14: case 15: case 16: case 46: return "DRIVE THROUGH NOT SERVED";
    case 17: return "PIT LANE SPEEDING";
    case 18: return "PARKED TOO LONG";
    case 19: case 23: case 24: return "TYRE REGULATIONS";
    case 20: return "TOO MANY PENALTIES";
    case 21: return "MULTIPLE WARNINGS";
    case 22: return "APPROACHING DISQUALIFICATION";
    case 25: case 26: return "TRACK LIMITS";
    case 30: return "WALL RIDING";
    case 31: return "FLASHBACK USED";
    case 32: return "RESET TO TRACK";
    case 33: return "BLOCKING THE PIT LANE";
    case 34: return "JUMP START";
    case 35: return "SAFETY CAR COLLISION";
    case 36: return "OVERTAKING THE SAFETY CAR";
    case 37: case 43: return "SAFETY CAR PACE";
    case 38: return "VSC PACE";
    case 39: case 40: return "FORMATION LAP";
    case 45: return "STOP-GO NOT SERVED";
    case 53: return "MANDATORY PIT STOP";
    default: return "";
    }
}

// Penalties and warnings: from the game's penalty events (the tap; they carry the reason) and, when no event came,
// from the counters (the live state or the packets) going up. A time penalty, drive through, stop-go or a
// disqualification takes the whole screen (white) for 4 s; a warning pops up amber in the cell, an invalidated
// lap red.
void penalties(const Reading &r, double dt)
{
    auto &G = udp_tap::g;
    static uint32_t seen = G.pen_seq;
    static double since_event = 99.0;
    auto full = [&](const std::string &title, const std::string &sub) {
        g_pen_title = title;
        g_pen_sub = sub;
        g_pen_left = 4.0;
    };
    since_event += dt;
    uint32_t seq = G.pen_seq;
    if (seq != seen) {
        seen = seq;
        since_event = 0.0;
        int type = G.pen_type, infr = G.pen_infr, secs = G.pen_time;
        std::string why = infringement(infr);
        swofty::log("[driver-display] penalty event: type %d infringement %d time %d", type, infr, secs);
        switch (type) {
        case 0: full("DRIVE THROUGH", why); break;
        case 1: full(secs > 0 ? "STOP-GO " + std::to_string(secs) + " SEC" : std::string("STOP-GO PENALTY"), why); break;
        case 2: full("GRID PENALTY", secs > 0 ? std::to_string(secs) + " PLACES" : why); break;
        case 3: full("SERVE PENALTY", why); break;
        case 4: full("+" + std::to_string(secs) + " SEC PENALTY", why); break;
        case 5: warn(why); break;
        case 6: full("DISQUALIFIED", why); break;
        case 10: case 11: case 12: case 13: case 14: case 15:
            pop("LAP INVALID", why.empty() ? std::string("-") : why, 'r', 2.5);
            break;
        default: break;
        }
    }
    // the counters: a rise with no event in the last 3 s
    static int pen = -1, dts = -1, sgs = -1, warns = -1, tl = -1;
    bool quiet = since_event > 3.0;
    if (quiet && pen >= 0 && r.pen_s > pen)
        full("+" + std::to_string(r.pen_s - pen) + " SEC PENALTY", "");
    if (quiet && dts >= 0 && r.dt > dts)
        full("DRIVE THROUGH", "SERVE IT IN THE PIT LANE");
    if (quiet && sgs >= 0 && r.sg > sgs)
        full("STOP-GO PENALTY", "SERVE IT IN THE PIT LANE");
    if (quiet && tl >= 0 && r.tl > tl)
        warn("TRACK LIMITS " + std::to_string(r.tl));
    else if (quiet && warns >= 0 && r.warns > warns)
        warn("WARNINGS " + std::to_string(r.warns));
    pen = r.pen_s;
    dts = r.dt;
    sgs = r.sg;
    warns = r.warns;
    tl = r.tl;
    if (g_pen_left > 0.0)
        g_pen_left -= dt;
    if (g_warn_left > 0.0)
        g_warn_left -= dt;
}

// The chequered flag: when it comes out (the live state's flag, or the game's event) the whole screen shows it for
// 4 s, FINAL LAP; when the player finishes (result status 3) for 8 s with the finishing position.
void chequered(const Reading &r, double dt)
{
    static bool first = true, was = false;
    static int last_result = -1;
    static uint32_t seen = 0;
    uint32_t seq = udp_tap::g.chqf_seq;
    bool event = !first && seq != seen;
    seen = seq;
    bool rise = !first && ((r.chequered && !was) || event);
    if (rise && r.result != 3) {
        g_chq_title = "CHEQUERED FLAG";
        g_chq_sub = "";
        g_chq_left = 4.0;
        swofty::log("[driver-display] chequered flag out (%s)", event ? "event" : "live state");
    }
    if (!first && last_result == 2 && r.result == 3) {
        char t[32];
        if (r.pos > 0) std::snprintf(t, sizeof t, "FINISHED P%d", r.pos);
        else std::snprintf(t, sizeof t, "FINISHED");
        g_chq_title = "CHEQUERED FLAG";
        g_chq_sub = t;
        g_chq_left = 8.0;
        swofty::log("[driver-display] finished: %s", t);
    }
    // out of the race (1.0.7): DNF, not classified, retired, white on black for 5 s (a disqualification has its
    // own screen from the penalty event)
    // the screen stays up until the player races again (a new session) (1.0.20: 1.0.7-1.0.19 gave it 5 s; until
    // 1.0.18 it happened to stay, as nothing else wrote the screen)
    static const char *out = nullptr;
    if (!first && last_result == 2 && (r.result == 4 || r.result == 6 || r.result == 7)) {
        out = r.result == 4 ? "DNF" : r.result == 6 ? "NOT CLASSIFIED" : "RETIRED";
        swofty::log("[driver-display] out of the race: result status %d", r.result);
    }
    if (out && r.result == 2)
        out = nullptr;
    if (out)
        warn("", out, 1.0);
    if (r.result >= 0)
        last_result = r.result;
    was = r.chequered;
    first = false;
    if (g_chq_left > 0.0)
        g_chq_left -= dt;
}

// DRS / overtake enabled or disabled for the race: the game says so at the top of its HUD (from lap 2, after a
// safety car, when the track dries), which a player without the HUD or in VR doesn't see; it takes the whole screen
// for 3 s, in blue (the game's overtake colour; disabled with the reason), under any flag, penalty or limiter screen. 2026 content
// calls it overtake. Every other event code is logged once, to learn what the season pack sends.
// 1.0.21: the start lights on (0 none), the fastest lap's line and time left
int g_lights = 0;
double g_lights_left = 0.0, g_fl_left = 0.0;
std::string g_fl_text;
// a race message across the bottom row (1.0.22): 'b' blue (information), 'a' amber (an incident); the latest replaces
// the one up
std::string g_row_text;
char g_row_colour = 'b';
double g_row_left = 0.0;
void row(const std::string &text, char colour, double secs = 3.0)
{
    g_row_text = text;
    g_row_colour = colour;
    g_row_left = secs;
}

// a car's driver code by its packet index (the live state's driver names: the packets' index is the live vehicle id)
std::string car_code(int idx)
{
    if (idx >= 0)
        if (auto n = swofty::live::driver_name(uint32_t(idx)); n && !n->empty())
            return driver_code(n->c_str());
    return idx >= 0 ? "CAR " + std::to_string(idx + 1) : std::string("-");
}

// fallbacks for a game with its UDP output off (the default, 1.1.8): launcher 1.10's race control events. Only
// used while the tap gets no lap packets, so a message never shows twice.
std::mutex g_rc_mu;
std::vector<int> g_rc_retired; // vehicle ids, from the flow thread's session.car.status
bool udp_events() { return udp_tap::fresh(udp_tap::g.lap_tick); }

// ------------------------------------------------------------------ radio tags (1.2.3)
// The race engineer's important radio calls as a short tag across the bottom row (the user's design: the row
// holds three or four words, and the engineer says the rest in your ear). Launcher 1.10.20's speech.line events
// give each line's name (its subtitle key's stem, "eng_stratchange_box_02"); the game names its lines by subject,
// so the tag comes from the name, the first prefix that matches. What the dash already shows from its own data
// (punctures, the safety car / VSC / red flag, penalties, the pit window, wing damage) has no tag here. Every
// engineer line is logged with its tag (or none), to tune this list from a race's log.
struct RadioTag {
    const char *prefix;
    const char *tag; // nullptr: not shown
    char colour;     // 'b' blue (strategy, weather), 'a' amber (damage, faults), 'r' red (failure)
    bool box_if_pit; // damage: "  BOX" when the line's name says to pit for it (_pit / repair)
};
const RadioTag kRadioTags[] = {
    // strategy and pit calls
    {"eng_stratchange_box", "BOX THIS LAP", 'b', false},
    {"eng_vc_fl_box", "BOX THIS LAP", 'b', false},
    {"eng_pit_conf", "BOX CONFIRMED", 'b', false},
    {"eng_newstrat_damage", "NEW STRATEGY  DAMAGE", 'a', false},
    {"eng_newstrat_weather", "NEW STRATEGY  WEATHER", 'b', false},
    {"eng_newstrat_lowtyrewear", "NEW STRATEGY  TYRES", 'b', false},
    {"eng_newstrat_wear", "NEW STRATEGY  TYRES", 'b', false},
    {"eng_strat_lowwear", "NEW STRATEGY  TYRES", 'b', false},
    {"eng_sc_pitstrat_good", "BOX UNDER THE SC", 'b', false},
    {"eng_sc_pitstrat_bad", "STAY OUT UNDER THE SC", 'b', false},
    {"eng_sc_pitstrat", "SC PIT STRATEGY", 'b', false},
    {"eng_tyre_weather_strat", "TYRES FOR THE WEATHER", 'b', false},
    {"eng_red_flag_entered_strat", "RED FLAG STRATEGY", 'b', false},
    // weather
    {"eng_rainsoon", "RAIN SOON", 'b', false},
    {"eng_dry_rain", "RAIN STARTING", 'b', false},
    {"eng_drysoon", "DRYING SOON", 'b', false},
    {"eng_rain_dry", "TRACK DRYING", 'b', false},
    {"eng_poorweather", "POOR WEATHER", 'b', false},
    {"eng_wetness_", "TRACK CONDITIONS CHANGING", 'b', false},
    // failures and faults
    {"eng_mechfailure", "MECHANICAL FAILURE", 'r', false},
    {"eng_pu_fault_mgu", "MGU FAULT", 'a', false},
    {"eng_pu_fault_turbo", "TURBO FAULT", 'a', false},
    {"eng_pu_fault_energystore", "ENERGY STORE FAULT", 'a', false},
    {"eng_pu_fault_controlelectronics", "ELECTRONICS FAULT", 'a', false},
    {"eng_pu_fault_gb", "GEARBOX FAULT", 'a', false},
    {"eng_pu_fault", "POWER UNIT FAULT", 'a', false},
    {"eng_enginefault", "ENGINE FAULT", 'a', false},
    {"eng_gearbox_jam", "GEARBOX FAULT", 'a', false},
    {"eng_dam_gb", "GEARBOX DAMAGE", 'a', false},
    {"eng_randomfault_brakes", "BRAKE FAULT", 'a', false},
    {"eng_randomfault_cooling", "COOLING FAULT", 'a', false},
    {"eng_randomfault", "CAR FAULT", 'a', false},
    {"eng_drs_faultahd", nullptr, 0, false}, // the car ahead's DRS
    {"eng_drs_fault", "DRS FAULT", 'a', false},
    {"eng_faultfixed", "FAULT FIXED", 'b', false},
    // damage the dash has no own alert for
    {"eng_pldf_dam", "DIFFUSER DAMAGE", 'a', true},
    {"eng_plub_dam", "FLOOR DAMAGE", 'a', true},
    {"eng_plbw_dam", "BODYWORK DAMAGE", 'a', true},
    {"eng_pl_sus_dam", "SUSPENSION DAMAGE", 'a', true},
    // wing damage: only the calls to pit for it (the dash shows the damage itself)
    {"eng_plrw_dam", "REAR WING", 'a', true},
    {"eng_plfw_dam", "FRONT WING", 'a', true},
};

// the tag of an engineer line's name; nullptr when it has none
const RadioTag *radio_tag(std::string_view stem)
{
    for (const RadioTag &t : kRadioTags)
        if (stem.rfind(t.prefix, 0) == 0)
            return t.tag ? &t : nullptr;
    return nullptr;
}

std::vector<std::string> g_radio_stems; // engineer lines posted, from the flow thread (g_rc_mu)

std::string g_msg_title, g_msg_sub;
int g_msg_tone = 6; // the blue screen: the game's overtake colour
double g_msg_left = 0.0;

// ------------------------------------------------------------------ the Ferrari's own page 0 (1.2.18)
// The 2026 Ferrari shows Leclerc's 2026 race page (Ferrari's own layout) on page 0 in every session; the layout's
// fp_* elements are Ferrari-only, the moved values keep their own switches. Values the other cars' page 0 also
// shows are read back from their leaves (Leaf::last).
// ------------------------------------------------------------------ the 2026 McLaren's own pages (1.2.27)
// Its switches and values (the shared ones, fp_spd / fp_delta / fp_togo and the like, come from fp_page): the delta's
// colour, the MGU-K line, the battery's 13 segments, the revs' 20, the interval list (two or three cars either side).
void mc_page(const Reading &r, int soc, bool on)
{
    char t[48];
    const char st = g_fps;
    auto in = [&](const char *states) { return on && st != 0 && std::strchr(states, st) != nullptr; };
    // the delta: green gaining, red losing (race, car page), yellow behind the best (practice / qualifying)
    const std::string &d = leaf(g_quali ? "qdelta" : "delta").last;
    bool dneg = !d.empty() && d[0] == '-' && d != "--.--", dpos = !d.empty() && d[0] == '+';
    put_flag("mc_dneg", in("rdq") && dneg);
    put_flag("mc_dpos_r", in("rd") && dpos);
    put_flag("mc_dpos_q", in("q") && dpos);
    put_flag("mc_dnone", in("rdq") && !dneg && !dpos);
    // the MGU-K line: orange deploying, red harvesting, grey otherwise (the car page's sits lower)
    bool dep = false, har = false;
    float rpm_limit = 0.f;
    if (auto me = swofty::live::player()) {
        if (swofty::live::has(*me, SWOFTY_LIVE_HAVE_ERS_POWER)) {
            dep = me->ers_deploy > 1000.f;
            har = !dep && me->ers_harvest_k > 1000.f;
        }
        if (swofty::live::has(*me, SWOFTY_LIVE_HAVE_RPM))
            rpm_limit = me->rpm_limit;
    }
    bool kl = in("rqsp"), kd = in("d");
    put_flag("mc_k_d", kl && dep);
    put_flag("mc_k_h", kl && har);
    put_flag("mc_k_i", kl && !dep && !har);
    put_flag("mc_kd_d", kd && dep);
    put_flag("mc_kd_h", kd && har);
    put_flag("mc_kd_i", kd && !dep && !har);
    // the battery (13 segments, the bottom one first) and the revs (20, against the rev limit)
    for (int i = 0; i < 13; i++) {
        bool lit = on && r.soc >= 0 && soc > i * 100 / 13;
        std::snprintf(t, sizeof t, "mc_seg%02d", i);
        put_flag(t, lit && !g_overtake);
        std::snprintf(t, sizeof t, "mc_sob%02d", i);
        put_flag(t, lit && g_overtake);
    }
    float lim = rpm_limit > 3000.f ? rpm_limit : 13000.f;
    float frac = r.rpm > 0 ? float(r.rpm) / lim : 0.f;
    for (int i = 0; i < 20; i++) {
        std::snprintf(t, sizeof t, "mc_rpm%02d", i);
        put_flag(t, on && frac > i / 20.f);
    }
    // the interval list (race): the nearest five cars, up to three ahead, code and gap (ahead negative)
    std::string code[5], gap[5];
    int rows = 0;
    if (in("r"))
        if (auto me = swofty::live::player()) {
            auto o = swofty::live::order();
            int rk = o.rank_of(me->vehicle), n = int(o.ids.size());
            auto gap_of = [](const swofty::live::Car &c) -> int {
                return swofty::live::has(c, SWOFTY_LIVE_HAVE_GAP_AHEAD) && c.gap_ahead_ms != SWOFTY_LIVE_NO_GAP
                           ? int(c.gap_ahead_ms) : -1;
            };
            if (rk >= 0) {
                int ahead = std::min(rk, std::max(3, 5 - (n - 1 - rk)));
                int first = rk - ahead;
                // cumulative gaps: ahead, the player's own gap then each car's in front; behind, each car's own
                std::vector<int> cum(size_t(n), -1);
                cum[size_t(rk)] = 0;
                for (int k = rk - 1; k >= 0; k--) {
                    auto below = swofty::live::car(o.ids[size_t(k + 1)]);
                    int g = below ? gap_of(*below) : -1;
                    cum[size_t(k)] = (cum[size_t(k + 1)] >= 0 && g >= 0) ? cum[size_t(k + 1)] + g : -1;
                }
                for (int k = rk + 1; k < n; k++) {
                    auto c = swofty::live::car(o.ids[size_t(k)]);
                    int g = c ? gap_of(*c) : -1;
                    cum[size_t(k)] = (cum[size_t(k - 1)] >= 0 && g >= 0) ? cum[size_t(k - 1)] + g : -1;
                }
                for (int k = first; k < n && rows < 5; k++) {
                    if (k == rk)
                        continue;
                    code[rows] = car_code(int(o.ids[size_t(k)]));
                    if (cum[size_t(k)] >= 0)
                        std::snprintf(t, sizeof t, "%c%.2f", k < rk ? '-' : '+', cum[size_t(k)] / 1000.0);
                    else
                        std::snprintf(t, sizeof t, "-");
                    gap[rows++] = t;
                }
            }
        }
    for (int i = 0; i < 5; i++) {
        std::snprintf(t, sizeof t, "mc_code%d", i);
        put(t, code[i]);
        std::snprintf(t, sizeof t, "mc_gap%d", i);
        put(t, gap[i]);
        std::snprintf(t, sizeof t, "mc_row%d", i);
        put_flag(t, i < rows);
    }
}

// ------------------------------------------------------------------ the 2026 Mercedes's own pages (1.2.31)
// Its switches: the header's 12 RPM lights (against the rev limit), the lit gear in the gear column, the battery's 20
// segments, the delta's cell tint (green gaining, red losing).
void me_page(const Reading &r, int soc, bool on)
{
    char t[24];
    const char st = g_fps;
    auto in = [&](const char *states) { return on && st != 0 && std::strchr(states, st) != nullptr; };
    float lim = 13000.f;
    if (auto me = swofty::live::player(); me && swofty::live::has(*me, SWOFTY_LIVE_HAVE_RPM) && me->rpm_limit > 3000.f)
        lim = me->rpm_limit;
    float frac = r.rpm > 0 ? float(r.rpm) / lim : 0.f;
    for (int i = 0; i < 12; i++) {
        std::snprintf(t, sizeof t, "me_rpm%02d", i);
        put_flag(t, on && frac > 0.4f + i * 0.05f); // the strip covers the top 60 % of the revs
    }
    for (int g = 1; g <= 8; g++) {
        std::snprintf(t, sizeof t, "me_g%d", g);
        put_flag(t, on && r.gear == g);
    }
    for (int i = 0; i < 20; i++) {
        bool lit = on && r.soc >= 0 && soc > i * 5;
        std::snprintf(t, sizeof t, "me_seg%02d", i);
        put_flag(t, lit && !g_overtake);
        std::snprintf(t, sizeof t, "me_sob%02d", i);
        put_flag(t, lit && g_overtake);
    }
    const std::string &d = leaf(g_quali ? "qdelta" : "delta").last;
    bool dneg = !d.empty() && d[0] == '-' && d != "--.--", dpos = !d.empty() && d[0] == '+';
    put_flag("me_dneg", in("rq") && dneg); // 1.2.35: not on the car page
    put_flag("me_dpos", in("rq") && dpos);
}

// ------------------------------------------------------------------ the 2026 Red Bull's own pages (1.2.37)
// Its switches: the battery's 16 segments (green; blue in the overtake zone), the light top left (the last sector's
// pace, 1.2.43), the tyre cells' temperature bands (blue under 75, green to 100, orange
// above: a guide, the game shares no windows).
void rb_page(const Reading &r, int soc, bool on)
{
    char t[24];
    const char st = g_fps;
    auto in = [&](const char *states) { return on && st != 0 && std::strchr(states, st) != nullptr; };
    for (int i = 0; i < 16; i++) {
        bool lit = on && r.soc >= 0 && soc > i * 100 / 16;
        std::snprintf(t, sizeof t, "rb_seg%02d", i);
        put_flag(t, lit && !g_overtake);
        std::snprintf(t, sizeof t, "rb_sob%02d", i);
        put_flag(t, lit && g_overtake);
    }
    // the light top left: the last sector's pace (the game's lap packets: 0 yellow, 1 green, 2 purple), grey before
    // there is one (1.2.43)
    {
        int last = -1;
        for (int i = 0; i < 3; i++)
            if (udp_tap::g.sector_ms[i] > 0)
                last = i;
        int col = last >= 0 && udp_tap::fresh(udp_tap::g.lap_tick) ? int(udp_tap::g.sector_col[last]) : -1;
        bool sq = in("rqp");
        put_flag("rb_sq_n", sq && col < 0);
        put_flag("rb_sq_y", sq && col == 0);
        put_flag("rb_sq_g", sq && col == 1);
        put_flag("rb_sq_p", sq && col == 2);
    }
    static const char *cn[] = {"fl", "fr", "rl", "rr"};
    bool temps = in("rqs") && udp_tap::fresh(udp_tap::g.tel_tick);
    for (int i = 0; i < 4; i++) {
        int c = temps ? int(udp_tap::g.tyre_inner[i]) : -1;
        char band = c <= 0 ? 0 : c < 75 ? 'k' : c <= 100 ? 'g' : 'o';
        for (char b : {'k', 'g', 'o'}) {
            std::snprintf(t, sizeof t, "rb_t%s_%c", cn[i], b);
            put_flag(t, band == b);
        }
    }
}

// ------------------------------------------------------------------ the 2026 Aston Martin's own pages (1.3.1)
// Its switches: the 30-light rev strip (against the rev limit), the fuel bar (32 segments: the fuel in the tank
// against the capacity from the game's packets, else against the most this session has seen), the battery (32,
// green; blue in the overtake zone), the tyres' side bars (temperature bands), the SC / VSC name.
void am_page(const Reading &r, int soc, bool on)
{
    char t[24];
    const char st = g_fps;
    auto in = [&](const char *states) { return on && st != 0 && std::strchr(states, st) != nullptr; };
    float lim = 13000.f;
    if (auto me = swofty::live::player(); me && swofty::live::has(*me, SWOFTY_LIVE_HAVE_RPM) && me->rpm_limit > 3000.f)
        lim = me->rpm_limit;
    float frac = r.rpm > 0 ? float(r.rpm) / lim : 0.f;
    for (int i = 0; i < 30; i++) {
        std::snprintf(t, sizeof t, "am_rpm%02d", i);
        put_flag(t, on && frac > i / 30.f);
    }
    // fuel: the tank against its capacity (else the most seen this session)
    static uint32_t gen = ~0u;
    static float most = 0.f;
    auto ses = swofty::live::session();
    if (uint32_t g = ses ? ses->generation : 0; g != gen) {
        gen = g;
        most = 0.f;
    }
    float tank = udp_tap::fresh(udp_tap::g.status_tick) ? float(udp_tap::g.fuel_tank) : -1.f;
    float cap = float(udp_tap::g.fuel_cap);
    most = std::max(most, tank);
    float full = cap > 1.f ? cap : most;
    int pct = tank >= 0.f && full > 1.f ? std::clamp(int(std::lround(tank / full * 100.f)), 0, 100) : -1;
    put("am_fuelpct", pct >= 0 ? std::to_string(pct) + "%" : std::string("-"));
    for (int i = 0; i < 32; i++) {
        std::snprintf(t, sizeof t, "am_fuel%02d", i);
        put_flag(t, on && pct > i * 100 / 32);
    }
    for (int i = 0; i < 32; i++) {
        bool lit = on && r.soc >= 0 && soc > i * 100 / 32;
        std::snprintf(t, sizeof t, "am_seg%02d", i);
        put_flag(t, lit && !g_overtake);
        std::snprintf(t, sizeof t, "am_sob%02d", i);
        put_flag(t, lit && g_overtake);
    }
    static const char *cn[] = {"fl", "fr", "rl", "rr"};
    bool temps = in("rqs") && udp_tap::fresh(udp_tap::g.tel_tick);
    for (int i = 0; i < 4; i++) {
        int c = temps ? int(udp_tap::g.tyre_inner[i]) : -1;
        char band = c <= 0 ? 0 : c < 75 ? 'k' : c <= 100 ? 'g' : 'o';
        for (char b : {'k', 'g', 'o'}) {
            std::snprintf(t, sizeof t, "am_t%s_%c", cn[i], b);
            put_flag(t, band == b);
        }
    }
    put("fp_scshort", r.sc == 2 ? "VSC" : "SC");
}

// ------------------------------------------------------------------ the 2026 Williams's own pages (1.3.4)
// Its switches: the battery's 12 segments (mint; blue in the overtake zone), the delta's colour (mint gaining,
// salmon losing). Colour is scarce, so a row of values is one widget whose text is picked here for the layout.
void wi_page(const Reading &r, int soc, bool on)
{
    char t[24];
    const char st = g_fps;
    auto in = [&](const char *states) { return on && st != 0 && std::strchr(states, st) != nullptr; };
    for (int i = 0; i < 12; i++) {
        bool lit = on && r.soc >= 0 && soc > i * 100 / 12;
        std::snprintf(t, sizeof t, "wi_seg%02d", i);
        put_flag(t, lit && !g_overtake);
        std::snprintf(t, sizeof t, "wi_sob%02d", i);
        put_flag(t, lit && g_overtake);
    }
    const std::string &d = leaf(g_quali ? "qdelta" : "delta").last;
    bool dneg = !d.empty() && d[0] == '-' && d != "--.--", dpos = !d.empty() && d[0] == '+';
    put_flag("wi_dneg", in("rq") && dneg);
    put_flag("wi_dpos", in("rq") && dpos);
    put_flag("wi_dnone", in("rq") && !dneg && !dpos);
    if (!on || st == 0)
        return;
    auto L = [](const char *n) -> std::string { return leaf(n).last.empty() ? std::string("-") : leaf(n).last; };
    // the top: the position (race), the session (practice / qualifying), PIT (pit page), and its subtitle
    int ty = udp_tap::g.session_type;
    put("wi_top", st == 'q' ? L("fp_sess") : st == 'p' ? std::string("PIT") : L("pos"));
    put("wi_sub", st == 'p'   ? "STRATEGY"
                  : st == 'q' ? (ty >= 1 && ty <= 4 ? "PRACTICE" : ty == 18 ? "TIME TRIAL" : "QUALIFYING")
                              : "RACE");
    // the strip
    put("wi_sl", st == 'q' ? L("fp_last") : st == 'p' ? L("pst") : L("cur"));
    put("wi_sm", st == 'p' ? L("lap") : L("pos"));
    put("wi_sr", st == 'q' ? L("fp_togo") : st == 'p' ? L("rejoin") : L("lap"));
    // the rows: race / practice (four), SC / car / pit page (three), the tyres (temperatures, wear on the pit page)
    bool rc = st == 'r';
    put("wi_l0", rc ? L("fp_gap0") : L("s1"));
    put("wi_l1", rc ? L("fp_gap1") : L("s2"));
    put("wi_l2", rc ? L("fuel") : L("s3"));
    put("wi_m0", st == 'd' ? L("mix") : st == 'p' ? L("fp_stint") : L("fuel"));
    put("wi_m1", st == 'd' ? L("diff") : st == 'p' ? L("fp_wing") : L("pw"));
    put("wi_m2", L("bbal"));
    static const char *cn[] = {"fl", "fr", "rl", "rr"};
    for (int i = 0; i < 4; i++) {
        std::snprintf(t, sizeof t, "%c_%s", st == 'p' ? 'w' : 't', cn[i]);
        std::string v = L(t);
        std::snprintf(t, sizeof t, "wi_t%d", i);
        put(t, v);
    }
}

// ------------------------------------------------------------------ the 2026 Audi's own pages (1.3.7)
// Its switches: the battery's 24 segments, left to right (green; blue in the overtake zone).
void au_page(const Reading &r, int soc, bool on)
{
    char t[24];
    for (int i = 0; i < 24; i++) {
        bool lit = on && r.soc >= 0 && soc > i * 100 / 24;
        std::snprintf(t, sizeof t, "au_seg%02d", i);
        put_flag(t, lit && !g_overtake);
        std::snprintf(t, sizeof t, "au_sob%02d", i);
        put_flag(t, lit && g_overtake);
    }
}

// ------------------------------------------------------------------ the 2026 Haas's own pages (1.3.9)
// Its switches: the battery's bar across the top (25 pieces, left to right; green, blue in the overtake zone).
void hs_page(const Reading &r, int soc, bool on)
{
    char t[24];
    for (int i = 0; i < 25; i++) {
        bool lit = on && r.soc >= 0 && soc > i * 4;
        std::snprintf(t, sizeof t, "hs_seg%02d", i);
        put_flag(t, lit && !g_overtake);
        std::snprintf(t, sizeof t, "hs_sob%02d", i);
        put_flag(t, lit && g_overtake);
    }
}

// ------------------------------------------------------------------ the 2026 Alpine's own pages (1.3.10)
// Its switches: the battery column (32 segments, bottom up; green, blue in the overtake zone); its value: the
// clock (the PC's local time) on the race and practice pages.
void al_page(const Reading &r, int soc, bool on)
{
    char t[24];
    for (int i = 0; i < 32; i++) {
        bool lit = on && r.soc >= 0 && soc > i * 100 / 32;
        std::snprintf(t, sizeof t, "al_seg%02d", i);
        put_flag(t, lit && !g_overtake);
        std::snprintf(t, sizeof t, "al_sob%02d", i);
        put_flag(t, lit && g_overtake);
    }
    if (!on)
        return;
    std::time_t now = std::time(nullptr);
    std::tm lt{};
    localtime_s(&lt, &now);
    std::snprintf(t, sizeof t, "%02d:%02d:%02d", lt.tm_hour, lt.tm_min, lt.tm_sec);
    put("al_clock", t);
}

const char *session_name(int type); // below

void fp_page(const Reading &r, int soc)
{
    char t[64];
    const char st = g_fps;
    auto in = [st](const char *states) { return st != 0 && std::strchr(states, st) != nullptr; };
    // the layout sets (fp_combos.hpp): fp_<states> on while the Ferrari's layout is one of them
    const bool F = g_team == 'f', MC = g_team == 'm';
    for (const char *c : kFpCombos)
        put_flag((std::string("fp_") + c).c_str(), F && in(c));
    for (const char *c : kMcCombos)
        put_flag((std::string("mc_") + c).c_str(), MC && in(c));
    mc_page(r, soc, MC);
    const bool ME = g_team == 'e';
    for (const char *c : kMeCombos)
        put_flag((std::string("me_") + c).c_str(), ME && in(c));
    me_page(r, soc, ME);
    const bool RB = g_team == 'b';
    for (const char *c : kRbCombos)
        put_flag((std::string("rb_") + c).c_str(), RB && in(c));
    rb_page(r, soc, RB);
    const bool AM = g_team == 'a';
    for (const char *c : kAmCombos)
        put_flag((std::string("am_") + c).c_str(), AM && in(c));
    am_page(r, soc, AM);
    const bool WI = g_team == 'w';
    for (const char *c : kWiCombos)
        put_flag((std::string("wi_") + c).c_str(), WI && in(c));
    wi_page(r, soc, WI);
    const bool AU = g_team == 'u';
    for (const char *c : kAuCombos)
        put_flag((std::string("au_") + c).c_str(), AU && in(c));
    au_page(r, soc, AU);
    const bool HS = g_team == 'h';
    for (const char *c : kHsCombos)
        put_flag((std::string("hs_") + c).c_str(), HS && in(c));
    hs_page(r, soc, HS);
    const bool AL = g_team == 'l';
    for (const char *c : kAlCombos)
        put_flag((std::string("al_") + c).c_str(), AL && in(c));
    al_page(r, soc, AL);
    if (!g_fp) { // everything Ferrari-only off
        for (const char *k : {"fp_sec_on", "fp_sec_p", "fp_sec_g", "fp_sec_y", "fp_k_dep", "fp_k_har", "fp_gaps",
                              "fp_dneg", "fp_dpos", "fp_dnone", "fp_dsc"})
            put_flag(k, false);
        for (int i = 0; i < 20; i++) {
            std::snprintf(t, sizeof t, "fp_seg%02d", i);
            put_flag(t, false);
            std::snprintf(t, sizeof t, "fp_sob%02d", i);
            put_flag(t, false);
        }
        return;
    }
    // speed in the setting's unit, revs as a number
    bool mph = std::string_view(speed_unit) == "mph";
    int kmh = r.speed_kmh < 0 ? 0 : r.speed_kmh;
    put("fp_spd", std::to_string(mph ? int(std::lround(kmh * 0.621371)) : kmh));
    put("fp_rev", std::to_string(r.rpm < 0 ? 0 : r.rpm));
    // the lap alone, laps to go (race) or the session's time left (practice / qualifying)
    put("fp_lap", std::to_string(r.lap >= 0 ? r.lap + 1 : 0));
    if (g_quali) {
        put("fp_togo", leaf("sess_left").last.empty() ? std::string("--:--") : leaf("sess_left").last);
    } else {
        int total = udp_tap::g.total_laps;
        put("fp_togo", total > 0 && r.lap >= 0 ? std::to_string(std::max(0, total - (r.lap + 1))) : std::string("-"));
    }
    // the delta: the race's (the reference lap, or the SC delta) or, in practice / qualifying, to the best lap; its
    // box green gaining, red losing, grey before there is one, yellow under a SC
    const std::string &d = leaf(g_quali ? "qdelta" : "delta").last;
    put("fp_delta", d.empty() ? std::string("--.--") : d);
    bool dneg = !d.empty() && d[0] == '-' && d != "--.--", dpos = !d.empty() && d[0] == '+';
    bool dset = F && in("rdq");
    put_flag("fp_dneg", dset && dneg);
    put_flag("fp_dpos", dset && dpos);
    put_flag("fp_dnone", dset && !dneg && !dpos);
    put_flag("fp_dsc", F && st == 's');
    put("fp_scname", r.sc == 2 ? "VIRTUAL SAFETY CAR" : "SAFETY CAR");
    // the sector just finished and its colour (the game's lap packets: 0 yellow, 1 green, 2 purple): race, car page
    {
        int last = -1;
        for (int i = 0; i < 3; i++)
            if (udp_tap::g.sector_ms[i] > 0)
                last = i;
        bool on = F && in("rd") && last >= 0 && udp_tap::fresh(udp_tap::g.lap_tick);
        int col = on ? int(udp_tap::g.sector_col[last]) : -1;
        put("fp_sec", on ? "S" + std::to_string(last + 1) : std::string("-"));
        put_flag("fp_sec_on", on);
        put_flag("fp_sec_y", on && col == 0);
        put_flag("fp_sec_g", on && col == 1);
        put_flag("fp_sec_p", on && col == 2);
    }
    // practice / qualifying: the session (FP2, Q1), the laps on these tyres, the best lap
    {
        std::string sn = session_name(udp_tap::g.session_type);
        if (sn == "P1" || sn == "P2" || sn == "P3") sn = "F" + sn;
        else if (sn == "PRACTICE") sn = "FP";
        else if (sn == "QUALI") sn = "Q";
        else if (sn == "ONE-SHOT") sn = "OSQ";
        else if (sn.size() > 4) sn = sn.substr(0, 4);
        put("fp_sess", sn);
        put("fp_stint", r.tyre_age >= 0 ? std::to_string(r.tyre_age) : std::string("-"));
        float best = g_game.best_s > 0.f ? g_game.best_s : g_laps.best_s;
        put("fp_pb", best > 0.f ? laptime(best) : std::string("-:--.---"));
    }
    // the MGU-K: deploying (blue) or harvesting (red) from the live state's ERS power, nothing when idle
    {
        bool dep = false, har = false;
        if (auto me = swofty::live::player(); me && swofty::live::has(*me, SWOFTY_LIVE_HAVE_ERS_POWER)) {
            dep = me->ers_deploy > 1000.f;              // W
            har = !dep && me->ers_harvest_k > 1000.f;   // the MGU-K's own harvest
        }
        put_flag("fp_k_dep", F && dep);
        put_flag("fp_k_har", F && har);
    }
    // ERS SOC: the number and 20 segments of 5 %
    put("soc_n", std::to_string(soc));
    for (int i = 0; i < 20; i++) {
        std::snprintf(t, sizeof t, "fp_seg%02d", i);
        bool lit = F && r.soc >= 0 && soc > i * 5;
        put_flag(t, lit && !g_overtake);
        std::snprintf(t, sizeof t, "fp_sob%02d", i);
        put_flag(t, lit && g_overtake); // 1.2.35: blue in the overtake zone
    }
    // the car ahead and the car behind (race): their codes are code_a / code_b; the gaps with 2 decimals, the car
    // ahead negative and the car behind positive (Ferrari's own signs)
    {
        if (r.gap_ms > 0 && r.pos > 1) std::snprintf(t, sizeof t, "-%.2f", r.gap_ms / 1000.0);
        else std::snprintf(t, sizeof t, "-");
        put("fp_gap0", t);
        if (r.gap_b_ms > 0) std::snprintf(t, sizeof t, "+%.2f", r.gap_b_ms / 1000.0);
        else std::snprintf(t, sizeof t, "-");
        put("fp_gap1", t);
        put_flag("fp_gaps", F && st == 'r');
    }
}

// ------------------------------------------------------------------ Immersive Pitstops link (1.2.16)
// When Immersive Pitstops (sedz.immersive-pitstops) is installed too, it publishes its stuck nuts, loose wheels and
// retirements as a launcher service (ip_link.hpp, the same file in both repos) and the display shows them. Without
// it the service never appears and nothing here runs: the display is exactly as before. Read only.
std::atomic<bool (*)(uint32_t, ip_link::Car *)> g_ip_car{nullptr};
std::atomic<bool (*)()> g_ip_fmode{nullptr}; // 1.2.25: Immersive Pitstops' Ferrari Mode option (1.0.23+)

// ------------------------------------------------------------------ Ferrari Mode (1.2.25, the user's joke)
// With Immersive Pitstops' Ferrari Mode on and the 2026 Ferrari's own pages showing, the dash plays along: K1 IS
// AVAILABLE whenever the battery is high, retirements MUST BE THE (a part), the pit page's numbers are off (a pit
// window a few laps late, a rejoin place a few places out, the wing call the wrong way round, another compound, a
// slower stop, shuffled wear), and the engineer's BOX CONFIRMED is WE ARE CHECKING. Nothing else changes.
bool g_meme = false;

uint32_t meme_hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

const char *meme_part(uint32_t seed)
{
    static const char *parts[] = {"ENGINE", "GEARBOX", "TURBO", "PU", "BRAKES", "CLUTCH", "HYDRAULICS", "MGU-K"};
    return parts[meme_hash(seed) % 8];
}

// a retirement across the bottom row: RETIRED  VER, or in Ferrari Mode VER: MUST BE THE GEARBOX
std::string retired_row(int car)
{
    if (!g_meme)
        return "RETIRED  " + car_code(car);
    auto ses = swofty::live::session();
    return car_code(car) + ": MUST BE THE " + meme_part(uint32_t(car) * 7919u + (ses ? ses->generation : 0));
}

inline bool on_page(int p, bool v); // below

void meme_frame(const Reading &r, int soc)
{
    auto fm = g_ip_fmode.load();
    bool on = g_fer && fm && fm();
    if (on != g_meme) {
        g_meme = on;
        swofty::log("[driver-display] Ferrari Mode %s", on ? "on: the dash plays along" : "off");
    }
    if (!g_meme)
        return;
    char t[48];
    auto ses = swofty::live::session();
    uint32_t gen = ses ? ses->generation : 0;
    // K1 IS AVAILABLE, every 20 s while the battery is at 80 % or more in a race (not in the pit lane, when the
    // bottom row is free)
    {
        static double since = 0.0;
        since += g_step;
        bool in_pit = false;
        if (auto me = swofty::live::player())
            in_pit = swofty::live::in_pit(*me);
        if (!g_quali && r.soc >= 0 && soc >= 80 && !in_pit && since >= 20.0 && g_row_left <= 0.0) {
            row("K1 IS AVAILABLE", 'b', 3.0);
            since = 0.0;
        }
    }
    // the pit page, a little off (new numbers each lap)
    uint32_t seed = gen * 131u + uint32_t(r.lap < 0 ? 0 : r.lap);
    {
        int a = 0, b = 0;
        if (std::sscanf(leaf("pw").last.c_str(), "%d-%d", &a, &b) == 2) {
            int k = 2 + int(meme_hash(seed) % 4);
            std::snprintf(t, sizeof t, "%d-%d", a + k, b + k);
            put("pw", t);
        }
        int p = 0;
        if (std::sscanf(leaf("rejoin").last.c_str(), "P%d", &p) == 1) {
            int k = 3 + int(meme_hash(seed + 1) % 6);
            p = (meme_hash(seed + 2) & 1) ? std::min(22, p + k) : std::max(1, p - k);
            std::snprintf(t, sizeof t, "P%02d", p);
            put("rejoin", t);
        }
        const std::string &w = leaf("fp_wing").last;
        if (w == "YES" || w == "NO") {
            bool fix = w == "NO"; // the wrong way round
            put("fp_wing", fix ? "YES" : "NO");
            put_flag("fp_wingfix", g_fps == 'p' && fix);
        }
        static const char *names[] = {"SOFT", "MEDIUM", "HARD", "INTER", "WET"};
        static const char tags[] = {'s', 'm', 'h', 'i', 'w'};
        const std::string &tyre = leaf("tyre").last;
        int real = -1;
        for (int i = 0; i < 5; i++)
            if (tyre == names[i])
                real = i;
        if (real >= 0) {
            int fake = (real + 1 + int(meme_hash(seed + 3) % 4)) % 5;
            put("tyre", names[fake]);
            for (int i = 0; i < 5; i++) {
                char k[8];
                std::snprintf(k, sizeof k, "ty_%c", tags[i]);
                put_flag(k, on_page(2, i == fake));
            }
        }
        float pst = 0.f;
        if (std::sscanf(leaf("pst").last.c_str(), "%f", &pst) == 1 && pst > 0.f) {
            std::snprintf(t, sizeof t, "%.2f", double(pst) * (1.6 + (meme_hash(seed + 4) % 15) / 10.0));
            put("pst", t);
        }
        // the wear shuffled round the corners, each a few points out
        static const char *corners[] = {"fl", "fr", "rl", "rr"};
        int wear[4];
        bool known = true;
        for (int i = 0; i < 4; i++) {
            std::snprintf(t, sizeof t, "w_%s", corners[i]);
            if (std::sscanf(leaf(t).last.c_str(), "%d", &wear[i]) != 1)
                known = false;
        }
        if (known) {
            int shift = 1 + int(meme_hash(seed + 5) % 3);
            for (int i = 0; i < 4; i++) {
                int v = wear[(i + shift) % 4] + int(meme_hash(seed + 6 + uint32_t(i)) % 15) - 4;
                v = std::max(0, std::min(100, v));
                char k[16];
                std::snprintf(k, sizeof k, "w_%s", corners[i]);
                std::snprintf(t, sizeof t, "%d%%", v);
                put(k, t);
                char band = v >= int(wear_red) ? 'r' : v >= int(wear_amber) ? 'a' : 'g';
                for (char c : {'g', 'a', 'r'}) {
                    std::snprintf(k, sizeof k, "w%c_%s", c, corners[i]);
                    put_flag(k, on_page(2, band == c));
                }
            }
        }
    }
}

void ip_messages()
{
    auto fn = g_ip_car.load();
    if (!fn)
        return;
    auto pl = swofty::live::player(); // the live state's player (the UDP index is unknown without UDP)
    int me = pl ? int(pl->vehicle) : -1;
    constexpr int kCars = 32;
    static uint32_t gen = ~0u;
    static ip_link::Car was[kCars];
    auto ses = swofty::live::session();
    uint32_t g = ses ? ses->generation : 0;
    if (g != gen) {
        gen = g;
        for (auto &w : was)
            w = ip_link::Car{};
    }
    for (int i = 0; i < kCars; i++) {
        ip_link::Car c{};
        c.size = sizeof c;
        if (!fn(uint32_t(i), &c))
            continue;
        ip_link::Car &w = was[i];
        if (i == me) {
            if (c.stuck) { // the full screen for as long as the nut is stuck, with the time in the box
                char t[32];
                std::snprintf(t, sizeof t, "IN THE BOX  %.1f s", double(c.box_secs));
                g_msg_title = "STUCK WHEEL NUT";
                g_msg_sub = t;
                g_msg_tone = 2; // amber
                g_msg_left = 0.3;
                if (!w.stuck)
                    swofty::log("[driver-display] immersive pitstops: the player's stuck wheel nut");
            }
            if (c.wheel_off && !w.wheel_off) {
                g_msg_title = "WHEEL OFF";
                g_msg_sub = "";
                g_msg_tone = 1; // red
                g_msg_left = 3.0;
                swofty::log("[driver-display] immersive pitstops: the player's loose wheel came off");
            }
            if (c.retired && !w.retired) {
                g_msg_title = "RETIRED";
                g_msg_sub = ""; // no reason (1.2.17, the user's choice); Ferrari Mode: MUST BE THE (a part)
                if (g_meme) {
                    auto ses = swofty::live::session();
                    g_msg_sub = std::string("MUST BE THE ") + meme_part(uint32_t(i) * 104729u + (ses ? ses->generation : 0));
                }
                g_msg_tone = 1;
                g_msg_left = 6.0;
                swofty::log("[driver-display] immersive pitstops: the player's car retired (reason %d)", int(c.retired));
            }
            // the loose wheel across the bottom row until the pit lane (kept up each frame)
            auto car = swofty::live::car(uint32_t(i));
            if (c.loose && !c.wheel_off && !c.retired && !(car && swofty::live::in_pit(*car)))
                row("LOOSE WHEEL  BOX NOW", 'r', 0.3);
            if (c.loose && !w.loose)
                swofty::log("[driver-display] immersive pitstops: the player's loose wheel");
        } else {
            if (c.stuck && !w.stuck) {
                row("STUCK NUT  " + car_code(i), 'a');
                swofty::log("[driver-display] immersive pitstops: car %d stuck wheel nut", i);
            }
            if (c.loose && !w.loose) {
                row("LOOSE WHEEL  " + car_code(i), 'a');
                swofty::log("[driver-display] immersive pitstops: car %d loose wheel", i);
            }
        }
        w = c;
    }
}

void race_messages(double dt)
{
    auto &G = udp_tap::g;
    static uint32_t se = G.drse_seq, sd = G.drsd_seq, sev = 0;
    const char *what = G.format >= 2026 ? "OVERTAKE" : "DRS";
    if (G.drse_seq != se) {
        se = G.drse_seq;
        g_msg_title = what;
        g_msg_sub = "ENABLED";
        g_msg_tone = 6;
        g_msg_left = 3.0;
        swofty::log("[driver-display] event: %s enabled", what);
    }
    if (G.drsd_seq != sd) {
        sd = G.drsd_seq;
        static const char *why[] = {"WET TRACK", "SAFETY CAR", "RED FLAG", "TOO EARLY IN THE RACE"};
        int rs = G.drsd_reason;
        g_msg_title = what;
        g_msg_sub = rs >= 0 && rs < 4 ? std::string("DISABLED: ") + why[rs] : std::string("DISABLED");
        g_msg_tone = 6;
        g_msg_left = 3.0;
        swofty::log("[driver-display] event: %s disabled (reason %d)", what, rs);
    }
    // 1.0.21: start lights, lights out (GO), the fastest lap (the bottom row), retirements and the player's contact
    // (the ERS SOC cell's pop-up: with the AI mods there are many, so no full screens)
    static uint32_t sl = G.lights_seq, so = G.lgot_seq, sf = G.ftlp_seq, sr = G.rtmt_seq, scl = G.coll_seq;
    if (G.lights_seq != sl) {
        sl = G.lights_seq;
        g_lights = bool(start_lights) ? int(G.lights) : 0;
        g_lights_left = bool(start_lights) ? 10.0 : 0.0;
    }
    if (G.lgot_seq != so) {
        so = G.lgot_seq;
        g_lights = 0;
        g_lights_left = 0.0;
        if (bool(start_lights)) {
            g_msg_title = "GO";
            g_msg_sub = "";
            g_msg_tone = 8; // green
            g_msg_left = 1.5;
        }
        swofty::log("[driver-display] event: lights out");
    }
    if (g_lights_left > 0.0)
        g_lights_left -= dt;
    bool lights = g_lights > 0 && g_lights_left > 0.0;
    put_flag("lights", lights);
    for (int i = 1; i <= 5; i++) {
        char k[8];
        std::snprintf(k, sizeof k, "lt%d", i);
        put_flag(k, lights && i <= g_lights);
    }
    int me = G.player;
    if (G.ftlp_seq != sf) {
        sf = G.ftlp_seq;
        float s = G.ftlp_time;
        char t[48];
        int ms = int(std::lround(double(s) * 1000.0));
        std::snprintf(t, sizeof t, "FASTEST LAP %s %d:%02d.%03d", car_code(G.ftlp_car).c_str(), ms / 60000,
                      ms / 1000 % 60, ms % 1000);
        g_fl_text = t;
        g_fl_left = 5.0;
        swofty::log("[driver-display] event: %s", t);
    }
    if (g_fl_left > 0.0)
        g_fl_left -= dt;
    if (g_row_left > 0.0)
        g_row_left -= dt;
    if (G.rtmt_seq != sr) {
        sr = G.rtmt_seq;
        if (G.rtmt_car != me) {
            row(retired_row(int(G.rtmt_car)), 'a');
            swofty::log("[driver-display] event: car %d retired", int(G.rtmt_car));
        }
    }
    {
        std::vector<int> out;
        {
            std::lock_guard<std::mutex> l(g_rc_mu);
            out.swap(g_rc_retired);
        }
        int mine = -1;
        if (auto p = swofty::live::player())
            mine = int(p->vehicle);
        if (!udp_events())
            for (int v : out)
                if (v != mine) {
                    row(retired_row(v), 'a');
                    swofty::log("[driver-display] race control: car %d retired (no UDP)", v);
                }
    }
    // the engineer's important calls (1.2.3)
    {
        std::vector<std::string> stems;
        {
            std::lock_guard<std::mutex> l(g_rc_mu);
            stems.swap(g_radio_stems);
        }
        for (const std::string &stem : stems) {
            const RadioTag *t = radio_tag(stem);
            std::string tag;
            if (t) {
                tag = t->tag;
                if (t->box_if_pit) {
                    bool pit = stem.find("_pit") != std::string::npos || stem.find("repair") != std::string::npos;
                    if (pit)
                        tag += "  BOX";
                    else if (tag == "REAR WING" || tag == "FRONT WING")
                        tag.clear(); // wing damage without a call to pit: the dash's own alert has it
                }
            }
            if (g_meme && tag == "BOX CONFIRMED")
                tag = "WE ARE CHECKING"; // Ferrari Mode (1.2.25)
            if (!tag.empty() && bool(radio_tags))
                row(tag, t->colour, 4.0);
            swofty::log("[driver-display] radio: %s -> %s", stem.c_str(), tag.empty() ? "(no tag)" : tag.c_str());
        }
    }
    if (G.coll_seq != scl) {
        scl = G.coll_seq;
        int a = G.coll_a, b = G.coll_b;
        if (a == me || b == me) {
            row("CONTACT  " + car_code(a == me ? b : a), 'a', 2.5);
            swofty::log("[driver-display] event: contact with car %d", a == me ? b : a);
        }
    }
    ip_messages(); // 1.2.16: only with Immersive Pitstops installed
    if (g_msg_left > 0.0)
        g_msg_left -= dt;
    if (G.event_seq != sev) {
        sev = G.event_seq;
        static std::unordered_set<uint32_t> seen;
        uint32_t c = G.event_code;
        if (seen.insert(c).second) {
            char code[5] = {char(c), char(c >> 8), char(c >> 16), char(c >> 24), 0};
            swofty::log("[driver-display] event code seen: %s", code);
        }
    }
}

// 1.0.21, in a race: the backmarker ahead (the game's own message isn't readable: worked out from the live state),
// the final lap, the pit window opening; in any session: rain in the forecast
void race_extras(const Reading &r)
{
    int type = udp_tap::g.session_type;
    bool race = type >= 15 && type <= 17;
    // backmarker: the nearest car within 200 m ahead on the road that is half a lap or more behind in the race, not
    // in the pit lane, still running; the pop-up once per car as the player closes in
    static int bm_last = -1;
    int bm = -1;
    auto me = swofty::live::player();
    auto ses = swofty::live::session();
    if (race && r.sc == 0 && me && ses && swofty::live::has(*ses, SWOFTY_LIVE_SHAVE_TRACK_LENGTH) &&
        swofty::live::has(*me, SWOFTY_LIVE_HAVE_PROGRESS) && ses->track_length_m > 500.f) {
        float L = ses->track_length_m, best = 200.f;
        for (uint32_t v : swofty::live::road_order()) {
            if (v == me->vehicle)
                continue;
            auto c = swofty::live::car(v);
            if (!c || !swofty::live::has(*c, SWOFTY_LIVE_HAVE_PROGRESS) || swofty::live::in_pit(*c))
                continue;
            if (swofty::live::has(*c, SWOFTY_LIVE_HAVE_CLASS) && c->result_status != 2)
                continue;
            float ahead = c->lap_m - me->lap_m;
            if (ahead < 0.f)
                ahead += L;
            if (ahead > 0.f && ahead < best && me->total_m - c->total_m > L * 0.5f) {
                best = ahead;
                bm = int(v);
            }
        }
    }
    if (bm >= 0 && bm != bm_last) {
        row("BACKMARKER  " + car_code(bm), 'b');
        swofty::log("[driver-display] backmarker ahead: car %d", bm);
    }
    bm_last = bm;
    // the final lap: a blue message over the screen as it starts
    static int last_lap = -1;
    int total = udp_tap::g.total_laps;
    if (race && total > 1 && r.lap >= 0 && r.lap != last_lap && r.lap + 1 == total) {
        g_msg_title = "FINAL LAP";
        g_msg_sub = "";
        g_msg_tone = 9; // white with black text: the white flag of the last lap (NASCAR, IndyCar) (1.1.2)
        g_msg_left = 3.0;
        swofty::log("[driver-display] final lap");
    }
    if (r.lap >= 0)
        last_lap = r.lap;
    // the pit window opening (the session packet's ideal lap)
    auto &G = udp_tap::g;
    bool sess = udp_tap::fresh(G.session_tick);
    static int pw_said = -1;
    int lap = r.lap >= 0 ? r.lap + 1 : 0, ideal = sess ? int(G.pit_ideal) : 0, latest = sess ? int(G.pit_latest) : 0;
    if (race && ideal > 0 && latest >= ideal && lap >= ideal && lap <= latest && pw_said != ideal) {
        row("PIT WINDOW OPEN", 'b');
        pw_said = ideal;
        swofty::log("[driver-display] pit window open (laps %d-%d)", ideal, latest);
    }
    // rain in the forecast while it's dry: once as the chance passes 50 %, again only after it fell below 30 %
    static bool rain_said = false;
    int r10 = sess ? int(G.rain10) : -1, wnow = sess ? int(G.weather) : -1, at = G.forecast_min;
    if (r10 >= 50 && wnow >= 0 && wnow <= 2 && !rain_said) {
        row((at > 0 ? "RAIN IN " + std::to_string(at) + " MIN  " : std::string("RAIN  ")) + std::to_string(r10) + "%", 'b', 4.0);
        rain_said = true;
        swofty::log("[driver-display] rain forecast: %d%% in %d min", r10, at);
    }
    if (r10 >= 0 && r10 < 30)
        rain_said = false;

    // 1.1.4, any session, across the bottom row in amber: damage (from the game's damage packet) and tyre wear
    // front / rear wing: as it rises 10 points or more past the last alert (a repair lowers the mark)
    if (udp_tap::fresh(G.damage_tick) && !G.from_launcher) {
        static int fw_said = 0, rw_said = 0;
        int fl = G.dmg_fwl, fr = G.dmg_fwr, fw = fl > fr ? fl : fr, rw = G.dmg_rw;
        if (fw >= 10 && fw >= fw_said + 10) {
            row("FRONT WING DAMAGE", 'a', 4.0); // (1.1.7: with the L / R % it ran past the row)
            swofty::log("[driver-display] alert: front wing damage %d%% / %d%%", fl, fr);
            fw_said = fw;
        } else if (fw < fw_said) {
            fw_said = fw;
        }
        if (rw >= 10 && rw >= rw_said + 10) {
            row("REAR WING DAMAGE", 'a', 4.0);
            swofty::log("[driver-display] alert: rear wing damage %d%%", rw);
            rw_said = rw;
        } else if (rw < rw_said) {
            rw_said = rw;
        }
    }
    // tyre wear, per tyre: past 75 % TYRE WEAR (amber), the point a puncture becomes possible; again after new tyres
    {
        static bool said[4] = {};
        static const char *cn[] = {"FL", "FR", "RL", "RR"};
        for (int i = 0; i < 4; i++) {
            float w = r.wear[i];
            if (w < 0.f)
                continue;
            if (w > 75.f && !said[i]) {
                row(std::string("TYRE WEAR  ") + cn[i] + "  " + std::to_string(int(std::lround(w))) + "%", 'a', 4.0);
                swofty::log("[driver-display] alert: tyre wear %s %.0f%%", cn[i], double(w));
                said[i] = true;
            } else if (w < 20.f) {
                said[i] = false;
            }
        }
    }
    // punctures (1.1.8): the game's own puncture state of each wheel (launcher 1.10's tyre damage library, Steam):
    // FL SLOW PUNCTURE (amber) when the tyre is punctured, FL FULL PUNCTURE (red) when it bursts; again after a
    // tyre change clears it
    if (me) {
        namespace td = swofty::tyre_damage;
        static td::State said[4] = {};
        for (unsigned i = 0; i < td::WHEELS; i++) {
            auto w = td::Wheel(i);
            auto st = td::state(*me, w);
            if (!st)
                break;
            if (*st == said[i])
                continue;
            if (*st == td::State::punctured)
                row(std::string(td::name(w)) + " SLOW PUNCTURE", 'a', 5.0);
            else if (*st == td::State::burst)
                row(std::string(td::name(w)) + " FULL PUNCTURE", 'r', 5.0);
            swofty::log("[driver-display] alert: %s tyre %s (was %s), deflation %.2f", td::name(w), td::name(*st),
                        td::name(said[i]), double(td::deflation(*me, w).value_or(0.f)));
            said[i] = *st;
        }
    }

    // 1.2.0, any session: where the yellow is (the launcher's race control flag state): "YELLOW SECTOR 2" in amber as a
    // sector's yellow comes out, "SECTOR 2 CLEAR" in blue as it ends; quiet under a safety car or red flag, and at the
    // first read of a session
    {
        static int was = -1;
        static uint32_t gen = 0;
        auto st = swofty::race_control::now();
        if (!st || !ses) {
            was = -1;
        } else {
            int now = st->yellow_sectors;
            if (ses->generation != gen) {
                gen = ses->generation;
                was = now;
            }
            if (was >= 0 && now != was && r.sc == 0 && !r.red_flag) {
                auto list = [](int bits) {
                    std::string out;
                    int n = 0;
                    for (int i = 0; i < 8; i++)
                        if (bits >> i & 1)
                            out += (n++ ? " + " : " ") + std::to_string(i + 1);
                    return std::make_pair(out, n);
                };
                int added = now & ~was, gone = was & ~now;
                if (added) {
                    auto [txt, n] = list(added);
                    row(std::string(n > 1 ? "YELLOW SECTORS" : "YELLOW SECTOR") + txt, 'a', 4.0);
                } else if (gone) {
                    auto [txt, n] = list(gone);
                    row(std::string(n > 1 ? "SECTORS" : "SECTOR") + txt + " CLEAR", 'b');
                }
                swofty::log("[driver-display] yellow sectors 0x%x -> 0x%x", was, now);
            }
            was = now;
        }
    }

    // Undercut calls (1.2.6, the user's rules; 1.2.2-1.2.5 called every stop of the car ahead or behind). Races from
    // lap 2, no safety car or red flag, and only around your own pit window (the game's, from its UDP session
    // packet: from 2 laps before its ideal lap to its latest), against a car on the same stop count as you:
    //   the car ahead, within the window gap, as your window opens   UNDERCUT CHANCE  LEC   (blue: pit to undercut)
    //   the car behind, within the window gap, pits                   UNDERCUT THREAT  NOR   (amber: pit to cover)
    //   and that car leaving the pit lane                             NOR OUT  P9  2.1s BEHIND
    // A neighbour counts only after 8 s next to you within the gap (1.2.2-1.2.5 took the last frame's: a leader
    // dropping through your place in the pit lane read as the car ahead). Overcut calls are out: whether staying out
    // pays can't be told from this data, and a wrong call costs the race.
    if (bool(undercut)) {
        namespace lv = swofty::live;
        constexpr double kSettle = 8.0, kRecent = 6.0;
        static double clock = 0.0;
        clock += g_step;
        struct Near {
            double since = -1.0, last = -1e9; // next to you within the gap: since when, and last seen so
        };
        static Near near_ah[SWOFTY_LIVE_MAX_CARS], near_bh[SWOFTY_LIVE_MAX_CARS];
        static bool was_in[SWOFTY_LIVE_MAX_CARS] = {};
        static bool threat[SWOFTY_LIVE_MAX_CARS] = {}; // an UNDERCUT THREAT out, its OUT line to come
        static int chance_said[SWOFTY_LIVE_MAX_CARS];  // your stop count when its UNDERCUT CHANCE was said (-1 none)
        static uint32_t gen = ~0u;
        if (ses && ses->generation != gen) {
            gen = ses->generation;
            for (int i = 0; i < SWOFTY_LIVE_MAX_CARS; i++) {
                near_ah[i] = near_bh[i] = Near{};
                was_in[i] = threat[i] = false;
                chance_said[i] = -1;
            }
        }
        int window_ms = int(undercut_window) * 1000;
        auto &G = udp_tap::g;
        // your pit window: the game's ideal and latest laps (0 when it has none)
        int ideal = sess ? int(G.pit_ideal) : 0, latest = sess ? int(G.pit_latest) : 0;
        int lap_now = r.lap + 1;
        bool window = ideal > 0 && latest >= ideal && lap_now >= ideal - 2 && lap_now <= latest;
        {
            static bool told = false;
            if (race && !told && !(ideal > 0)) {
                swofty::log("[driver-display] undercut calls: no pit window from the game yet (they need its UDP output)");
                told = true;
            }
        }
        bool on = race && me && r.sc == 0 && !r.red_flag && r.lap >= 1;
        auto o = lv::order();
        int mk = me ? o.rank_of(me->vehicle) : -1;
        auto gap_of = [](const lv::Car &c) {
            return lv::has(c, SWOFTY_LIVE_HAVE_GAP_AHEAD) && c.gap_ahead_ms != SWOFTY_LIVE_NO_GAP ? c.gap_ahead_ms : -1;
        };
        auto stops_of = [](const lv::Car &c) { return int(lv::pit_stops(c).value_or(0)); };
        int my_stops = me ? stops_of(*me) : 0;
        // who is next to you now, within the gap
        int ah = -1, bh = -1;
        if (mk > 0) {
            int g = gap_of(*me);
            if (g >= 0 && g <= window_ms)
                ah = int(o.ids[size_t(mk) - 1]);
        }
        if (mk >= 0 && size_t(mk) + 1 < o.ids.size())
            if (auto b = lv::car(o.ids[size_t(mk) + 1]); b && gap_of(*b) >= 0 && gap_of(*b) <= window_ms)
                bh = int(o.ids[size_t(mk) + 1]);
        auto settle = [&](Near *list, int id) {
            for (int i = 0; i < SWOFTY_LIVE_MAX_CARS; i++) {
                bool now = i == id;
                if (now && (list[i].since < 0.0 || clock - list[i].last > 1.0))
                    list[i].since = clock; // newly next to you (or again after a break)
                if (now)
                    list[i].last = clock;
            }
        };
        settle(near_ah, ah);
        settle(near_bh, bh);
        auto steady = [&](const Near &n) { return n.since >= 0.0 && clock - n.last <= kRecent && n.last - n.since >= kSettle; };
        auto against_me = [&](const lv::Car &c) {
            int rk = o.rank_of(c.vehicle);
            char g[24];
            if (rk == mk + 1 && gap_of(c) > 0)
                std::snprintf(g, sizeof g, "%.1fs BEHIND", gap_of(c) / 1000.0);
            else if (rk == mk - 1 && gap_of(*me) > 0)
                std::snprintf(g, sizeof g, "%.1fs AHEAD", gap_of(*me) / 1000.0);
            else
                std::snprintf(g, sizeof g, "%s", rk > mk ? "BEHIND" : "AHEAD");
            return std::string(g);
        };
        // the car ahead: an undercut chance once your window is near, once per car and stint
        if (on && window && ah >= 0 && steady(near_ah[ah]) && chance_said[ah] != my_stops && !lv::in_pit(*me))
            if (auto c = lv::car(uint32_t(ah)); c && !lv::in_pit(*c) && stops_of(*c) == my_stops) {
                row("UNDERCUT CHANCE  " + car_code(ah), 'b', 5.0);
                chance_said[ah] = my_stops;
                swofty::log("[driver-display] undercut chance: car %d %.1f s ahead, lap %d (window %d-%d)", ah,
                            gap_of(*me) / 1000.0, lap_now, ideal, latest);
            }
        for (uint32_t v = 0; v < SWOFTY_LIVE_MAX_CARS; v++) {
            auto c = lv::car(v);
            bool in = c && lv::in_pit(*c);
            bool entered = in && !was_in[v], left = c && !in && was_in[v];
            was_in[v] = in;
            if (!on || !c || v == me->vehicle)
                continue;
            // the car behind pits: a threat while your window is near and it was on your stop count
            if (entered && window && steady(near_bh[v]) && (stops_of(*c) == my_stops || stops_of(*c) == my_stops + 1) &&
                !lv::in_pit(*me)) {
                // (its count may already include this stop)
                row("UNDERCUT THREAT  " + car_code(int(v)), 'a', 5.0);
                threat[v] = true;
                swofty::log("[driver-display] undercut threat: car %u pitted behind, lap %d (window %d-%d)", v, lap_now,
                            ideal, latest);
            }
            if (left && threat[v]) {
                threat[v] = false;
                if (c->position > 0) {
                    std::string where = against_me(*c);
                    row(car_code(int(v)) + " OUT  P" + std::to_string(c->position) + "  " + where, 'a', 5.0);
                    swofty::log("[driver-display] car %u out of the pit lane: P%d, %s", v, c->position, where.c_str());
                }
            }
        }
    }

    // CATCH / HOLD (1.2.7, the user's request): once a lap, as you cross the line, the lap time you need to catch the
    // car ahead by the flag (CATCH LEC  1:31.5, or LEC IN 5 LAPS when you're already catching it), or to stay ahead
    // of a quicker car behind to the flag (HOLD NOR  1:32.1; it goes first). Every car's laps are timed here from the
    // live state's lap counter (no UDP needed for that); its pace is the average of its last 3 clean laps (no pit
    // stop, safety car, VSC or red flag, no flashback). The laps left come from the game's total laps (UDP). It
    // assumes everyone keeps their pace: stops still to come and tyre wear can move the target from lap to lap.
    if (bool(pace_targets)) {
        namespace lv = swofty::live;
        static double clock = 0.0;
        clock += g_step;
        struct Laps {
            int lap = -1;
            double start = -1.0; // when the lap now being driven started (-1: not timed: it didn't start at the line)
            bool dirty = false;  // a stop, a caution or a flashback in it
            float lm = 0.f;      // its lap distance, to see a flashback
            double t[3] = {};    // the last clean laps, newest first
            int n = 0;
        };
        static Laps laps[SWOFTY_LIVE_MAX_CARS];
        static uint32_t gen = ~0u;
        static bool hold_first = true;
        if (ses && ses->generation != gen) {
            gen = ses->generation;
            for (Laps &l : laps)
                l = Laps{};
        }
        bool caution = r.sc != 0 || r.red_flag;
        int my_new_lap = -1;
        for (uint32_t v = 0; v < SWOFTY_LIVE_MAX_CARS; v++) {
            auto c = lv::car(v);
            Laps &l = laps[v];
            if (!c || !lv::has(*c, SWOFTY_LIVE_HAVE_PROGRESS)) {
                l = Laps{};
                continue;
            }
            if (c->lap != l.lap) {
                if (c->lap == l.lap + 1 && l.start >= 0.0 && !l.dirty) {
                    for (int k = 2; k > 0; k--)
                        l.t[k] = l.t[k - 1];
                    l.t[0] = clock - l.start;
                    l.n = l.n < 3 ? l.n + 1 : 3;
                }
                bool timed = c->lap == l.lap + 1;
                l.lap = c->lap;
                l.start = timed ? clock : -1.0; // a jump that isn't one lap (a flashback, the start): not timed
                l.dirty = false;
                l.lm = c->lap_m; // (the distance starts again at the line: no flashback)
                if (me && v == me->vehicle && timed)
                    my_new_lap = c->lap;
            }
            if (lv::in_pit(*c) || caution || c->lap_m + 50.f < l.lm)
                l.dirty = true; // (the lap distance going back within a lap: a flashback)
            l.lm = c->lap_m;
        }
        auto pace = [&](uint32_t v) {
            const Laps &l = laps[v];
            if (l.n < 2)
                return -1.0;
            double sum = 0.0;
            for (int k = 0; k < l.n; k++)
                sum += l.t[k];
            return sum / l.n;
        };
        auto fmt = [](double secs) {
            char b[16];
            int ms = int(std::lround(secs * 10.0)); // tenths
            std::snprintf(b, sizeof b, "%d:%02d.%d", ms / 600, ms / 10 % 60, ms % 10);
            return std::string(b);
        };
        auto &G = udp_tap::g;
        int total = sess ? int(G.total_laps) : 0;
        if (race && me && my_new_lap >= 2 && !caution && total > 0) {
            int left = total - my_new_lap; // laps still to drive, the one just started included
            auto o = lv::order();
            int mk = o.rank_of(me->vehicle);
            auto gap_of = [](const lv::Car &c) {
                return lv::has(c, SWOFTY_LIVE_HAVE_GAP_AHEAD) && c.gap_ahead_ms != SWOFTY_LIVE_NO_GAP ? c.gap_ahead_ms : -1;
            };
            double mine = pace(me->vehicle);
            std::string hold, catch_;
            if (left >= 1 && mk >= 0 && size_t(mk) + 1 < o.ids.size()) {
                uint32_t b = o.ids[size_t(mk) + 1];
                auto bc = lv::car(b);
                double pb = pace(b);
                int g = bc ? gap_of(*bc) : -1;
                if (bc && !lv::in_pit(*bc) && g > 0 && g <= 10000 && pb > 0.0 && mine > 0.0 && pb < mine &&
                    (g / 1000.0) / (mine - pb) < left) // quicker, and it would catch you before the flag
                    hold = "HOLD " + car_code(int(b)) + "  " + fmt(pb + g / 1000.0 / left);
            }
            if (left >= 1 && mk > 0) {
                uint32_t a = o.ids[size_t(mk) - 1];
                auto ac = lv::car(a);
                double pa = pace(a);
                int g = gap_of(*me);
                if (ac && !lv::in_pit(*ac) && g > 0 && g <= 10000 && pa > 0.0) {
                    double gs = g / 1000.0;
                    if (mine > 0.0 && mine < pa && std::ceil(gs / (pa - mine)) <= left) {
                        int n = int(std::ceil(gs / (pa - mine)));
                        catch_ = car_code(int(a)) + " IN " + std::to_string(n) + (n == 1 ? " LAP" : " LAPS");
                    } else {
                        catch_ = "CATCH " + car_code(int(a)) + "  " + fmt(pa - gs / left);
                    }
                }
            }
            // one a lap: the threat from behind first, the catch on the next lap when both apply
            std::string said;
            char colour = 'b';
            if (!hold.empty() && (catch_.empty() || hold_first)) {
                said = hold;
                colour = 'a';
                hold_first = false;
            } else if (!catch_.empty()) {
                said = catch_;
                hold_first = true;
            }
            if (!said.empty()) {
                row(said, colour, 5.0);
                swofty::log("[driver-display] pace: lap %d of %d, mine %.3f: %s", my_new_lap, total, mine, said.c_str());
            }
        }
    }
}

void popups(const Reading &r, const std::string &mode, double dt)
{
    static int last_mode = -2;
    if (r.ers_mode != last_mode) {
        if (last_mode >= 0 && r.ers_mode >= 0)
            pop("ERS MODE", mode);
        swofty::log("[driver-display] ERS mode %d -> %d", last_mode, r.ers_mode);
        last_mode = r.ers_mode;
    }
    // the settings only the game's UDP packets carry: a change pops up, as on Wheel Dash and a real wheel
    static int last_mix = -1, last_bbal = -1, last_diff = -1;
    if (udp_tap::fresh(udp_tap::g.status_tick)) {
        int mix = udp_tap::g.mix, bbal = udp_tap::g.bbal;
        if (last_mix >= 0 && mix != last_mix) pop("FUEL MIX", mix_name(mix));
        if (last_bbal >= 0 && bbal != last_bbal) pop("BRAKE BIAS", std::to_string(bbal) + ".0");
        last_mix = mix;
        last_bbal = bbal;
    }
    if (udp_tap::fresh(udp_tap::g.setup_tick)) {
        int diff = udp_tap::g.diff;
        if (last_diff >= 0 && diff != last_diff) pop("DIFF", std::to_string(diff) + "%");
        last_diff = diff;
    }
    if (g_pop_left > 0.0)
        g_pop_left -= dt;
    race_extras(r);
    // the flag the game shows this car (its status packet), and the live state's yellow sectors for the
    // sector the car is in: a local yellow (no overtaking there) without a safety car, and the blue flag
    bool udp_status = udp_tap::fresh(udp_tap::g.status_tick);
    int fia = udp_status ? int(udp_tap::g.fia_flag) : 0;
    bool yellow = fia == 3 || r.sector_yellow, blue_flag = fia == 2;

    // Flags take the whole screen (as Wheel Dash had them): red flag, safety car / VSC / formation lap, yellow,
    // blue flag, by priority. The "flag_screen" setting: "brief" shows it for 4 s when a flag comes out (then the
    // strip along the top edge carries on), "full" for as long as it is out. Setting changes pop up in the ERS SOC
    // cell only (blue, 2 s).
    int kind = r.red_flag ? 1 : r.sc == 1 ? 2 : r.sc == 2 ? 3 : r.sc == 3 ? 4 : yellow ? 5 : blue_flag ? 6 : 0;
    static int last_kind = 0;
    static double full_left = 0.0;
    if (kind != last_kind) {
        if (kind != 0)
            full_left = 4.0;
        last_kind = kind;
    }
    if (full_left > 0.0)
        full_left -= dt;
    bool full = kind != 0 && (std::string_view(flag_screen) == "full" || full_left > 0.0);
    // the return to green: when a caution (safety car, VSC, yellow) that was out for a second or more clears, a
    // green flag flashes over the whole screen for 3 s. The game's yellows flicker (a 66 ms yellow in a 0.4.2 log)
    // and a VSC ends VSC -> yellow -> green within 0.2 s, so a gap of under 0.5 s inside a caution counts as the
    // same caution, and a caution shorter than 1 s gives no green.
    static double caution_for = 0.0, clear_for = 0.0, green_left = 0.0;
    bool caution = kind == 2 || kind == 3 || kind == 5;
    if (caution) {
        caution_for += dt;
        clear_for = 0.0;
        green_left = 0.0;
    } else {
        clear_for += dt;
        if (kind == 1 || kind == 4) { // a red flag or a formation lap: no green after it
            caution_for = 0.0;
        } else if (clear_for >= 0.5) { // clear for 0.5 s: not a flicker
            if (caution_for >= 1.0)
                green_left = 3.0;
            caution_for = 0.0;
        }
        if (green_left > 0.0)
            green_left -= dt;
    }
    // Under a safety car / VSC (1.0.4): the full screen for its 4 s, then the bottom row turns amber with what is out
    // (SAFETY CAR / VSC, then SC IN THIS LAP / VSC ENDING once the game's safety car event says it is returning) and
    // the delta box shows the game's SC delta (sc_delta_shown). Going under the delta (negative: too fast, the game
    // penalises it) shows SLOW DOWN in white on black for 3 s and in the bar until back above it.
    static bool sc_ending = false;
    static uint32_t scar_seen = udp_tap::g.scar_seq;
    if (udp_tap::g.scar_seq != scar_seen) {
        scar_seen = udp_tap::g.scar_seq;
        int st = udp_tap::g.scar_type, ev = udp_tap::g.scar_event;
        swofty::log("[driver-display] safety car event: type %d event %d", st, ev);
        if (st == 1 || st == 2)
            sc_ending = ev == 1;
    }
    bool sc_out = kind == 2 || kind == 3;
    if (!sc_out)
        sc_ending = false;
    if (sc_out && g_sc_slow_rise) {
        char d[32];
        std::snprintf(d, sizeof d, "DELTA %+.2f", double(udp_tap::g.sc_delta));
        warn(d, "SLOW DOWN");
    }
    g_sc_slow_rise = false;
    if (green_left > 0.0 && (kind == 0 || kind == 6)) {
        kind = 8;
        full = true;
    }
    // the pit limiter takes the whole screen for as long as it is on (the driver has nothing to read on the dash in
    // the pit lane); the flag out sets its colour: blue normally, red under a red flag, amber under a safety car /
    // VSC / formation lap, yellow under a yellow. From the game's car status packet (the tap); the launcher's
    // telemetry has no limiter.
    bool limiter = udp_tap::fresh(udp_tap::g.status_tick) && !udp_tap::g.from_launcher && udp_tap::g.pit_limiter == 1;
    int flag_kind = kind;
    if (limiter) {
        kind = 7;
        full = true;
    }
    // a penalty (white) and the chequered flag take the screen over the rest for their few seconds, a red flag
    // excepted
    if (flag_kind != 1 && g_pen_left > 0.0) {
        kind = 9;
        full = true;
    } else if (flag_kind != 1 && g_chq_left > 0.0) {
        kind = 10;
        full = true;
    } else if (flag_kind != 1 && g_warn_left > 0.0) {
        kind = 12;
        full = true;
    } else if (!full && g_msg_left > 0.0) { // a race message, when no other screen is up
        kind = 11;
        full = true;
    }
    static const char *titles[] = {"", "RED FLAG", "SAFETY CAR", "VSC", "FORMATION LAP", "YELLOW FLAG",
                                   "BLUE FLAG", "PIT LIMITER", "GREEN FLAG"};
    static const char *subs[] = {"", "RETURN TO THE PITS", "NO OVERTAKING", "NO OVERTAKING", "", "NO OVERTAKING",
                                 "LET THE CAR BY", "", "TRACK CLEAR"};
    if (full && kind == 9) {
        put("full_title", g_pen_title);
        put("full_sub", g_pen_sub);
    } else if (full && kind == 10) {
        put("full_title", g_chq_title);
        put("full_sub", g_chq_sub);
    } else if (full && kind == 11) {
        put("full_title", g_msg_title);
        put("full_sub", g_msg_sub);
    } else if (full && kind == 12) {
        put("full_title", g_warn_title);
        put("full_sub", g_warn_sub);
    } else if (full) {
        put("full_title", titles[kind]);
        if (kind == 7) // the speed under the limiter: the game's own speed text (rd_lim_speed, 1.1.3)
            put("full_sub", "");
        else
            put("full_sub", subs[kind]);
    }
    // the screen's colour: the flag's, or for the limiter the flag out under it (blue when none, or a blue flag)
    int tone = kind == 7 ? (flag_kind >= 1 && flag_kind <= 5 ? flag_kind : 6) : kind == 11 ? g_msg_tone : kind;
    // the green flag flashes (the blink of the strip along the top edge)
    static double green_blink = 0.0;
    green_blink = kind == 8 ? green_blink + dt : 0.0;
    if (kind == 8 && std::fmod(green_blink, 0.6) >= 0.4)
        full = false;
    put_flag("full_red", full && tone == 1);
    put_flag("full_amber", full && (tone == 2 || tone == 3 || tone == 4));
    put_flag("full_yellow", full && tone == 5);
    put_flag("full_blue", full && tone == 6);
    put_flag("full_green", full && tone == 8);
    put_flag("full_white", full && tone == 9);
    put_flag("lim_light", full && kind == 7 && (tone == 1 || tone == 6));
    put_flag("lim_dark", full && kind == 7 && tone >= 2 && tone <= 5);
    put_flag("full_chq", full && tone == 10);
    put_flag("full_black", full && tone == 12);
    put_flag("full_dark", full && ((tone >= 2 && tone <= 5) || tone == 9)); // black text on amber, yellow, white
    put_flag("full_light", full && (tone == 1 || tone == 6 || tone == 8 || tone == 12)); // white on red, blue, green, black
    put_flag("full_chq_text", full && tone == 10); // the chequered flag's own (lower) title and line
    // after the flag screen, the background takes a tone for as long as a full course caution is out: dark amber
    // under a safety car / VSC, dark red under a red flag
    put_flag("tint_sc", !full && (kind == 2 || kind == 3));
    // 1.2.0: on the race page the safety car layout takes the bar's place (PIT NOW, what is out, the SC delta; the
    // bottom row stays the race layout's); the car and pit pages keep the bar
    bool fcy = !full && (flag_kind == 2 || flag_kind == 3);
    g_fcy = fcy;
    g_sc_scr = fcy && g_page == 0 && !g_quali;
    put_flag("sc_scr", g_sc_scr);
    // the yellow safety car screen (1.2.0, the user's mock-up): SC / VSC DELTA (the game's safetyCarDelta: + is
    // above the delta), the gear and speed, the estimated pit exit position; SAFETY CAR IN THIS LAP / VSC ENDING
    // across the bottom row as the game says it is ending (SLOW DOWN stays the full screen warning)
    put_flag("sc_lbl_s", g_sc_scr && flag_kind == 2);
    put_flag("sc_lbl_v", g_sc_scr && flag_kind == 3);
    {
        static bool ending_said = false;
        if (g_sc_scr && sc_ending && !ending_said)
            row(flag_kind == 2 ? "SAFETY CAR IN THIS LAP" : "VSC ENDING", 'a', 5.0);
        ending_said = fcy && sc_ending;
    }
    if (g_sc_scr) {
        auto &G = udp_tap::g;
        char d[16] = "-";
        if (udp_tap::fresh(G.lap_tick) && !G.from_launcher)
            std::snprintf(d, sizeof d, "%+.2f", double(G.sc_delta));
        put("scd", d);
        put("sc_lap", "LAP " + g_lap_text);
        char pn[8] = "-";
        if (udp_tap::fresh(G.session_tick) && G.pit_rejoin > 0)
            std::snprintf(pn, sizeof pn, "P%02d", int(G.pit_rejoin)); // as the pit page's REJOIN POSITION
        put("pitnow", pn);
        static bool logged = false;
        if (!logged) {
            swofty::log("[driver-display] safety car layout up (%s), pit now P%d", flag_kind == 2 ? "SC" : "VSC", int(G.pit_rejoin));
            logged = true;
        }
    }
    bool bar = fcy && !g_sc_scr;
    put_flag("sc_bar", bar);
    bool flb = !full && !bar && g_fl_left > 0.0;
    put_flag("fl_bar", flb);
    if (flb)
        put("fl_text", g_fl_text);
    bool rowb = !full && !bar && !flb && g_row_left > 0.0;
    put_flag("row_blue", rowb && g_row_colour == 'b');
    put_flag("row_amber", rowb && g_row_colour == 'a');
    put_flag("row_red", rowb && g_row_colour == 'r');
    put_flag("row_light", rowb && (g_row_colour == 'b' || g_row_colour == 'r'));
    put_flag("row_dark", rowb && g_row_colour == 'a');
    if (rowb)
        put("row_text", g_row_text);
    if (bar)
        put("sc_bar_text", g_sc_slow ? "SLOW DOWN" : flag_kind == 2 ? (sc_ending ? "SC IN THIS LAP" : "SAFETY CAR")
                                                               : (sc_ending ? "VSC ENDING" : "VSC"));
    put_flag("tint_red", !full && kind == 1);
    bool on = g_pop_left > 0.0;
    if (on) {
        put("pop_title", g_pop_title);
        put("pop_value", g_pop_value);
    }
    put_flag("pop_blue", on && g_pop_colour == 'b');
    put_flag("pop_amber", on && g_pop_colour == 'a');
    put_flag("pop_red", on && g_pop_colour == 'r');
    put_flag("pop_dark", on && g_pop_colour == 'a');  // black text on amber
    put_flag("pop_light", on && g_pop_colour != 'a'); // white on blue and red
    g_pop_on = on;

    // the flag strip across the top of the screen, blinking: yellow for a yellow (also under SC / VSC), blue for
    // a blue flag, red for a red flag
    static double blink = 0.0;
    blink += dt;
    bool lit = std::fmod(blink, 0.6) < 0.4;
    g_track_flag = r.red_flag ? 'r' : (yellow || r.sc == 1 || r.sc == 2) ? 'y' : (r.sc == 0 && blue_flag) ? 'b' : 'g';
    put_flag("strip_red", r.red_flag && lit);
    put_flag("strip_yellow", !r.red_flag && (yellow || r.sc == 1 || r.sc == 2) && lit);
    put_flag("strip_blue", !r.red_flag && !yellow && r.sc == 0 && blue_flag && lit);

    static int last_flag = -1;
    int flag = r.red_flag ? 9 : r.sc ? r.sc : yellow ? 5 : blue_flag ? 6 : 0;
    if (flag != last_flag) {
        swofty::log("[driver-display] track status: %s%s", r.red_flag ? "red flag" : r.sc == 1 ? "safety car" : r.sc == 2 ? "VSC"
                    : r.sc == 3 ? "formation" : yellow ? "yellow" : blue_flag ? "blue flag" : "green",
                    yellow && fia != 3 ? " (sector, live state)" : "");
        last_flag = flag;
    }
}

// ------------------------------------------------------------------ pages
// The dash has pages: 0 race, 1 car (damage, brakes, power unit), 2 pit (the MFD's pit information). The page button is a "UDP Action" from the
// game's own Controls menu, which any device can be bound to (wheel, controller, keyboard); the game reports
// its presses in its button events, which the tap counts.
constexpr int kPages = 3;

void page_input()
{
    int presses = 0;
    static uint32_t seen[12] = {};
    int a = int(page_action);
    for (int i = 0; i < 12; i++) {
        uint32_t n = udp_tap::g.action_presses[i];
        if (i == a - 1)
            presses += int(n - seen[i]);
        seen[i] = n;
    }
    if (presses > 0) {
        g_page = (g_page + presses) % kPages;
        swofty::log("[driver-display] page %d", g_page + 1);
    }
}

// a value shown only on one page
inline bool on_page(int p, bool v) { return v && g_page == p; }
// page 0's layout (1.0.16): the race's, or practice / qualifying / time trial's (sector times, time left)
inline bool on_race0(bool v) { return v && g_page == 0 && !g_quali; }
inline bool on_quali0(bool v) { return v && g_page == 0 && g_quali; }

// the session's name for the time left label, "" for a race (the session packet's sessionType, F1 25 spec)
const char *session_name(int type)
{
    switch (type) {
    case 1: return "P1";
    case 2: return "P2";
    case 3: return "P3";
    case 4: return "PRACTICE";
    case 5: return "Q1";
    case 6: return "Q2";
    case 7: return "Q3";
    case 8: return "QUALI";
    case 9: case 14: return "ONE-SHOT";
    case 10: return "SQ1";
    case 11: return "SQ2";
    case 12: return "SQ3";
    case 13: return "SPRINT QUALI";
    case 18: return "TIME TRIAL";
    default: return "";
    }
}

// the car page's colours: each part green under 20 % damage, amber under 50 %, red from 50 %, grey until known;
// the tyres by their wear (green, amber from the first limit, red from the second)
void car_page(const Reading &r)
{
    char k[24];
    bool dmg = udp_tap::fresh(udp_tap::g.damage_tick) && !udp_tap::g.from_launcher; // the launcher has no part damage
    auto colour = [&](const char *key, bool known, int band) { // band 0 green, 1 amber, 2 red
        static const char tags[] = {'g', 'a', 'r'};
        for (int i = 0; i < 3; i++) {
            std::snprintf(k, sizeof k, "c%c_%s", tags[i], key);
            put_flag(k, on_page(1, known && band == i));
        }
        // (unknown: no copy shows, the grey base car shows through; 1.0.15)
    };
    auto band = [](int v) { return v >= 50 ? 2 : v >= 20 ? 1 : 0; };
    struct Part {
        const char *key;
        int v;
    } parts[] = {{"fwl", udp_tap::g.dmg_fwl},     {"fwr", udp_tap::g.dmg_fwr},       {"rw", udp_tap::g.dmg_rw},
                 {"floor", udp_tap::g.dmg_floor}, {"diff", udp_tap::g.dmg_diffuser}, {"side", udp_tap::g.dmg_sidepod},
                 {"gbox", udp_tap::g.dmg_gearbox}};
    for (auto &p : parts)
        colour(p.key, dmg, band(p.v));
    // the engine by its temperature (1.0.13; it was the damage packet's engineDamage, whose place in the 2026 pack's
    // packet isn't known: it showed amber with nothing wrong). Green under 130 C, amber from 130, red from 150
    // (1.0.14, the user's limits)
    {
        bool tel = udp_tap::fresh(udp_tap::g.tel_tick) && !udp_tap::g.from_launcher;
        int et = udp_tap::g.engine_temp;
        bool known = tel && et > 0;
        int eb = et >= 150 ? 2 : et >= 130 ? 1 : 0;
        colour("eng", known, eb);
        static int logged_band = -1, hottest = 0;
        if (known && eb != logged_band) {
            swofty::log("[driver-display] engine %d C: %s", et, eb == 2 ? "red" : eb == 1 ? "amber" : "green");
            logged_band = eb;
        }
        if (known && et > hottest + 4) { // the session's hottest so far, in 5 C steps (for setting the limits)
            hottest = et;
            swofty::log("[driver-display] engine hottest so far: %d C", et);
        }
    }
    // brake temperatures beside the car (the game's packets)
    {
        static const char *bt[] = {"bt_fl", "bt_fr", "bt_rl", "bt_rr"};
        bool tel = udp_tap::fresh(udp_tap::g.tel_tick);
        for (int i = 0; i < 4; i++) {
            int c = tel ? int(udp_tap::g.brake_temp[i]) : -1;
            put(bt[i], c > 0 ? std::to_string(c) + "\xC2\xB0" : std::string("-"));
        }
    }
    static const char *bn[] = {"bfl", "bfr", "brl", "brr"};
    for (int i = 0; i < 4; i++)
        colour(bn[i], dmg, band(udp_tap::g.dmg_brake[i]));
    static const char *cn[] = {"tfl", "tfr", "trl", "trr"};
    // 1.2.0: a punctured tyre (the game's puncture state) goes amber as it starts to deflate and red from halfway to
    // the burst (deflation 1 at the puncture, 2 at the burst) or once burst, whatever its wear
    namespace td = swofty::tyre_damage;
    static const td::Wheel wh[] = {td::Wheel::front_left, td::Wheel::front_right, td::Wheel::rear_left, td::Wheel::rear_right};
    auto me = swofty::live::player();
    for (int i = 0; i < 4; i++) {
        float w = r.wear[i];
        int wb = w >= float(int(wear_red)) ? 2 : w >= float(int(wear_amber)) ? 1 : 0;
        bool known = w >= 0.f;
        if (me)
            if (auto st = td::state(*me, wh[i]); st && *st != td::State::none) {
                float dfl = td::deflation(*me, wh[i]).value_or(1.f);
                int pb = *st == td::State::burst || dfl >= 1.5f ? 2 : 1;
                wb = pb > wb ? pb : wb;
                known = true;
            }
        colour(cn[i], known, wb);
    }
}

// The pit page, to the user's mock-up: the pit window's laps (green while the lap is in it) and the position the
// car would rejoin in (the game's session packet, as the MFD's pit panel has them), the tyre on the car and its
// age, the front wing damage (left / right), the tyre wear per corner and the last pit stop's stationary time.
// The MFD's pit plan (next tyres, downforce level, wing repair) is the MFD's own data: no packet, live state or
// shared entry carries it.
void pit_page(const Reading &r)
{
    auto &G = udp_tap::g;
    char t[48];
    bool sess = udp_tap::fresh(G.session_tick);
    {
        static int logged_ideal = -2, logged_latest = -2, logged_rejoin = -2, logged_size = -2;
        int i = G.pit_ideal, l = G.pit_latest, j = G.pit_rejoin, n = G.session_size;
        if (sess && (i != logged_ideal || l != logged_latest || j != logged_rejoin || n != logged_size)) {
            swofty::log("[driver-display] session packet %d bytes: pit window %d-%d, rejoin P%d, type %d, weather %d, "
                        "+%d min %d rain %d%%", n, i, l, j, int(G.session_type), int(G.weather),
                        int(G.forecast_min), int(G.weather10), int(G.rain10));
            logged_ideal = i;
            logged_latest = l;
            logged_rejoin = j;
            logged_size = n;
        }
    }
    // the pit window
    int lap = r.lap >= 0 ? r.lap + 1 : 0;
    int ideal = sess ? int(G.pit_ideal) : 0, latest = sess ? int(G.pit_latest) : 0;
    bool open = ideal > 0 && latest >= ideal && lap >= ideal && lap <= latest;
    if (ideal > 0 && latest >= ideal) std::snprintf(t, sizeof t, "%d-%d", ideal, latest);
    else std::snprintf(t, sizeof t, "-");
    put("pw", t);
    put_flag("pw_g", on_page(2, open));
    put_flag("pw_n", on_page(2, !open));
    // the rejoin position if the car pitted now
    if (sess && G.pit_rejoin > 0) std::snprintf(t, sizeof t, "P%02d", int(G.pit_rejoin));
    else std::snprintf(t, sizeof t, "-");
    put("rejoin", t);
    // 1.2.2: the cars you'd come out between (the order without you: the rejoin place's car ahead and behind)
    {
        std::string between = "-";
        auto me = swofty::live::player();
        int j = sess ? int(G.pit_rejoin) : 0;
        if (me && j > 0) {
            std::vector<uint32_t> others;
            for (uint32_t id : swofty::live::order().ids)
                if (id != me->vehicle)
                    others.push_back(id);
            std::string a = j >= 2 && size_t(j - 2) < others.size() ? car_code(int(others[size_t(j - 2)])) : "-";
            std::string b = size_t(j - 1) < others.size() ? car_code(int(others[size_t(j - 1)])) : "-";
            between = a + " / " + b;
        }
        put("rejoin_cars", between);
    }
    // the tyre on the car, in its compound's colour, and its age
    char ty = 'h'; // unknown or another compound: the hard's copy (white; 1.0.17 dropped the grey one)
    std::string name = "-";
    switch (r.tyre_vis) {
    case 16: name = "SOFT"; ty = 's'; break;
    case 17: name = "MEDIUM"; ty = 'm'; break;
    case 18: name = "HARD"; ty = 'h'; break;
    case 7: name = "INTER"; ty = 'i'; break;
    case 8: name = "WET"; ty = 'w'; break;
    default: if (r.tyre_vis > 0) name = "TYRE " + std::to_string(r.tyre_vis); break;
    }
    put("tyre", name);
    static const char tys[] = {'s', 'm', 'h', 'i', 'w'};
    for (char c : tys) {
        char k[8];
        std::snprintf(k, sizeof k, "ty_%c", c);
        put_flag(k, on_page(2, ty == c));
    }
    put("tyre_age", r.tyre_age >= 0 ? std::to_string(r.tyre_age) + (r.tyre_age == 1 ? " LAP" : " LAPS") : std::string("-"));
    // front wing damage: the game's damage packet (the launcher's telemetry has no part damage)
    bool dmg = udp_tap::fresh(G.damage_tick) && !G.from_launcher;
    int fl = G.dmg_fwl, fr = G.dmg_fwr, worst = fl > fr ? fl : fr;
    char wing = !dmg ? 'g' : worst >= 50 ? 'r' : worst >= 20 ? 'a' : 'g'; // unknown: "-" in green (1.0.17)
    if (!dmg) std::snprintf(t, sizeof t, "-");
    else if (worst == 0) std::snprintf(t, sizeof t, "NONE");
    else std::snprintf(t, sizeof t, "%d%% / %d%%", fl, fr);
    put("wing", t);
    static const char bands[] = {'g', 'a', 'r'};
    for (char c : bands) {
        char k[10];
        std::snprintf(k, sizeof k, "wing_%c", c);
        put_flag(k, on_page(2, wing == c));
    }
    // 1.2.22: the Ferrari pit page's REPAIR WING: yes once the front wing's damage is in the red band
    put("fp_wing", wing == 'r' ? "YES" : (wing == 'g' || wing == 'a') ? "NO" : "-");
    put_flag("fp_wingfix", g_team == 'f' && g_fps == 'p' && wing == 'r');
    put_flag("mc_wing_yes", g_team == 'm' && g_fps == 'p' && wing == 'r'); // 1.2.27: the McLaren's, orange
    put_flag("mc_wing_no", g_team == 'm' && g_fps == 'p' && wing != 'r');
    put_flag("me_wing_yes", g_team == 'e' && g_fps == 'p' && wing == 'r'); // 1.2.31: the Mercedes's cell orange
    // tyre wear per corner, by the wear limits in the settings
    static const char *corners[] = {"fl", "fr", "rl", "rr"};
    for (int i = 0; i < 4; i++) {
        float w = r.wear[i];
        char k[12];
        std::snprintf(k, sizeof k, "w_%s", corners[i]);
        if (w >= 0.f) std::snprintf(t, sizeof t, "%d%%", int(std::lround(w)));
        else std::snprintf(t, sizeof t, "--");
        put(k, t);
        char band = w < 0.f ? 'g' : w >= float(int(wear_red)) ? 'r' : w >= float(int(wear_amber)) ? 'a' : 'g'; // unknown: "--" in green
        for (char c : bands) {
            std::snprintf(k, sizeof k, "w%c_%s", c, corners[i]);
            put_flag(k, on_page(2, band == c));
        }
    }
    // the last pit stop's time stationary (held: the game clears it on the way out)
    static int stop = 0;
    if (udp_tap::fresh(G.lap_tick) && G.stop_ms > 0)
        stop = G.stop_ms;
    if (stop > 0) std::snprintf(t, sizeof t, "%.2f", stop / 1000.0);
    else std::snprintf(t, sizeof t, "-");
    put("pst", t);
    put_flag("pst_on", on_page(2, stop > 0));
}

// ------------------------------------------------------------------ DRS / straight mode, overtake
// The race page's panel: DRS (2025 rules) or straight mode (2026) | the ERS mode, blue while overtake is
// available or active (as the game turns its ERS bar blue) | brake bias.
void aero(double dt)
{
    bool panel = g_page == 0 && !g_pop_on;
    bool t2 = udp_tap::fresh(udp_tap::g.t2_tick);
    bool y2026 = udp_tap::g.format >= 2026 || (t2 && udp_tap::g.regs2026 == 1);

    // the mode badge under ERS SOC (both pages, under the pop-up): 2026 straight mode: grey with the metres to the
    // next zone, green when available, yellow when active; 2025 DRS: grey with the metres to the activation point,
    // green when allowed, yellow when open. Overtake (2026) turns the ERS SOC number blue instead (g_overtake).
    bool mid = !g_pop_on;
    bool ot = t2 && (udp_tap::g.ot_available == 1 || udp_tap::g.ot_active == 1);
    g_overtake = ot;
    bool status = udp_tap::fresh(udp_tap::g.status_tick) && !udp_tap::g.from_launcher;
    bool tel = udp_tap::fresh(udp_tap::g.tel_tick);
    char colour = 0;
    std::string badge;
    if (y2026 && t2) {
        int dist = udp_tap::g.st_distance;
        if (udp_tap::g.st_active == 1) {
            colour = 'p';
            badge = "STRAIGHT ACTIVE";
        } else if (udp_tap::g.st_available == 1) {
            colour = 'g';
            badge = "STRAIGHT READY";
        } else if (dist >= 0 && dist <= 300) {
            colour = 'n';
            badge = "STRAIGHT " + std::to_string(dist) + "m"; // counts down to 0 as the zone comes
        }
    } else if (!y2026) {
        bool open = tel && udp_tap::g.drs_open == 1, allowed = status && udp_tap::g.drs_allowed == 1;
        int dist = udp_tap::g.drs_distance;
        if (open) {
            colour = 'p';
            badge = "DRS OPEN";
        } else if (allowed) {
            colour = 'g';
            badge = "DRS READY";
        } else if (status && dist > 0 && dist <= 300) {
            colour = 'n';
            badge = "DRS " + std::to_string(dist) + "m";
        }
    }
    put("badge_text", badge);
    // an empty grey outline before the zone, green fill when ready, purple (the game's colour) when active
    mid = mid && !g_sc_scr; // 1.2.0: the safety car screen has the lap there
    put_flag("badge_n", mid && colour == 'n');
    put_flag("badge_g", mid && colour == 'g' && !g_fp);
    put_flag("badge_p", mid && colour == 'p' && !g_fp);
    // 1.2.22: the Ferrari pages' mode bar: the badge's text on its colour (purple active, green ready, grey near)
    bool tm = g_fp && !badge.empty() && badge != "-";
    // 1.2.35: the team pages' badge says STRAIGHT MODE whatever the game's text (STRAIGHT ACTIVE, STRAIGHT 120M
    // overflowed the boxes), purple while it's active, grey otherwise (the user's choice)
    put("tm_badge", badge.rfind("STRAIGHT", 0) == 0 ? std::string("STRAIGHT") : badge); // 1.2.41: one word (the user)
    for (char tc : {'f', 'm', 'e', 'b', 'a', 'w', 'u', 'h', 'l'}) {
        bool on = tm && g_team == tc;
        std::string p = tc == 'f'   ? "fp_mode"
                        : tc == 'm' ? "mc_mode"
                        : tc == 'e' ? "me_mode"
                        : tc == 'b' ? "rb_mode"
                        : tc == 'a' ? "am_mode"
                        : tc == 'w' ? "wi_mode"
                        : tc == 'u' ? "au_mode"
                        : tc == 'h' ? "hs_mode"
                                    : "al_mode";
        put_flag(p.c_str(), on);
        put_flag((p + "_p").c_str(), on && colour == 'p');
        put_flag((p + "_g").c_str(), false);
        put_flag((p + "_n").c_str(), on && colour != 'p');
    }
    put_flag("badge_dark", mid && colour == 'g');
    put_flag("badge_light", mid && (colour == 'n' || colour == 'p'));
    (void)panel;

    (void)dt;
}

// ------------------------------------------------------------------ frame
double since = 0.0;
char last_src = 1;
double g_session_s = 0.0; // time in the session (paused time excluded), from the first data on

void on_frame(double dt, void *)
{
    since += dt;
    if (since < 0.05) // 20 Hz
        return;
    g_step = since;
    g_refresh_left = kRefreshPerTick; // 1.2.33
    g_sets_left = kSetsPerTick;       // 1.2.36
    since = 0.0;
    {
        // a team's own pages (1.2.18 the 2026 Ferrari, 1.2.27 the 2026 McLaren): by the car's team badge, while the
        // team pages setting is on
        char team = 0;
        if (auto me = swofty::live::player())
            if (auto key = swofty::live::team_badge(me->vehicle)) {
                static std::string seen_key;
                if (*key != seen_key) { // 1.3.1: the player's team badge, for adding teams
                    seen_key = *key;
                    swofty::log("[driver-display] player's team badge: %s", key->c_str());
                }
                // 1.3.2: the 2025 cars too. The game's badge keys (overrides.osd.timing_tower.team_badges_hud): the
                // 2025 teams plain (ferrari, red_bull, aston_martin), the 2026 ones with _26; matched without it
                std::string k = *key;
                if (k.size() > 3 && k.compare(k.size() - 3, 3, "_26") == 0)
                    k.resize(k.size() - 3);
                team = k == "ferrari"    ? 'f'
                       : k == "mclaren"  ? 'm'
                       : k == "mercedes" ? 'e'
                       : (k == "red_bull" || k == "redbull") ? 'b'
                       : (k == "aston_martin" || k == "astonmartin" || k == "force_india" || k == "forceindia") ? 'a'
                       : k == "williams" ? 'w' // 1.3.4
                       // 1.3.7: the Audi (alfa_romeo_26) and the 2025 Sauber (alfa_romeo)
                       : (k == "alfa_romeo" || k == "alfaromeo" || k == "sauber" || k == "audi") ? 'u'
                       : k == "haas" ? 'h' // 1.3.9
                       : (k == "alpine" || k == "renault") ? 'l' // 1.3.10
                       : 0;
            }
        // 1.3.0: the team pages are always on (the Team pages setting is gone)
        if (team != g_team) {
            swofty::log("[driver-display] team pages: %s", team == 'f'   ? "Ferrari"
                                                           : team == 'm' ? "McLaren"
                                                           : team == 'e' ? "Mercedes"
                                                           : team == 'b' ? "Red Bull"
                                                           : team == 'a' ? "Aston Martin"
                                                           : team == 'w' ? "Williams"
                                                           : team == 'u' ? "Audi"
                                                           : team == 'h' ? "Haas"
                                                           : team == 'l' ? "Alpine"
                                                                         : "off");
            g_team = team;
            g_fer = team == 'f';
        }
        put_flag("all", true); // the switch of everything always shown (its _n / _f copies)
    }
    Reading r = from_live();
    if (!r.src)
        r = from_telemetry();
    // The boot screen: the team badge on black for 3 s, as the dash switching on. Armed by each new session (the
    // live state's session generation, or the data starting), shown when the car first moves: out of the garage in
    // practice and qualifying, the start of the formation lap in a race. 0.4.15 showed it as the data started,
    // which is while the garage menu is up (the wheel off screen), and never in the race (the data source changed
    // between live and telemetry, never through none). A race's standing start without a formation lap gets none:
    // the first movement there is lights out.
    static double boot_left = 0.0;
    static int boot_slice = -1; // the player's team's slice of the logo set
    static bool boot_armed = false;
    static uint32_t boot_gen = 0;
    {
        auto ses = swofty::live::session();
        uint32_t gen = ses ? ses->generation : 0;
        // (1.0.18 armed it on a new session in the game's packets too: it still didn't show on the grid, and a
        // retirement changes the packets' session, so it played over the DNF screen; reverted in 1.0.20)
        if (r.src && (!last_src || gen != boot_gen)) {
            if (!boot_armed)
                swofty::log("[driver-display] boot screen armed (session generation %u)", gen);
            boot_armed = true;
            boot_gen = gen;
        }
        // shown when the player gets the car (the game hands it from the cutscene's control to the human: cutscenes
        // skipped or watched), else when the car first moves
        // A handover or the first movement makes it due; it shows once the car's badge is known (0.4.17's handover
        // came before the badge at load and used the boot up with none). Handovers count for the player's car only
        // (the event fires for every car).
        if (auto me = swofty::live::player())
            g_player_vehicle = int64_t(me->vehicle);
        revlights::frame();
        static bool due = false, due_handed = false, waiting_logged = false;
        bool handed = g_to_human.exchange(false);
        if (boot_armed && !swofty::view::paused() && !due && (handed || r.speed_kmh >= 5)) {
            int type = udp_tap::fresh(udp_tap::g.session_tick) ? int(udp_tap::g.session_type) : -1;
            bool race = type >= 15 && type <= 17; // F1 25 session types: race, race 2, race 3
            if (race && r.sc != 3 && !handed) {
                swofty::log("[driver-display] boot screen skipped: a race start without a formation lap");
                boot_armed = false;
            } else {
                due = true;
                due_handed = handed;
                waiting_logged = false;
            }
        }
        if (boot_armed && due) {
            auto car = swofty::live::player();
            auto key = car ? swofty::live::team_badge(car->vehicle) : std::nullopt;
            if (key && !key->empty()) {
                // the badge key names the team as the logo set does, but for spelling (red_bull_26 / redbull_26)
                auto squash = [](std::string x) {
                    x.erase(std::remove(x.begin(), x.end(), '_'), x.end());
                    for (auto &c : x) c = char(std::tolower(static_cast<unsigned char>(c)));
                    return x;
                };
                // 1.3.2: the badge keys that name a team differently from the logo set (aston_martin_26 is the logo
                // set's forceindia_26), and the 2025 cars' plain keys for the teams whose logo didn't change
                std::string want = squash(*key);
                if (want.size() > 2 && want.compare(want.size() - 2, 2, "26") == 0)
                    want.resize(want.size() - 2);
                static const std::pair<const char *, const char *> kAlias[] = {
                    {"astonmartin", "forceindia"}, {"alpine", "renault"}, {"alphatauri", "tororosso"},
                    {"alfaromeo", "sauber"},       {"gmcadillac", "gmcadillac"}};
                bool y2025 = squash(*key) == want; // no _26
                for (auto &[from, to] : kAlias)
                    if (want == from)
                        want = to;
                if (y2025 && want == "sauber")
                    want.clear(); // the 2025 Kick Sauber: the 2026 set only has Audi's logo
                boot_slice = -1;
                for (int i = 0; i < int(std::size(kTeamLogoKeys)); i++) {
                    std::string k = squash(kTeamLogoKeys[i]);
                    if (k.size() > 2 && k.compare(k.size() - 2, 2, "26") == 0)
                        k.resize(k.size() - 2);
                    if (!want.empty() && k == want)
                        boot_slice = i;
                }
                boot_left = boot_slice >= 0 ? 4.0 : 0.0; // 3 s of logo, then 1 s of black (1.2.0)
                boot_armed = due = false;
                swofty::log("[driver-display] boot screen: team %s, logo slice %d (%s)", key->c_str(), boot_slice,
                            due_handed ? "the car handed to the player" : "the car moved");
            } else if (!waiting_logged) {
                swofty::log("[driver-display] boot screen: waiting for the car's team badge");
                waiting_logged = true;
            }
        }
        if (!boot_armed)
            due = false;
    }
    if (boot_left > 0.0)
        boot_left -= g_step;
    // 1.2.0: the logo shows for the first 3 s and the black stays 1 s longer: the logo's fade-out showed over the
    // page after it (a test's screenshot: the Haas logo faint over the qualifying page right after the boot screen)
    put_flag("boot", boot_left > 0.0);
    for (int i = 0; i < int(std::size(kTeamLogoKeys)); i++) {
        char k[12];
        std::snprintf(k, sizeof k, "boot_s%d", i);
        put_flag(k, boot_left > 1.0 && i == boot_slice);
    }
    if (r.src != last_src) {
        last_src = r.src;
        swofty::log("[driver-display] data source: %s", r.src == 'L' ? "live state" : r.src == 'T' ? "telemetry" : "none");
        if (!r.src) {
            g_laps = Laps{}; // the session ended
            g_session_s = 0.0;
        }
    }
    if (!r.src)
        return;
    if (r.src == 'L')
        telemetry_extras(r);
    tap_extras(r);
    launcher_fill();
    {
        swofty::telemetry::SessionState s{};
        s.version = swofty::telemetry::version;
        if (swofty::telemetry::session(&s) == swofty::telemetry::Result::ok && s.session_time > 0.f)
            r.game_t = s.session_time;
    }
    {
        // which pause reasons the game sets (to learn whether its instant replay has one of its own)
        static uint32_t last_pr = 0;
        uint32_t pr = swofty::view::pause_reasons();
        if (pr != last_pr) {
            swofty::log("[driver-display] pause reasons %#x -> %#x", last_pr, pr);
            last_pr = pr;
        }
    }
    time_laps(r, g_step);
    if (!swofty::view::paused())
        g_session_s += g_step;

    char t[32];
    // the game's own last lap time: from its UDP packets (read by the tap), else the launcher's telemetry
    float game_last = r.tel_last_s;
    if (udp_tap::fresh(udp_tap::g.lap_tick) && udp_tap::g.last_lap_ms > 0)
        game_last = udp_tap::g.last_lap_ms / 1000.f;
    float last = game_last > 0.f ? game_last : g_laps.last_s;
    {
        static int lap_seen = -99;
        static double hold = 0.0;
        if (r.lap != lap_seen) {
            if (lap_seen >= 0 && r.lap == lap_seen + 1)
                hold = 3.0; // the finished lap shows for 3 s after the line
            lap_seen = r.lap;
        }
        if (hold > 0.0)
            hold -= g_step;
        // the game's lap clock; held while its packets pause once they've come (see the delta below)
        static float cur_held = 0.f;
        static bool seen = false;
        bool fresh_laps = udp_tap::fresh(udp_tap::g.lap_tick);
        seen = seen || fresh_laps;
        float cur = fresh_laps ? udp_tap::g.cur_lap_ms / 1000.f : seen ? cur_held : float(g_laps.clock);
        // on the grid before the start (and on the formation lap): the lap hasn't started, whatever the source. The
        // game's own lap clock counts there too (0.4.27 photos: 0:35 in neutral on the grid; 0.4.26 only covered
        // the HUD's own clock before the packets came)
        if (g_laps.on_grid)
            cur = 0.f;
        cur_held = cur;
        put("cur", hold > 0.0 && last > 0.f ? laptime(last) : laptime(cur));
        put("fp_last", last > 0.f ? laptime(last) : std::string("-:--.---")); // 1.2.37: the last lap alone
    }
    {
        int rpm = r.rpm < 0 ? 0 : r.rpm;
        if (rpm >= 1000) std::snprintf(t, sizeof t, "%d,%03d RPM", rpm / 1000, rpm % 1000);
        else std::snprintf(t, sizeof t, "%d RPM", rpm);
        put("rpm", t);
    }
    // the gear: R, N, 1..8
    if (r.gear == -1) put("gear", "R");
    else if (r.gear == 0) put("gear", "N");
    else if (r.gear > 0) put("gear", std::to_string(r.gear));

    // the delta: on the game's own lap clock while its lap packets arrive, else the addon's own timing. Once the
    // game's lap packets have come this session, a gap in them (a replay, a flashback: the game pauses them) holds
    // the lap time and the delta where they were (0.4.0 switched to its own clock there, which kept counting).
    game_laps();
    bool udp_laps = udp_tap::fresh(udp_tap::g.lap_tick);
    if (!udp_tap::fresh(udp_tap::g.session_tick)) // no UDP (1.1.8): the live state's session type, same numbers
        if (auto ls = swofty::live::session(); ls && swofty::live::has(*ls, SWOFTY_LIVE_SHAVE_TYPE))
            udp_tap::g.session_type = int(ls->type);
    {
        int st = udp_tap::g.session_type;
        bool q = (st >= 1 && st <= 14) || st == 18;
        if (q != g_quali)
            swofty::log("[driver-display] page layout: %s (session type %d)", q ? "practice / qualifying" : "race", st);
        g_quali = q;
    }
    static bool laps_seen = false;
    laps_seen = laps_seen || udp_laps;
    bool hold_laps = laps_seen && !udp_laps;
    bool prev_ref = std::string_view(delta_ref) != "best";
    // under a safety car / VSC the box shows the game's SC delta (lap data safetyCarDelta: positive is above it,
    // green; negative too fast, red), back to the lap delta when it clears (1.0.4)
    bool sc_delta = (r.sc == 1 || r.sc == 2) && udp_laps && !udp_tap::g.from_launcher;
    put("delta_label", sc_delta ? (r.sc == 2 ? "VSC DELTA" : "SC DELTA") : prev_ref ? "PREV LAP" : "BEST LAP"); // (the font has no delta sign: 0.4.0 drew a box)
    bool have_delta = udp_laps ? (prev_ref ? g_game.have_prev : g_game.have) : g_laps.have_delta;
    double delta = udp_laps ? (prev_ref ? g_game.delta_prev : g_game.delta) : g_laps.delta;
    if (sc_delta) {
        double d = udp_tap::g.sc_delta;
        if (!g_sc_slow && d < 0.0) { g_sc_slow = true; g_sc_slow_rise = true; }
        else if (g_sc_slow && d > 0.05) g_sc_slow = false; // a little hysteresis around 0
        static double logged = 0.0;
        logged -= g_step;
        if (logged <= 0.0) {
            swofty::log("[driver-display] SC delta %+.2f", d);
            logged = 5.0;
        }
        have_delta = true;
        delta = -d; // the box's colours: negative green, positive red
    } else {
        g_sc_slow = false;
    }
    if (sc_delta) {
        std::snprintf(t, sizeof t, "%c%.2f", delta > 0 ? '-' : '+', std::fabs(delta));
        put("delta", t);
        put_flag("delta_neg", on_race0(delta <= 0));
        put_flag("delta_pos", on_race0(delta > 0));
    } else if (hold_laps) {
        have_delta = g_held_have;
        delta = g_held_delta;
    } else {
        g_held_have = have_delta;
        g_held_delta = delta;
    }
    if (!sc_delta) {
        if (have_delta) {
            std::snprintf(t, sizeof t, "%c%.2f", delta < 0 ? '-' : '+', std::fabs(delta));
            put("delta", t);
        } else {
            put("delta", "--.--");
        }
        // no delta yet: "--.--" in the green copy (1.0.17: the grey copy took 2 of the display's 512 nodes)
        put_flag("delta_neg", on_race0(!have_delta || delta < 0));
        put_flag("delta_pos", on_race0(have_delta && delta >= 0));
    }
    // practice / qualifying: the delta to the best lap, the sector times, the session's time left
    {
        bool qh = udp_laps ? g_game.have : g_laps.have_delta;
        double qd = udp_laps ? g_game.delta : g_laps.delta;
        if (qh) {
            std::snprintf(t, sizeof t, "%c%.3f", qd < 0 ? '-' : '+', std::fabs(qd));
            put("qdelta", t);
        } else {
            put("qdelta", "--.--");
        }
        put_flag("qd_neg", on_quali0(!qh || qd < 0)); // no best lap yet: "--.--" in green
        put_flag("qd_pos", on_quali0(qh && qd >= 0));
        static const char tags[] = {'y', 'g', 'p'};
        char k[16];
        for (int i = 0; i < 3; i++) {
            int ms = udp_tap::g.sector_ms[i], col = udp_tap::g.sector_col[i];
            if (ms > 0) {
                if (ms >= 60000) std::snprintf(t, sizeof t, "%d:%06.3f", ms / 60000, (ms % 60000) / 1000.0);
                else std::snprintf(t, sizeof t, "%.3f", ms / 1000.0);
            } else {
                std::snprintf(t, sizeof t, "-");
                col = 0;
            }
            std::snprintf(k, sizeof k, "s%d", i + 1);
            put(k, t);
            for (int c = 0; c < 3; c++) {
                std::snprintf(k, sizeof k, "s%d_%c", i + 1, tags[c]);
                put_flag(k, on_quali0(col == c));
            }
        }
        static uint32_t seen = 0;
        if (udp_tap::g.sector_seq != seen) {
            seen = udp_tap::g.sector_seq;
            swofty::log("[driver-display] sectors: %d %d %d ms (colours %d %d %d: 0 yellow, 1 green, 2 purple)",
                        int(udp_tap::g.sector_ms[0]), int(udp_tap::g.sector_ms[1]), int(udp_tap::g.sector_ms[2]),
                        int(udp_tap::g.sector_col[0]), int(udp_tap::g.sector_col[1]), int(udp_tap::g.sector_col[2]));
        }
        const char *sn = session_name(udp_tap::g.session_type);
        put("sess_label", udp_tap::g.session_type == 18 ? std::string("TIME TRIAL") : std::string(sn) + " TIME LEFT");
        int left = udp_tap::fresh(udp_tap::g.session_tick) ? int(udp_tap::g.session_left) : -1;
        if (left > 0 && udp_tap::g.session_type != 18) {
            if (left >= 3600) std::snprintf(t, sizeof t, "%d:%02d:%02d", left / 3600, left / 60 % 60, left % 60);
            else std::snprintf(t, sizeof t, "%02d:%02d", left / 60, left % 60);
        } else {
            std::snprintf(t, sizeof t, "--:--");
        }
        put("sess_left", t);
    }
    static int delta_src = -1;
    if (int(udp_laps) != delta_src) {
        delta_src = int(udp_laps);
        swofty::log("[driver-display] delta: %s", udp_laps ? "the game's lap clock (lap packets)" : "the addon's own lap timing");
    }

    // the lap you're on, as the game's HUD counts it (live gives laps completed)
    {
        int total = udp_tap::g.total_laps;
        if (total > 0) std::snprintf(t, sizeof t, "%d/%d", r.lap >= 0 ? r.lap + 1 : 0, total);
        else std::snprintf(t, sizeof t, "%d", r.lap >= 0 ? r.lap + 1 : 0);
        put("lap", t);
        g_lap_text = t;
    }

    int soc = r.soc < 0 ? 0 : r.soc > 100 ? 100 : r.soc;
    std::snprintf(t, sizeof t, "%d%%", soc);
    put("soc", t);

    if (r.pos > 0) std::snprintf(t, sizeof t, "P%02d", r.pos); else std::snprintf(t, sizeof t, "-");
    put("pos", t);
    if (r.gap_ms > 0 && r.pos > 1) std::snprintf(t, sizeof t, "+%.3f", r.gap_ms / 1000.0); else std::snprintf(t, sizeof t, "-");
    put("gap_a", t);
    {
        static double ba_left = 0.0, bb_left = 0.0, phase = 0.0;
        static bool was_a = false, was_b = false;
        bool live_race = !g_quali && r.sc == 0 && r.lap >= 1;
        bool in_a = live_race && r.gap_ms > 0 && r.pos > 1 && r.gap_ms < 1000;
        bool in_b = live_race && r.gap_b_ms > 0 && r.gap_b_ms < 1000;
        if (in_a && !was_a) ba_left = 3.0;
        if (in_b && !was_b) bb_left = 3.0;
        was_a = in_a;
        was_b = in_b;
        phase += g_step;
        bool off = std::fmod(phase, 0.5) >= 0.3;
        if (ba_left > 0.0) ba_left -= g_step;
        if (bb_left > 0.0) bb_left -= g_step;
        put_flag("gap_a_on", on_race0(!(ba_left > 0.0 && off)));
        put_flag("gap_b_on", on_race0(!(bb_left > 0.0 && off)));
    }
    if (r.gap_b_ms > 0) std::snprintf(t, sizeof t, "-%.3f", r.gap_b_ms / 1000.0); else std::snprintf(t, sizeof t, "-");
    put("gap_b", t);
    // the cars ahead and behind by their drivers' codes (HAM, LEC), else the words
    {
        std::string a, b;
        if (!live_neighbour_codes(a, b)) // an older launcher: the telemetry's participants
            neighbour_codes(a, b);
        put("code_a", a.empty() ? std::string("AHEAD") : a);
        put("code_b", b.empty() ? std::string("BEHIND") : b);
        static std::string logged_a, logged_b;
        if (a != logged_a || b != logged_b) {
            swofty::log("[driver-display] around: ahead %s, behind %s", a.empty() ? "-" : a.c_str(), b.empty() ? "-" : b.c_str());
            logged_a = a;
            logged_b = b;
        }
    }

    // ERS deploy mode by name. The 2026 game reports 1 normally and 2 while boosting (0.1.5 log, the user's
    // report); 0 and 3 keep the UDP spec's names. Another value shows as its number and is logged.
    // ERS deploy mode from the game's packets only (as Wheel Dash had it): 0 none, 1 medium, 2 hotlap, 3 overtake,
    // which the 2026 season (packet format 2026) calls BOOST. The launcher's live value numbers the modes its own
    // way (a 2026 career's log had it flip between 0 and 1 all session, 0.3.1): without packets the mode shows "-".
    r.ers_mode = udp_tap::fresh(udp_tap::g.status_tick) ? int(udp_tap::g.ers_mode) : -1;
    bool y2026 = udp_tap::g.format >= 2026;
    static const char *modes[] = {"NONE", "MEDIUM", "HOTLAP", "OVERTAKE"};
    std::string mode = r.ers_mode == 3 && y2026                 ? std::string("BOOST")
                       : r.ers_mode >= 0 && r.ers_mode < 4 ? modes[r.ers_mode]
                       : r.ers_mode >= 0                   ? "MODE " + std::to_string(r.ers_mode)
                                                           : std::string("-");
    // brake bias and fuel mix (UDP only)
    bool status = udp_tap::fresh(udp_tap::g.status_tick);
    put("bbal", status && udp_tap::g.bbal >= 0 ? std::to_string(int(udp_tap::g.bbal)) + "%" : std::string("-"));
    put("diff", udp_tap::fresh(udp_tap::g.setup_tick) && udp_tap::g.diff >= 0 ? std::to_string(int(udp_tap::g.diff)) + "%"
                                                                                : std::string("-"));
    put("mix", status ? mix_name(udp_tap::g.mix) : "-");
    if (udp_tap::fresh(udp_tap::g.damage_tick))
        for (int i = 0; i < 4; i++)
            r.wear[i] = udp_tap::g.wear[i];
    page_input();
    // 1.2.22: the Ferrari's layout (its own pages: every page)
    // 1.2.40: g_fcy, not g_sc_scr: g_sc_scr is set later in the tick from the page, so switching back to page 0
    // under a SC drew the race layout for one tick before the SC one (the user's flicker)
    g_fps = !g_team ? 0 : g_page == 1 ? 'd' : g_page == 2 ? 'p' : g_quali ? 'q' : g_fcy ? 's' : 'r';
    g_fp = g_fps != 0;
    put_flag("p0", g_page == 0);
    put_flag("p0r", g_page == 0 && !g_quali);
    put_flag("p0q", g_page == 0 && g_quali);
    put_flag("p1", g_page == 1);
    put_flag("p2", g_page == 2);
    penalties(r, g_step);
    chequered(r, g_step);
    race_messages(g_step);
    popups(r, mode, g_step);
    car_page(r);
    pit_page(r);
    aero(g_step);
    // the bottom middle cell (both pages): ERS SOC, under the pop-up box when one is up
    put_flag("mid_show", !g_pop_on);
    put_flag("soc_low", !g_pop_on && !g_overtake && r.soc >= 0 && soc < 15);
    put_flag("soc_ok", !g_pop_on && !g_overtake && r.soc >= 0 && soc >= 15);
    put_flag("soc_b", !g_pop_on && g_overtake && r.soc >= 0);
    fp_page(r, soc); // 1.2.18: the Ferrari's own page 0
    meme_frame(r, soc); // 1.2.25: Ferrari Mode (with Immersive Pitstops)
    // tyre temperatures (the game's packets), white: the launcher exposes no optimum windows (1.10)
    static const char *cn[] = {"fl", "fr", "rl", "rr"};
    bool temps = udp_tap::fresh(udp_tap::g.tel_tick);
    char k[24];
    for (int i = 0; i < 4; i++) {
        int c = temps ? int(udp_tap::g.tyre_inner[i]) : -1;
        bool known = c > 0;
        // with the degree sign: the game's own tyre values draw it in OSD_19_bold (Wheel OSD screenshot), the style
        // these use since 0.4.18 (0.4.11's OSD_24_bold drew a box for it)
        std::snprintf(t, sizeof t, known ? "%d" : "--", c); // 1.2.28: no degree sign (the user: more room)
        std::snprintf(k, sizeof k, "t_%s", cn[i]);
        put(k, t);
    }

    // fuel margin from the game's packets, white (1.0); without them the game's own value
    bool fuel_known = udp_tap::fresh(udp_tap::g.status_tick) && udp_tap::g.fuel_laps > -900.f;
    float fl = udp_tap::g.fuel_laps;
    std::snprintf(t, sizeof t, "%+.2f", double(fl));
    put("fuel", fuel_known ? std::string(t) : std::string("-"));
    put_flag("fuel_game", on_race0(!fuel_known));
    put_flag("fuel_ok", on_race0(fuel_known)); // white (1.0)
    {
        int total = udp_tap::g.total_laps;
        int left = total > 0 && r.lap >= 0 ? total - r.lap : 0;
        bool on = fuel_known && left > 0;
        if (on) {
            std::snprintf(t, sizeof t, "%+.2f / LAP", double(fl) / left);
            put("fuel_pl", t);
        }
        put_flag("fuel_pl_on", on_race0(on));
    }

    static double logt = 0.0;
    logt += g_step;
    if (verbose && logt > 10.0) {
        logt = 0.0;
        swofty::log("[driver-display] %c lap %d %.1f m clock %.2f last %.3f (game %.3f) best %.3f delta %s%.3f soc %d pos %d gap %d",
                    r.src, r.lap, double(r.lap_m), g_laps.clock, double(last), double(game_last), double(g_laps.best_s),
                    g_laps.have_delta ? "" : "(none) ", g_laps.delta, r.soc, r.pos, r.gap_ms);
        swofty::telemetry::SessionState s{};
        s.version = swofty::telemetry::version;
        swofty::telemetry::CarState c{};
        bool sess = swofty::telemetry::session(&s) == swofty::telemetry::Result::ok;
        bool car = sess && telemetry_car(c);
        swofty::log("[driver-display] telemetry: session %d (udp format %u, packets %u, player %u, time %.2f left %.0f, laps %u), "
                    "car %d: flags %#x lap %u last %.3f fuel %.2f laps %.2f tyres [%.0f %.0f %.0f %.0f] wear [%.0f %.0f %.0f %.0f]",
                    int(sess), s.udp_format, s.packets_seen, s.player_index, double(s.session_time),
                    double(s.session_time_left), s.total_laps, int(car), c.flags, c.lap,
                    double(c.last_lap_time), double(c.fuel), double(c.fuel_laps), double(c.tyre_temp[0]),
                    double(c.tyre_temp[1]), double(c.tyre_temp[2]), double(c.tyre_temp[3]), double(c.tyre_wear[0]),
                    double(c.tyre_wear[1]), double(c.tyre_wear[2]), double(c.tyre_wear[3]));
        swofty::log("[driver-display] udp tap calls: sendto [%u %u %u %u] WSASendTo [%u %u]",
                    unsigned(udp_tap::g_calls[0][0]), unsigned(udp_tap::g_calls[0][1]), unsigned(udp_tap::g_calls[0][2]),
                    unsigned(udp_tap::g_calls[0][3]), unsigned(udp_tap::g_calls[1][0]), unsigned(udp_tap::g_calls[1][1]));
        swofty::log("[driver-display] udp tap: %u packet(s), format %d, player %d; status %d (mix %d bbal %d fuel %.2f ers %d) "
                    "setup %d (diff %d) damage %d (wear %.1f %.1f %.1f %.1f) lap %d (last %u ms) engine %d C",
                    unsigned(udp_tap::g.packets), int(udp_tap::g.format), int(udp_tap::g.player),
                    int(udp_tap::fresh(udp_tap::g.status_tick)), int(udp_tap::g.mix), int(udp_tap::g.bbal),
                    double(udp_tap::g.fuel_laps), int(udp_tap::g.ers_mode), int(udp_tap::fresh(udp_tap::g.setup_tick)),
                    int(udp_tap::g.diff), int(udp_tap::fresh(udp_tap::g.damage_tick)), double(udp_tap::g.wear[0]),
                    double(udp_tap::g.wear[1]), double(udp_tap::g.wear[2]), double(udp_tap::g.wear[3]),
                    int(udp_tap::fresh(udp_tap::g.lap_tick)), unsigned(udp_tap::g.last_lap_ms),
                    int(udp_tap::g.engine_temp));
    }
}

// The tap goes in at load; if the game has no sendto slot yet (it may load its network code later), it is
// tried again every 5 s for 2 minutes.
bool g_tapped = false;
int g_tap_tries = 0;
double g_tap_wait = 0.0;
void try_tap()
{
    g_tap_tries++;
    auto res = udp_tap::install();
    g_tapped = res.sendto_slots || res.wsa_slots;
    if (g_tapped || g_tap_tries == 1 || g_tap_tries == 24)
        swofty::log("[driver-display] udp tap (try %d): %s; sendto slot(s) %d, WSASendTo slot(s) %d, %d MB scanned%s%s", g_tap_tries,
                    res.how, res.sendto_slots, res.wsa_slots, res.scanned_mb, res.where[0] ? ": " : "", res.where);
}

void tap_frame(double dt, void *)
{
    if (g_tapped || g_tap_tries >= 24)
        return;
    g_tap_wait += dt;
    if (g_tap_wait < 5.0)
        return;
    g_tap_wait = 0.0;
    try_tap();
}


} // namespace

SWOFTY_ON_LOAD
{
    swofty::log("[driver-display] loaded 1.4.0: hud %s, live %s, telemetry %s", swofty::hud::available() ? "available" : "not available",
                swofty::live::available() ? "available" : "not available (yet)",
                swofty::telemetry::available() ? "available" : "not available (yet)");
    swofty::events::on_frame(on_frame);
    // who drives the player's car: logged (to learn when the game hands it over), and a switch to the human marks
    // the end of the race intro for the boot screen. A notify-only handler: it never changes the switch.
    auto r = swofty::events::on<swofty::sessions::events::PlayerControl>().after(
        [](const swofty::sessions::events::PlayerControl &e, const swofty::events::Context &) {
            if (int64_t(e.car.index()) == g_player_vehicle.load()) // the player's car only (it fires for all)
                swofty::log("[driver-display] player control: %s -> %s (reason %d)",
                            e.from == swofty::sessions::Control::human ? "human" : "ai",
                            e.to == swofty::sessions::Control::human ? "human" : "ai", int(e.reason));
            if (e.to == swofty::sessions::Control::human && int64_t(e.car.index()) == g_player_vehicle.load())
                g_to_human = true;
        });
    swofty::log("[driver-display] player control events: %s", r == swofty::events::Result::ok ? "on" : "not available");
    // 1.2.16: Immersive Pitstops' stuck nuts, loose wheels and retirements, when it's installed (its service)
    auto ipr = swofty::services::when<ip_link::State>([](const ip_link::State &t) {
        if (t.size >= offsetof(ip_link::State, ferrari_mode) && t.car) {
            g_ip_car = t.car;
            swofty::log("[driver-display] immersive pitstops: linked (stuck nuts, loose wheels, retirements)");
        }
        if (t.size >= sizeof(ip_link::State) && t.ferrari_mode) { // 1.2.25: Immersive Pitstops 1.0.23+
            g_ip_fmode = t.ferrari_mode;
            swofty::log("[driver-display] immersive pitstops: Ferrari Mode linked");
        }
    });
    if (ipr != swofty::services::Result::ok && ipr != swofty::services::Result::late)
        swofty::log("[driver-display] immersive pitstops: no services on this core (nothing linked)");
    // race control (launcher 1.10): retirements while the game's UDP output is off
    namespace rc = swofty::race_control;
    auto rr = swofty::events::on<rc::events::CarStatus>().after([](const rc::events::CarStatus &e, const swofty::events::Context &) {
        if (e.previous == rc::Status::running &&
            (e.status == rc::Status::retired || e.status == rc::Status::did_not_finish)) {
            std::lock_guard<std::mutex> l(g_rc_mu);
            if (g_rc_retired.size() < 32)
                g_rc_retired.push_back(int(e.car));
        }
    });
    // the engineer's radio lines (launcher 1.10.20): their names, for the tags
    if (swofty::speech::live()) {
        auto sr = swofty::events::on<swofty::speech::events::Line>().after(
            [](const swofty::speech::events::Line &l, const swofty::events::Context &) {
                if (!swofty::speech::radio(l) || !swofty::speech::engineer(l))
                    return;
                std::string_view stem = swofty::speech::stem(l);
                if (stem.empty())
                    return;
                std::lock_guard<std::mutex> lk(g_rc_mu);
                if (g_radio_stems.size() < 16)
                    g_radio_stems.emplace_back(stem);
            });
        swofty::log("[driver-display] radio tags: %s", sr == swofty::events::Result::ok ? "on" : "not available");
    } else {
        swofty::log("[driver-display] radio tags: no voice line events on this core or game build");
    }
    swofty::log("[driver-display] race control events: %s, tyre damage: %s", rr == swofty::events::Result::ok ? "on" : "not available",
                swofty::hooks::live<swofty::tyre_damage::events::Damage>() ? "live" : "not live");
    {
        uintptr_t at = swofty::api && swofty::api->resolve ? swofty::api->resolve("Car_SteeringWheelLeds_Update") : 0;
        swofty::log("[driver-display] rev lights: %s", !bool(real_rev_lights) ? "off in the settings"
                    : at ? "the LED update is wrapped; a created career driver gets the most common real-driver LED settings"
                         : "the LED update isn't found on this build: the game's own lights");
    }
    try_tap();
    swofty::events::on_frame(tap_frame);
}

SWOFTY_ADDON()

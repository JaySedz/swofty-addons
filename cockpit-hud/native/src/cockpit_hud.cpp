// Cockpit HUD: a steering wheel LCD for F1 25, fed from the launcher's car state (no UDP socket of its own).
//
// The layout and template in ui/ (built by tools/build_ui.py) show the game's own LCD values where it has
// them (the gear) and these HUD leaves, written here (tyre temperatures t_fl..t_rr from the telemetry too):
//   ui_menu_data.hud_rdash_last      last lap time "1:31.402"
//   ui_menu_data.hud_rdash_rpm       engine rpm
//   ui_menu_data.hud_rdash_delta     live delta to the best lap "-0.21"; with delta_neg / delta_pos /
//                                    delta_none choosing its green / red / grey copy
//   ui_menu_data.hud_rdash_speed     km/h
//   ui_menu_data.hud_rdash_lap       laps completed
//   ui_menu_data.hud_rdash_soc       ERS store %; soc_ok / soc_low choose its green / red copy
//   ui_menu_data.hud_rdash_soc01..20 the SOC bar's segments (5 % each)
//   ui_menu_data.hud_rdash_pos       race position "P3"
//   ui_menu_data.hud_rdash_gap       gap to the car ahead "+1.234"
//
// Data: the launcher's live state (game memory); the launcher's telemetry (decoded from the game's own UDP
// packets inside the game, so the game's UDP output must be on, but nothing here listens on the port) only
// when the live state is not there. Lap times are timed here from the lap counter and the frame clock (paused
// time excluded); with telemetry the game's own last lap time replaces the timed one.
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <swofty/events.hpp>
#include <swofty/hud.hpp>
#include <swofty/live.hpp>
#include <swofty/mixin.hpp>
#include <swofty/systems/sessions.hpp>
#include <swofty/telemetry.hpp>
#include <swofty/view.hpp>

#include "packet_values.hpp"
#include "team_logos.hpp"
#include "udp_tap.hpp"

SWOFTY_SETTING(bool, verbose, false);
SWOFTY_SETTING(int, tyre_cold_below, 80);
SWOFTY_SETTING(int, tyre_hot_above, 105);
SWOFTY_SETTING(int, wear_amber, 40);
SWOFTY_SETTING(int, wear_red, 70);
SWOFTY_SETTING(std::string_view, delta_ref, "previous"); // the delta against the previous lap or the best one
SWOFTY_SETTING(std::string_view, flag_screen, "brief"); // flags over the whole screen: "brief" (4 s) or "full"
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
};

double g_step = 0.0;
// the player's car was handed to the human (session.player.control): the end of the race intro / cutscenes
std::atomic<bool> g_to_human{false};
std::atomic<int64_t> g_player_vehicle{-1}; // the player's vehicle id (live state), for the control event
int g_page = 0; // the dash page shown: 0 race, 1 car
bool g_pop_on = false; // a pop-up box covers the info panel
int g_last_speed = 0;   // km/h, for the packet 16 log
bool g_overtake = false;
bool g_held_have = false;
double g_held_delta = 0.0; // overtake available or active (2026): ERS SOC shows blue

Leaf &leaf(const char *name)
{
    static std::vector<Leaf *> all;
    std::string path = std::string("ui_menu_data.hud_rdash_") + name;
    for (Leaf *l : all)
        if (l->path == path)
            return *l;
    all.push_back(new Leaf{path});
    return *all.back();
}

void put(const char *name, const std::string &v)
{
    Leaf &l = leaf(name);
    l.age += g_step;
    if (l.last_b == 2 && v == l.last && l.age < 2.0)
        return;
    auto r = swofty::hud::set(l.path, v);
    if (r != swofty::hud::Result::ok) {
        if (l.logged++ < 1)
            swofty::log("[cockpit-hud] hud %s -> %s", l.path.c_str(), swofty::hud::name(r));
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

void put_flag(const char *name, bool v) // a separate name: a char buffer would otherwise pick a bool overload over std::string (0.1.x wrote every text as true)
{
    Leaf &l = leaf(name);
    l.age += g_step;
    if (l.last_b == int(v) && l.age < 2.0)
        return;
    if (swofty::hud::set(l.path, v) != swofty::hud::Result::ok)
        return;
    l.last_b = int(v);
    l.age = 0.0;
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
        swofty::log("[cockpit-hud] telemetry tyre_temp raw [%.0f %.0f %.0f %.0f]", double(c.tyre_temp[0]),
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
    swofty::telemetry::Participant me{};
    me.version = swofty::telemetry::version;
    if (swofty::telemetry::participant(s.player_index, &me) != swofty::telemetry::Result::ok || me.position <= 0)
        return;
    uint32_t n = s.participant_count < 24 ? s.participant_count : 24;
    for (uint32_t i = 0; i < n; i++) {
        if (i == s.player_index)
            continue;
        swofty::telemetry::Participant p{};
        p.version = swofty::telemetry::version;
        if (swofty::telemetry::participant(i, &p) != swofty::telemetry::Result::ok || !p.name[0])
            continue;
        if (p.position == me.position - 1) ahead = driver_code(p.name);
        else if (p.position == me.position + 1) behind = driver_code(p.name);
    }
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
        swofty::log("[cockpit-hud] the tap reads no packets: tyres, brakes, fuel and flags from the launcher's telemetry");
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
        swofty::log("[cockpit-hud] lap clock: %s", use_gt ? "the game's session time" : "frame time");
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
            swofty::log("[cockpit-hud] lap clock held: the car is frozen at %.1f m (a replay?), pause reasons %#x",
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
                swofty::log("[cockpit-hud] flashback: %.0f m -> %.0f m, lap clock back to %.2f", double(prev_m),
                            double(r.lap_m), L.clock);
            }
        }
    }
    prev_m = r.lap_m;

    // A race's opening lap counts from the moment the car leaves its grid slot (0.1.5 skipped it, so the first
    // lap time showed on lap 3). Only when the data arrived with the car standing still outside the pits on
    // lap 0: a garage start (practice, qualifying) is an out lap, not timed.
    if (L.lap == 0 && r.lap == 0 && !L.whole) {
        if (r.speed_kmh <= 0 && !r.in_pit && r.lap_m >= 0.f)
            L.on_grid = true;
        else if (L.on_grid && r.speed_kmh > 0 && r.sc != 3) { // not off on the formation lap: the real start
            L.on_grid = false;
            L.whole = true;
            L.clock = 0.0;
            std::fill(L.cur.begin(), L.cur.end(), -1.f);
            swofty::log("[cockpit-hud] off the grid at %.1f m: the opening lap is timed from here", double(r.lap_m));
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
                swofty::log("[cockpit-hud] reference lap %.3f%s", double(lap_s), G.held_invalid ? " (invalid)" : "");
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
        swofty::log("[cockpit-hud] penalty event: type %d infringement %d time %d", type, infr, secs);
        switch (type) {
        case 0: full("DRIVE THROUGH", why); break;
        case 1: full(secs > 0 ? "STOP-GO " + std::to_string(secs) + " SEC" : std::string("STOP-GO PENALTY"), why); break;
        case 2: full("GRID PENALTY", secs > 0 ? std::to_string(secs) + " PLACES" : why); break;
        case 3: full("SERVE PENALTY", why); break;
        case 4: full("+" + std::to_string(secs) + " SEC PENALTY", why); break;
        case 5: pop("WARNING", why.empty() ? std::string("-") : why, 'a', 3.0); break;
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
        pop("WARNING", "TRACK LIMITS " + std::to_string(r.tl), 'a', 3.0);
    else if (quiet && warns >= 0 && r.warns > warns)
        pop("WARNING", "WARNINGS " + std::to_string(r.warns), 'a', 3.0);
    pen = r.pen_s;
    dts = r.dt;
    sgs = r.sg;
    warns = r.warns;
    tl = r.tl;
    if (g_pen_left > 0.0)
        g_pen_left -= dt;
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
        swofty::log("[cockpit-hud] chequered flag out (%s)", event ? "event" : "live state");
    }
    if (!first && last_result == 2 && r.result == 3) {
        char t[32];
        if (r.pos > 0) std::snprintf(t, sizeof t, "FINISHED P%d", r.pos);
        else std::snprintf(t, sizeof t, "FINISHED");
        g_chq_title = "CHEQUERED FLAG";
        g_chq_sub = t;
        g_chq_left = 8.0;
        swofty::log("[cockpit-hud] finished: %s", t);
    }
    if (r.result >= 0)
        last_result = r.result;
    was = r.chequered;
    first = false;
    if (g_chq_left > 0.0)
        g_chq_left -= dt;
}

void popups(const Reading &r, const std::string &mode, double dt)
{
    static int last_mode = -2;
    if (r.ers_mode != last_mode) {
        if (last_mode >= 0 && r.ers_mode >= 0)
            pop("ERS MODE", mode);
        swofty::log("[cockpit-hud] ERS mode %d -> %d", last_mode, r.ers_mode);
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
    } else if (full) {
        put("full_title", titles[kind]);
        if (kind == 7) // the speed under the limiter
            put("full_sub", std::to_string(r.speed_kmh < 0 ? 0 : r.speed_kmh) + " KM/H");
        else
            put("full_sub", subs[kind]);
    }
    // the screen's colour: the flag's, or for the limiter the flag out under it (blue when none, or a blue flag)
    int tone = kind == 7 ? (flag_kind >= 1 && flag_kind <= 5 ? flag_kind : 6) : kind;
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
    put_flag("full_chq", full && tone == 10);
    put_flag("full_dark", full && ((tone >= 2 && tone <= 5) || tone == 9)); // black text on amber, yellow, white
    put_flag("full_light", full && (tone == 1 || tone == 6 || tone == 8 || tone == 10)); // white on red, blue, green, chequered
    // after the flag screen, the background takes a tone for as long as a full course caution is out: dark amber
    // under a safety car / VSC, dark red under a red flag
    put_flag("tint_sc", !full && (kind == 2 || kind == 3));
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
    put_flag("pop_on", on);
    g_pop_on = on;
    put_flag("ers_show", !on && g_page == 0);
    put_flag("p1_panel", !on && g_page == 1);

    // the flag strip across the top of the screen, blinking: yellow for a yellow (also under SC / VSC), blue for
    // a blue flag, red for a red flag
    static double blink = 0.0;
    blink += dt;
    bool lit = std::fmod(blink, 0.6) < 0.4;
    put_flag("strip_red", r.red_flag && lit);
    put_flag("strip_yellow", !r.red_flag && (yellow || r.sc == 1 || r.sc == 2) && lit);
    put_flag("strip_blue", !r.red_flag && !yellow && r.sc == 0 && blue_flag && lit);

    static int last_flag = -1;
    int flag = r.red_flag ? 9 : r.sc ? r.sc : yellow ? 5 : blue_flag ? 6 : 0;
    if (flag != last_flag) {
        swofty::log("[cockpit-hud] track status: %s%s", r.red_flag ? "red flag" : r.sc == 1 ? "safety car" : r.sc == 2 ? "VSC"
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
        swofty::log("[cockpit-hud] page %d", g_page + 1);
    }
}

// a value shown only on one page
inline bool on_page(int p, bool v) { return v && g_page == p; }

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
        std::snprintf(k, sizeof k, "cn_%s", key);
        put_flag(k, on_page(1, !known));
    };
    auto band = [](int v) { return v >= 50 ? 2 : v >= 20 ? 1 : 0; };
    struct Part {
        const char *key;
        int v;
    } parts[] = {{"fwl", udp_tap::g.dmg_fwl},     {"fwr", udp_tap::g.dmg_fwr},       {"rw", udp_tap::g.dmg_rw},
                 {"floor", udp_tap::g.dmg_floor}, {"diff", udp_tap::g.dmg_diffuser}, {"side", udp_tap::g.dmg_sidepod},
                 {"gbox", udp_tap::g.dmg_gearbox}, {"eng", udp_tap::g.dmg_engine}};
    for (auto &p : parts)
        colour(p.key, dmg, band(p.v));
    static const char *bn[] = {"bfl", "bfr", "brl", "brr"};
    for (int i = 0; i < 4; i++)
        colour(bn[i], dmg, band(udp_tap::g.dmg_brake[i]));
    static const char *cn[] = {"tfl", "tfr", "trl", "trr"};
    for (int i = 0; i < 4; i++) {
        float w = r.wear[i];
        int wb = w >= float(int(wear_red)) ? 2 : w >= float(int(wear_amber)) ? 1 : 0;
        colour(cn[i], w >= 0.f, wb);
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
            swofty::log("[cockpit-hud] session packet %d bytes: pit window %d-%d, rejoin P%d, type %d, weather %d, "
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
    // the tyre on the car, in its compound's colour, and its age
    char ty = 'n';
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
    static const char tys[] = {'s', 'm', 'h', 'i', 'w', 'n'};
    for (char c : tys) {
        char k[8];
        std::snprintf(k, sizeof k, "ty_%c", c);
        put_flag(k, on_page(2, ty == c));
    }
    put("tyre_age", r.tyre_age >= 0 ? std::to_string(r.tyre_age) + (r.tyre_age == 1 ? " LAP" : " LAPS") : std::string("-"));
    // front wing damage: the game's damage packet (the launcher's telemetry has no part damage)
    bool dmg = udp_tap::fresh(G.damage_tick) && !G.from_launcher;
    int fl = G.dmg_fwl, fr = G.dmg_fwr, worst = fl > fr ? fl : fr;
    char wing = !dmg ? 'n' : worst >= 50 ? 'r' : worst >= 20 ? 'a' : 'g';
    if (!dmg) std::snprintf(t, sizeof t, "-");
    else if (worst == 0) std::snprintf(t, sizeof t, "NONE");
    else std::snprintf(t, sizeof t, "%d%% / %d%%", fl, fr);
    put("wing", t);
    static const char bands[] = {'g', 'a', 'r', 'n'};
    for (char c : bands) {
        char k[10];
        std::snprintf(k, sizeof k, "wing_%c", c);
        put_flag(k, on_page(2, wing == c));
    }
    // tyre wear per corner, by the wear limits in the settings
    static const char *corners[] = {"fl", "fr", "rl", "rr"};
    for (int i = 0; i < 4; i++) {
        float w = r.wear[i];
        char k[12];
        std::snprintf(k, sizeof k, "w_%s", corners[i]);
        if (w >= 0.f) std::snprintf(t, sizeof t, "%d%%", int(std::lround(w)));
        else std::snprintf(t, sizeof t, "--");
        put(k, t);
        char band = w < 0.f ? 'n' : w >= float(int(wear_red)) ? 'r' : w >= float(int(wear_amber)) ? 'a' : 'g';
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
    put_flag("badge_n", mid && colour == 'n');
    put_flag("badge_g", mid && colour == 'g');
    put_flag("badge_p", mid && colour == 'p');
    put_flag("badge_dark", mid && colour == 'g');
    put_flag("badge_light", mid && (colour == 'n' || colour == 'p'));
    (void)panel;

    // packet 16's bytes for the player's car, logged on every change (at most 400 lines a session), to map
    // straight mode: drive through a straight-mode zone and use overtake with the Debug log on
    static uint64_t last_lo = ~0ull, last_hi = ~0ull;
    static int lines = 0;
    if (t2) {
        uint64_t lo = udp_tap::g.t2_raw[0], hi = udp_tap::g.t2_raw[1];
        if ((lo != last_lo || hi != last_hi) && lines < 400) {
            lines++;
            last_lo = lo;
            last_hi = hi;
            char hex[64];
            for (int i = 0; i < 16; i++)
                std::snprintf(hex + i * 3, 4, "%02x ", unsigned((i < 8 ? lo >> (8 * i) : hi >> (8 * (i - 8))) & 0xff));
            swofty::log("[cockpit-hud] packet 16 (%d bytes a car): %s| speed %d drs-allowed %d", int(udp_tap::g.t2_size), hex,
                        g_last_speed, int(udp_tap::g.drs_allowed));
        }
    }
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
    since = 0.0;
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
        if (r.src && (!last_src || gen != boot_gen)) {
            if (!boot_armed)
                swofty::log("[cockpit-hud] boot screen armed (session generation %u)", gen);
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
        static bool due = false, due_handed = false, waiting_logged = false;
        bool handed = g_to_human.exchange(false);
        if (boot_armed && !swofty::view::paused() && !due && (handed || r.speed_kmh >= 5)) {
            int type = udp_tap::fresh(udp_tap::g.session_tick) ? int(udp_tap::g.session_type) : -1;
            bool race = type >= 15 && type <= 17; // F1 25 session types: race, race 2, race 3
            if (race && r.sc != 3 && !handed) {
                swofty::log("[cockpit-hud] boot screen skipped: a race start without a formation lap");
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
                boot_slice = -1;
                for (int i = 0; i < int(std::size(kTeamLogoKeys)); i++)
                    if (squash(kTeamLogoKeys[i]) == squash(*key))
                        boot_slice = i;
                boot_left = boot_slice >= 0 ? 3.0 : 0.0;
                boot_armed = due = false;
                swofty::log("[cockpit-hud] boot screen: team %s, logo slice %d (%s)", key->c_str(), boot_slice,
                            due_handed ? "the car handed to the player" : "the car moved");
            } else if (!waiting_logged) {
                swofty::log("[cockpit-hud] boot screen: waiting for the car's team badge");
                waiting_logged = true;
            }
        }
        if (!boot_armed)
            due = false;
    }
    if (boot_left > 0.0)
        boot_left -= g_step;
    put_flag("boot", boot_left > 0.0);
    for (int i = 0; i < int(std::size(kTeamLogoKeys)); i++) {
        char k[12];
        std::snprintf(k, sizeof k, "boot_s%d", i);
        put_flag(k, boot_left > 0.0 && i == boot_slice);
    }
    if (r.src != last_src) {
        last_src = r.src;
        swofty::log("[cockpit-hud] data source: %s", r.src == 'L' ? "live state" : r.src == 'T' ? "telemetry" : "none");
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
            swofty::log("[cockpit-hud] pause reasons %#x -> %#x", last_pr, pr);
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
    put("last", laptime(last));
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
        cur_held = cur;
        put("cur", hold > 0.0 && last > 0.f ? laptime(last) : laptime(cur));
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
    static bool laps_seen = false;
    laps_seen = laps_seen || udp_laps;
    bool hold_laps = laps_seen && !udp_laps;
    bool prev_ref = std::string_view(delta_ref) != "best";
    put("delta_label", prev_ref ? "PREV LAP" : "BEST LAP"); // (the font has no delta sign: 0.4.0 drew a box)
    bool have_delta = udp_laps ? (prev_ref ? g_game.have_prev : g_game.have) : g_laps.have_delta;
    double delta = udp_laps ? (prev_ref ? g_game.delta_prev : g_game.delta) : g_laps.delta;
    if (hold_laps) {
        have_delta = g_held_have;
        delta = g_held_delta;
    } else {
        g_held_have = have_delta;
        g_held_delta = delta;
    }
    if (have_delta) {
        std::snprintf(t, sizeof t, "%c%.2f", delta < 0 ? '-' : '+', std::fabs(delta));
        put("delta", t);
    }
    put_flag("delta_neg", on_page(0, have_delta && delta < 0));
    put_flag("delta_pos", on_page(0, have_delta && delta >= 0));
    put_flag("delta_none", on_page(0, !have_delta));
    static int delta_src = -1;
    if (int(udp_laps) != delta_src) {
        delta_src = int(udp_laps);
        swofty::log("[cockpit-hud] delta: %s", udp_laps ? "the game's lap clock (lap packets)" : "the addon's own lap timing");
    }

    std::snprintf(t, sizeof t, "%d", r.speed_kmh < 0 ? 0 : r.speed_kmh);
    put("speed", t);
    g_last_speed = r.speed_kmh;
    // the lap you're on, as the game's HUD counts it (live gives laps completed)
    {
        int total = udp_tap::g.total_laps;
        if (total > 0) std::snprintf(t, sizeof t, "%d/%d", r.lap >= 0 ? r.lap + 1 : 0, total);
        else std::snprintf(t, sizeof t, "%d", r.lap >= 0 ? r.lap + 1 : 0);
        put("lap", t);
    }

    int soc = r.soc < 0 ? 0 : r.soc > 100 ? 100 : r.soc;
    std::snprintf(t, sizeof t, "%d%%", soc);
    put("soc", t);

    if (r.pos > 0) std::snprintf(t, sizeof t, "P%02d", r.pos); else std::snprintf(t, sizeof t, "-");
    put("pos", t);
    if (r.gap_ms > 0 && r.pos > 1) std::snprintf(t, sizeof t, "+%.3f", r.gap_ms / 1000.0); else std::snprintf(t, sizeof t, "-");
    put("gap_a", t);
    if (r.gap_b_ms > 0) std::snprintf(t, sizeof t, "-%.3f", r.gap_b_ms / 1000.0); else std::snprintf(t, sizeof t, "-");
    put("gap_b", t);
    // the cars ahead and behind by their drivers' codes (HAM, LEC), else the words
    {
        std::string a, b;
        neighbour_codes(a, b);
        put("code_a", a.empty() ? std::string("AHEAD") : a);
        put("code_b", b.empty() ? std::string("BEHIND") : b);
        static std::string logged_a, logged_b;
        if (a != logged_a || b != logged_b) {
            swofty::log("[cockpit-hud] around: ahead %s, behind %s", a.empty() ? "-" : a.c_str(), b.empty() ? "-" : b.c_str());
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
    put("ers_mode", mode);
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
    put_flag("p0", g_page == 0);
    put_flag("p1", g_page == 1);
    put_flag("p2", g_page == 2);
    penalties(r, g_step);
    chequered(r, g_step);
    popups(r, mode, g_step);
    car_page(r);
    pit_page(r);
    aero(g_step);
    // the bottom middle cell (both pages): ERS SOC, under the pop-up box when one is up
    put_flag("mid_show", !g_pop_on);
    put_flag("soc_low", !g_pop_on && !g_overtake && r.soc >= 0 && soc < 15);
    put_flag("soc_ok", !g_pop_on && !g_overtake && r.soc >= 0 && soc >= 15);
    put_flag("soc_b", !g_pop_on && g_overtake && r.soc >= 0);
    // tyres, colour coded: temperature blue below the cold limit, green in the window, red above the hot limit
    // (the numbers from the game's packets; without them the game's own formatted temperature shows, uncoloured);
    // wear green, amber from the first limit, red from the second
    static const char *cn[] = {"fl", "fr", "rl", "rr"};
    bool temps = udp_tap::fresh(udp_tap::g.tel_tick);
    char k[24];
    for (int i = 0; i < 4; i++) {
        int c = temps ? int(udp_tap::g.tyre_inner[i]) : -1;
        bool known = c > 0;
        // with the degree sign: the game's own tyre values draw it in OSD_19_bold (Wheel OSD screenshot), the style
        // these use since 0.4.18 (0.4.11's OSD_24_bold drew a box for it)
        std::snprintf(t, sizeof t, known ? "%d\xC2\xB0" : "--", c);
        std::snprintf(k, sizeof k, "t_%s", cn[i]);
        put(k, t);
        std::snprintf(k, sizeof k, "tg_%s", cn[i]);
        put_flag(k, on_page(0, !known));
        std::snprintf(k, sizeof k, "tc_%s", cn[i]);
        put_flag(k, on_page(0, known && c < int(tyre_cold_below)));
        std::snprintf(k, sizeof k, "th_%s", cn[i]);
        put_flag(k, on_page(0, known && c > int(tyre_hot_above)));
        std::snprintf(k, sizeof k, "tn_%s", cn[i]);
        put_flag(k, on_page(0, known && c >= int(tyre_cold_below) && c <= int(tyre_hot_above)));
    }

    // fuel margin, colour coded from the game's packets: green with fuel to spare, red when it needs saving;
    // without them the game's own value shows, uncoloured
    bool fuel_known = udp_tap::fresh(udp_tap::g.status_tick) && udp_tap::g.fuel_laps > -900.f;
    float fl = udp_tap::g.fuel_laps;
    std::snprintf(t, sizeof t, "%+.2f", double(fl));
    put("fuel", fuel_known ? std::string(t) : std::string("-"));
    put_flag("fuel_game", on_page(0, !fuel_known));
    put_flag("fuel_ok", on_page(0, fuel_known && fl >= 0.f));
    put_flag("fuel_low", on_page(0, fuel_known && fl < 0.f));

    static double logt = 0.0;
    logt += g_step;
    if (verbose && logt > 10.0) {
        logt = 0.0;
        swofty::log("[cockpit-hud] %c lap %d %.1f m clock %.2f last %.3f (game %.3f) best %.3f delta %s%.3f soc %d pos %d gap %d",
                    r.src, r.lap, double(r.lap_m), g_laps.clock, double(last), double(game_last), double(g_laps.best_s),
                    g_laps.have_delta ? "" : "(none) ", g_laps.delta, r.soc, r.pos, r.gap_ms);
        swofty::telemetry::SessionState s{};
        s.version = swofty::telemetry::version;
        swofty::telemetry::CarState c{};
        bool sess = swofty::telemetry::session(&s) == swofty::telemetry::Result::ok;
        bool car = sess && telemetry_car(c);
        swofty::log("[cockpit-hud] telemetry: session %d (udp format %u, packets %u, player %u, time %.2f left %.0f, laps %u), "
                    "car %d: flags %#x lap %u last %.3f fuel %.2f laps %.2f tyres [%.0f %.0f %.0f %.0f] wear [%.0f %.0f %.0f %.0f]",
                    int(sess), s.udp_format, s.packets_seen, s.player_index, double(s.session_time),
                    double(s.session_time_left), s.total_laps, int(car), c.flags, c.lap,
                    double(c.last_lap_time), double(c.fuel), double(c.fuel_laps), double(c.tyre_temp[0]),
                    double(c.tyre_temp[1]), double(c.tyre_temp[2]), double(c.tyre_temp[3]), double(c.tyre_wear[0]),
                    double(c.tyre_wear[1]), double(c.tyre_wear[2]), double(c.tyre_wear[3]));
        swofty::log("[cockpit-hud] udp tap calls: sendto [%u %u %u %u] WSASendTo [%u %u]",
                    unsigned(udp_tap::g_calls[0][0]), unsigned(udp_tap::g_calls[0][1]), unsigned(udp_tap::g_calls[0][2]),
                    unsigned(udp_tap::g_calls[0][3]), unsigned(udp_tap::g_calls[1][0]), unsigned(udp_tap::g_calls[1][1]));
        swofty::log("[cockpit-hud] udp tap: %u packet(s), format %d, player %d; status %d (mix %d bbal %d fuel %.2f ers %d) "
                    "setup %d (diff %d) damage %d (wear %.1f %.1f %.1f %.1f) lap %d (last %u ms)",
                    unsigned(udp_tap::g.packets), int(udp_tap::g.format), int(udp_tap::g.player),
                    int(udp_tap::fresh(udp_tap::g.status_tick)), int(udp_tap::g.mix), int(udp_tap::g.bbal),
                    double(udp_tap::g.fuel_laps), int(udp_tap::g.ers_mode), int(udp_tap::fresh(udp_tap::g.setup_tick)),
                    int(udp_tap::g.diff), int(udp_tap::fresh(udp_tap::g.damage_tick)), double(udp_tap::g.wear[0]),
                    double(udp_tap::g.wear[1]), double(udp_tap::g.wear[2]), double(udp_tap::g.wear[3]),
                    int(udp_tap::fresh(udp_tap::g.lap_tick)), unsigned(udp_tap::g.last_lap_ms));
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
        swofty::log("[cockpit-hud] udp tap (try %d): %s; sendto slot(s) %d, WSASendTo slot(s) %d, %d MB scanned%s%s", g_tap_tries,
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
    swofty::log("[cockpit-hud] loaded 0.4.20: hud %s, live %s, telemetry %s", swofty::hud::available() ? "available" : "not available",
                swofty::live::available() ? "available" : "not available (yet)",
                swofty::telemetry::available() ? "available" : "not available (yet)");
    swofty::events::on_frame(on_frame);
    // who drives the player's car: logged (to learn when the game hands it over), and a switch to the human marks
    // the end of the race intro for the boot screen. A notify-only handler: it never changes the switch.
    auto r = swofty::events::on<swofty::sessions::events::PlayerControl>().after(
        [](const swofty::sessions::events::PlayerControl &e, const swofty::events::Context &) {
            if (int64_t(e.car.index()) == g_player_vehicle.load()) // the player's car only (it fires for all)
                swofty::log("[cockpit-hud] player control: %s -> %s (reason %d)",
                            e.from == swofty::sessions::Control::human ? "human" : "ai",
                            e.to == swofty::sessions::Control::human ? "human" : "ai", int(e.reason));
            if (e.to == swofty::sessions::Control::human && int64_t(e.car.index()) == g_player_vehicle.load())
                g_to_human = true;
        });
    swofty::log("[cockpit-hud] player control events: %s", r == swofty::events::Result::ok ? "on" : "not available");
    try_tap();
    swofty::events::on_frame(tap_frame);
}

SWOFTY_ADDON()

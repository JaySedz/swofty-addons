// Race Dash: a steering wheel LCD for F1 25, fed from the launcher's car state (no UDP socket of its own).
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
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <swofty/events.hpp>
#include <swofty/hud.hpp>
#include <swofty/live.hpp>
#include <swofty/mixin.hpp>
#include <swofty/telemetry.hpp>
#include <swofty/view.hpp>

#include "packet_values.hpp"
#include "udp_tap.hpp"

SWOFTY_SETTING(bool, verbose, false);

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
            swofty::log("[race-dash] hud %s -> %s", l.path.c_str(), swofty::hud::name(r));
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
};

// the telemetry's tyre temperatures keep the UDP order, RL RR FL FR (checked against the raw log line)
void tyres_from(const swofty::telemetry::CarState &c, Reading &r)
{
    static int logged = 0;
    if (logged++ < 2)
        swofty::log("[race-dash] telemetry tyre_temp raw [%.0f %.0f %.0f %.0f]", double(c.tyre_temp[0]),
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
    }
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
    if (c.ers_store >= 0.f) r.soc = int(std::lround(c.ers_store / 4.0e6f * 100.f)); // 4 MJ store
    r.lap = int(c.lap) - 1; // the UDP lap number is the current lap (1 on the first); laps completed, as live
    r.lap_m = c.lap_distance;
    if (c.position > 0) r.pos = c.position;
    if (c.gap_front_ms > 0) r.gap_ms = int(c.gap_front_ms);
    r.tel_last_s = seconds(c.last_lap_time);
    if (c.flags & SWOFTY_CAR_FLAG_STATUS) r.fuel_laps = c.fuel_laps; // only once a status packet filled it (0.1.3 showed +0.00)
    tyres_from(c, r);
    return r;
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
        swofty::log("[race-dash] lap clock: %s", use_gt ? "the game's session time" : "frame time");
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
            swofty::log("[race-dash] lap clock held: the car is frozen at %.1f m (a replay?), pause reasons %#x",
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
    // slow). The game's session time already went back by itself.
    if (r.lap == L.lap && prev_m >= 0.f && r.lap_m >= 0.f && r.lap_m < prev_m - 30.f && prev_m - r.lap_m < 2000.f) {
        int b = int(r.lap_m / kBin);
        if (b >= 0 && b < kBins) {
            if (!use_gt && L.cur[b] >= 0.f)
                L.clock = L.cur[b];
            for (int i = b + 1; i < kBins; i++)
                L.cur[i] = -1.f;
            swofty::log("[race-dash] flashback: %.0f m -> %.0f m, lap clock %.2f", double(prev_m), double(r.lap_m), L.clock);
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
            swofty::log("[race-dash] off the grid at %.1f m: the opening lap is timed from here", double(r.lap_m));
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

void popups(const Reading &r, const std::string &mode, double dt)
{
    static int last_mode = -2;
    static double blue_left = 0.0;
    static std::string blue_title, blue_value;
    auto pop = [&](const char *title, const std::string &value) {
        blue_title = title;
        blue_value = value;
        blue_left = 2.0;
    };
    if (r.ers_mode != last_mode) {
        if (last_mode >= 0 && r.ers_mode >= 0)
            pop("ERS MODE", mode);
        swofty::log("[race-dash] ERS mode %d -> %d", last_mode, r.ers_mode);
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
    if (blue_left > 0.0)
        blue_left -= dt;
    bool red = r.red_flag, amber = !red && r.sc != 0, blue = !red && !amber && blue_left > 0.0;
    if (red) {
        put("pop_title", "FLAG");
        put("pop_value", "RED FLAG");
    } else if (amber) {
        put("pop_title", r.sc == 3 ? "FORMATION LAP" : "FLAG");
        put("pop_value", r.sc == 1 ? "SAFETY CAR" : r.sc == 2 ? "VIRTUAL SAFETY CAR" : "FORMATION");
    } else if (blue) {
        put("pop_title", blue_title);
        put("pop_value", blue_value);
    }
    put_flag("pop_red", red);
    put_flag("pop_amber", amber);
    put_flag("pop_blue", blue);
    put_flag("pop_on", red || amber || blue);
    put_flag("ers_show", !(red || amber || blue));
    static int last_flag = -1;
    int flag = red ? 9 : r.sc;
    if (flag != last_flag) {
        swofty::log("[race-dash] track status: %s", red ? "red flag" : r.sc == 1 ? "safety car" : r.sc == 2 ? "VSC" : r.sc == 3 ? "formation" : "green");
        last_flag = flag;
    }
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
    if (r.src != last_src) {
        last_src = r.src;
        swofty::log("[race-dash] data source: %s", r.src == 'L' ? "live state" : r.src == 'T' ? "telemetry" : "none");
        if (!r.src) {
            g_laps = Laps{}; // the session ended
            g_session_s = 0.0;
        }
    }
    if (!r.src)
        return;
    if (r.src == 'L')
        telemetry_extras(r);
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
            swofty::log("[race-dash] pause reasons %#x -> %#x", last_pr, pr);
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
    std::snprintf(t, sizeof t, "%d", r.rpm < 0 ? 0 : r.rpm);
    put("rpm", t);

    if (g_laps.have_delta) {
        double d = g_laps.delta;
        std::snprintf(t, sizeof t, "%c%.2f", d < 0 ? '-' : '+', std::fabs(d));
        put("delta", t);
    }
    put_flag("delta_neg", g_laps.have_delta && g_laps.delta < 0);
    put_flag("delta_pos", g_laps.have_delta && g_laps.delta >= 0);
    put_flag("delta_none", !g_laps.have_delta);

    std::snprintf(t, sizeof t, "%d", r.speed_kmh < 0 ? 0 : r.speed_kmh);
    put("speed", t);
    // the lap you're on, as the game's HUD counts it (live gives laps completed)
    std::snprintf(t, sizeof t, "%d", r.lap >= 0 ? r.lap + 1 : 0);
    put("lap", t);

    int soc = r.soc < 0 ? 0 : r.soc > 100 ? 100 : r.soc;
    std::snprintf(t, sizeof t, "%d", soc);
    put("soc", t);
    put_flag("soc_low", r.soc >= 0 && soc < 15);
    put_flag("soc_ok", r.soc >= 0 && soc >= 15);
    int lit = r.soc < 0 ? 0 : (soc + 2) / 5;
    char seg[8];
    for (int i = 0; i < 20; i++) {
        std::snprintf(seg, sizeof seg, "soc%02d", i + 1);
        put_flag(seg, i < lit);
    }

    if (r.pos > 0) std::snprintf(t, sizeof t, "P%d", r.pos); else std::snprintf(t, sizeof t, "-");
    put("pos", t);
    if (r.gap_ms > 0 && r.pos > 1) std::snprintf(t, sizeof t, "+%.3f", r.gap_ms / 1000.0); else std::snprintf(t, sizeof t, "-");
    put("gap_a", t);
    if (r.gap_b_ms > 0) std::snprintf(t, sizeof t, "-%.3f", r.gap_b_ms / 1000.0); else std::snprintf(t, sizeof t, "-");
    put("gap_b", t);

    // ERS deploy mode by name. The 2026 game reports 1 normally and 2 while boosting (0.1.5 log, the user's
    // report); 0 and 3 keep the UDP spec's names. Another value shows as its number and is logged.
    static const char *modes[] = {"NONE", "MEDIUM", "BOOST", "OVERTAKE"};
    std::string mode = r.ers_mode >= 0 && r.ers_mode < 4 ? modes[r.ers_mode]
                       : r.ers_mode >= 0                  ? "MODE " + std::to_string(r.ers_mode)
                                                          : std::string("-");
    put("ers_mode", mode);
    // brake bias and fuel mix (UDP only)
    bool status = udp_tap::fresh(udp_tap::g.status_tick);
    put("bbal", status ? std::to_string(int(udp_tap::g.bbal)) + ".0" : std::string("-"));
    put("mix", status ? mix_name(udp_tap::g.mix) : "-");
    if (udp_tap::fresh(udp_tap::g.damage_tick))
        for (int i = 0; i < 4; i++)
            r.wear[i] = udp_tap::g.wear[i];
    popups(r, mode, g_step);
    static const char *wn[] = {"w_fl", "w_fr", "w_rl", "w_rr"};
    for (int i = 0; i < 4; i++) {
        if (r.wear[i] >= 0.f) std::snprintf(t, sizeof t, "%d%%", int(std::lround(r.wear[i])));
        else std::snprintf(t, sizeof t, "--");
        put(wn[i], t);
    }

    static double logt = 0.0;
    logt += g_step;
    if (verbose && logt > 10.0) {
        logt = 0.0;
        swofty::log("[race-dash] %c lap %d %.1f m clock %.2f last %.3f (game %.3f) best %.3f delta %s%.3f soc %d pos %d gap %d",
                    r.src, r.lap, double(r.lap_m), g_laps.clock, double(last), double(game_last), double(g_laps.best_s),
                    g_laps.have_delta ? "" : "(none) ", g_laps.delta, r.soc, r.pos, r.gap_ms);
        swofty::telemetry::SessionState s{};
        s.version = swofty::telemetry::version;
        swofty::telemetry::CarState c{};
        bool sess = swofty::telemetry::session(&s) == swofty::telemetry::Result::ok;
        bool car = sess && telemetry_car(c);
        swofty::log("[race-dash] telemetry: session %d (udp format %u, packets %u, player %u, time %.2f left %.0f, laps %u), "
                    "car %d: flags %#x lap %u last %.3f fuel %.2f laps %.2f tyres [%.0f %.0f %.0f %.0f] wear [%.0f %.0f %.0f %.0f]",
                    int(sess), s.udp_format, s.packets_seen, s.player_index, double(s.session_time),
                    double(s.session_time_left), s.total_laps, int(car), c.flags, c.lap,
                    double(c.last_lap_time), double(c.fuel), double(c.fuel_laps), double(c.tyre_temp[0]),
                    double(c.tyre_temp[1]), double(c.tyre_temp[2]), double(c.tyre_temp[3]), double(c.tyre_wear[0]),
                    double(c.tyre_wear[1]), double(c.tyre_wear[2]), double(c.tyre_wear[3]));
        swofty::log("[race-dash] udp tap calls: sendto [%u %u %u %u] WSASendTo [%u %u]",
                    unsigned(udp_tap::g_calls[0][0]), unsigned(udp_tap::g_calls[0][1]), unsigned(udp_tap::g_calls[0][2]),
                    unsigned(udp_tap::g_calls[0][3]), unsigned(udp_tap::g_calls[1][0]), unsigned(udp_tap::g_calls[1][1]));
        swofty::log("[race-dash] udp tap: %u packet(s), format %d, player %d; status %d (mix %d bbal %d fuel %.2f ers %d) "
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
        swofty::log("[race-dash] udp tap (try %d): %s; sendto slot(s) %d, WSASendTo slot(s) %d, %d MB scanned%s%s", g_tap_tries,
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
    swofty::log("[race-dash] loaded 0.2.1: hud %s, live %s, telemetry %s", swofty::hud::available() ? "available" : "not available",
                swofty::live::available() ? "available" : "not available (yet)",
                swofty::telemetry::available() ? "available" : "not available (yet)");
    swofty::events::on_frame(on_frame);
    try_tap();
    swofty::events::on_frame(tap_frame);
}

SWOFTY_ADDON()

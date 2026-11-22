// SPDX-License-Identifier: MIT
// Immersive Pitstops, the native part.
//
// 1. Pit box hold (0.2.0). The pit lane has a failsafe: Game_PitLane_UpdateMovement(mgr, dt) (a launcher binding)
//    counts each car's time in its box (status 4, InPitBox) in rec+0xe4 and, when that passes the stop's limit
//    rec+0xe8 (15.77 s for a player stop in the 2026-11-22 test; +5 s while serving a penalty), marks the crew as done
//    and releases the car, whatever the crew are doing. The crew's own "done" message (4 -> 5) is the normal end of a
//    stop; a stuck wheel nut (the longer rear right sequence in erp/) runs past the limit. The timer of a car in its
//    box is kept just under its limit, for at most max_hold seconds, so the crew finishing ends the stop. Tested
//    2026-11-22: held 68 s, then dropped and released normally.
//
// 2. Retirement (0.2.1). A stop that runs past the pit lane's own limit (its failsafe timer reaches rec+0xe8) is a
//    stuck wheel nut: no stock stop gets there, a penalty stop is allowed 5 s more (0.2.3; 0.2.2 used a flat 8 s,
//    which the AI's front wing adjust mistakes, ~10 s, also reached). It rolls retire_chance once; on a hit the stuck
//    wheel nut plays out in
//    full and, when the crew finish (4 -> 5), the team retires the car instead of releasing it (0.2.2; 0.2.1 had a
//    "give up after" time, dropped at the user's request): the car is retired with
//    the game's own MessageVehicleStatusRequest (status 7 retired, as the pause menu's Retire posts it), and kept in
//    its box (its release timer rec+0xf0 held at 0). An AI car is then put in its garage with
//    Game_Pitlane_SetVehicleStatus(..., 9), as the game removes retired AI cars; your own car stays in the box and the
//    game's retirement takes over. The message's type and the bus it goes on are found with the byte pattern Terminal
//    Damage uses (uses work from Terminal Damage by nyannoying, https://github.com/nyannoying1337/f1-25-terminal-damage).
//
// 3. Loose wheel (0.2.8; tear-off 0.2.11). After a normal stop (not a stuck wheel nut) loose_chance rolls once for
//    your car when the crew finish: once released you get "LOOSE WHEEL - BOX THIS LAP". Entering the pit lane gives
//    a normal stop that fixes it. Crossing the line on track without pitting tears the wheel off (Terminal Damage's
//    method, below), and the game's own wheel loss ends your race; kFallback seconds later a car still running is
//    retired. AI cars: ai_loose_chance (very rare) after a normal stop, and the wheel comes off 5-25 s into the
//    out-lap. (0.2.8-0.2.10 lowered the live suspension damage fields instead: no wheel came off, as kerbs never
//    count as hits and the car's suspension damage scales are 0.)
//
// Offsets and the pattern are our own (from the launcher's notes and Terminal Damage), so this may stop working after
// a game update: every read is guarded, the plugin checks the pit lane record's garage (+0xe0) against the garage the
// launcher's live state reports before it writes anything, and a mismatch turns it off for the session (logged).
#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <random>
#include <string>

#include <swofty/events.hpp>
#include <swofty/hud.hpp>
#include <swofty/live.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/systems/pitlane.hpp>
#include <swofty/systems/race_control.hpp>
#include <swofty/systems/sessions.hpp>

SWOFTY_SETTING(bool, hold_box, true);      // keep the failsafe off while a car's crew work
SWOFTY_SETTING(int, max_hold, 120);        // seconds in the box after which the game's failsafe is let back in
SWOFTY_SETTING(int, retire_chance, 25);    // % of stuck wheel nuts that end the car's race
SWOFTY_SETTING(int, loose_chance, 3);      // % of your normal race stops that leave a loose wheel
SWOFTY_SETTING(float, ai_loose_chance, 1.0f);  // % of AI stops whose loose wheel comes off on the out-lap
SWOFTY_SETTING(bool, pit_log, true);       // log each stop's timer and limit (for testing)

namespace {

constexpr uint32_t kCars = 32;
constexpr uintptr_t kVehicles = 0x938, kStride = 0x10; // PittingVehicle pointers in the movement manager
constexpr uintptr_t kGarage = 0xe0, kTimer = 0xe4, kLimit = 0xe8, kReleaseTimer = 0xf0;
constexpr uint32_t kInPitBox = 4, kCrewDone = 5, kInGarage = 9;
constexpr float kMargin = 0.5f;  // how far under its limit the timer is kept
constexpr float kToGarage = 5.0f; // seconds after an AI car's retirement before it is put in its garage

// Game::setLocalPlayersStatuses (the pause menu's Retire) posts MessageVehicleStatusRequest to the game's message
// bus: `mov rcx, [rip+holder]` at +0, `lea rdx, [rip+vtable]` at +0x13; the bus is [[holder] + 8]. The message:
// {vtable, 0, u32 vehicle | status << 8 | reason << 16 | 1 << 24}. Status 7 retired; reason 2 as the pause menu
// sends it. (Terminal Damage's signature and layout.)
constexpr const char *kStatusRequestSite =
    "48 8B 0D ?? ?? ?? ?? 48 C7 84 24 90 00 00 00 00 00 00 00 48 8D 15 ?? ?? ?? ?? 48 89 94 24 88 00 00 00 89 84 24 "
    "98 00 00 00 48 8B 49 08 4C 89 F2 E8 ?? ?? ?? ??";
constexpr uint32_t kStatusRetired = 7, kReasonRetired = 2;
const void *g_status_vtable = nullptr;
uintptr_t g_bus_holder = 0;
uint64_t (*g_post)(void *, void *) = nullptr; // Ne_Post_BusMessage

std::atomic<uint32_t> g_status[kCars];   // pit status per car, from session.pit
std::atomic<uint32_t> g_stop[kCars];     // counts entries into the box, to reset the per-stop state
std::atomic<bool> g_wants_retire[kCars]; // the roll landed for the car's current stop
std::atomic<bool> g_retire_now[kCars];   // the crew finished first: retire at once
std::atomic<bool> g_off{false};          // a check failed: hands off until the session changes
std::atomic<void *> g_pit_system{nullptr}; // the PitLaneSystem (self of Game_Pitlane_SetVehicleStatus)

struct Stop {
    uint32_t seen = 0;      // g_stop value this state belongs to
    float in_box = 0;       // seconds in the box, from the pit lane's own dt
    float next_log = 0;
    float since_retire = 0;
    bool checked = false;   // the record passed the garage check
    bool held = false;      // we have kept its timer down at least once
    bool released = false;  // max_hold passed: the game's failsafe is back
    bool rolled = false;    // stuck: the retirement roll is done
    bool retired = false;   // the retirement message went out
    bool to_garage = false; // the AI car was put in its garage
    bool player = false;
};
Stop g_stops[kCars]; // only touched on the game thread, inside the wrap

// random numbers for the rolls, from the game thread and the flow thread
uint32_t rnd()
{
    static std::mt19937 r{std::random_device{}()};
    static std::mutex mu;
    std::lock_guard<std::mutex> lk(mu);
    return r();
}

void off(const char *why, uint32_t car)
{
    if (!g_off.exchange(true))
        swofty::log("[immersive-pitstops] off for this session (car %u: %s); stops end the game's way", car, why);
}

bool write_float(uintptr_t at, float v)
{
    if (!swofty::mem::readable(at, sizeof v))
        return false;
    __try {
        *reinterpret_cast<volatile float *>(at) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The record's garage against the live state's: the offsets are ours, so nothing is written unless this holds.
// No live garage (the live state off, or this build doesn't read it): the values must at least look like a timer.
bool check(uint32_t car, uintptr_t rec, float timer, float limit)
{
    uint32_t garage = 0;
    if (!swofty::mem::read(rec + kGarage, garage))
        return false;
    if (!(timer >= 0.0f && timer < 600.0f && limit > 0.0f && limit < 600.0f))
        return false;
    if (auto c = swofty::live::car(car)) {
        if (auto g = swofty::pitlane::garage(*c)) {
            bool same = (garage & 0xff) == *g;
            if (bool(pit_log))
                swofty::log("[immersive-pitstops] car %u: record garage %u, live garage %u (%s)", car, garage & 0xff,
                            unsigned(*g), same ? "match" : "MISMATCH");
            return same;
        }
    }
    if (bool(pit_log))
        swofty::log("[immersive-pitstops] car %u: no live garage to check against (record garage %u); timer and limit look "
                    "sane", car, garage & 0xff);
    return true;
}

void find_status_request()
{
    if (!swofty::api || !swofty::api->scan || !swofty::api->resolve)
        return;
    g_post = reinterpret_cast<uint64_t (*)(void *, void *)>(swofty::api->resolve("Ne_Post_BusMessage"));
    uintptr_t site = 0;
    int n = swofty::api->scan(".xtls", kStatusRequestSite, &site);
    if (n != 1 || !site) {
        swofty::log("[immersive-pitstops] retirement off: the status request pattern has %d hits on this build", n);
        return;
    }
    int32_t rel = 0;
    std::memcpy(&rel, reinterpret_cast<const void *>(site + 3), 4);
    g_bus_holder = site + 7 + int64_t(rel);
    std::memcpy(&rel, reinterpret_cast<const void *>(site + 0x16), 4);
    g_status_vtable = reinterpret_cast<const void *>(site + 0x1a + int64_t(rel));
    swofty::log("[immersive-pitstops] retirement: status request found (bus post %s)", g_post ? "bound" : "NOT BOUND");
}

// MessageVehicleStatusRequest for one car, on the game thread.
bool post_retired(uint32_t car)
{
    uintptr_t holder = 0, bus = 0;
    if (!g_post || !g_status_vtable || !g_bus_holder || !swofty::mem::read(g_bus_holder, holder) || !holder ||
        !swofty::mem::read(holder + 8, bus) || !bus)
        return false;
    struct {
        const void *vtable;
        uint64_t zero;
        uint32_t packed;
        uint8_t pad[0x2c];
    } msg{};
    msg.vtable = g_status_vtable;
    msg.packed = car | kStatusRetired << 8 | kReasonRetired << 16 | 1u << 24;
    g_post(reinterpret_cast<void *>(bus), &msg);
    return true;
}

void (*orig_set_status)(void *, uint8_t, uint32_t) = nullptr;

void set_status(void *self, uint8_t car, uint32_t status)
{
    g_pit_system.store(self);
    orig_set_status(self, car, status);
}

const swofty::detail::mixin reg_status{"Game_Pitlane_SetVehicleStatus", SWOFTY_MIXIN_WRAP, 0, (void *)&set_status,
                                       (void **)&orig_set_status};

void retire(uint32_t car, Stop &st, const char *when)
{
    if (!post_retired(car)) {
        swofty::log("[immersive-pitstops] car %u: retirement message could not be sent; the stop ends normally", car);
        st.retired = false;
        g_wants_retire[car].store(false);
        return;
    }
    st.retired = true;
    st.since_retire = 0;
    swofty::log("[immersive-pitstops] car %u: the team gives up on the stuck wheel nut (%s, %.1f s in the box): retired",
                car, when, st.in_box);
    if (st.player)
        swofty::hud::toast("RETIRED", "Stuck wheel nut: the team retires the car", 6.0f);
}

namespace loose { void game_tick(); }

void (*orig_update)(void *, float) = nullptr;

void update(void *mgr, float dt)
{
    uintptr_t m = reinterpret_cast<uintptr_t>(mgr);
    loose::game_tick(); // wheel tear-offs asked for on the flow thread
    if (m && !g_off.load()) {
        int64_t player = -1;
        if (auto me = swofty::live::player())
            player = int64_t(me->vehicle);
        for (uint32_t car = 0; car < kCars; ++car) {
            Stop &st = g_stops[car];
            uint32_t stop = g_stop[car].load();
            if (st.seen != stop)
                st = Stop{stop};
            uint32_t status = g_status[car].load();
            bool in_box = status == kInPitBox || (st.retired && status == kCrewDone);
            if (!in_box && !(g_retire_now[car].load() && status == kCrewDone))
                continue;
            uintptr_t rec = 0;
            float timer = 0, limit = 0;
            if (!swofty::mem::read(m + kVehicles + car * kStride, rec) || !rec ||
                !swofty::mem::read(rec + kTimer, timer) || !swofty::mem::read(rec + kLimit, limit))
                continue;
            if (st.in_box == 0) {
                st.player = int64_t(car) == player;
                if (bool(pit_log))
                    swofty::log("[immersive-pitstops] car %u in its box%s: failsafe timer %.2f s, limit %.2f s", car,
                                st.player ? " (you)" : "", timer, limit);
            }
            st.in_box += dt;
            if (!st.checked) {
                if (!check(car, rec, timer, limit)) {
                    off("the pit lane record doesn't look like this build's", car);
                    break;
                }
                st.checked = true;
            }
            if (bool(pit_log) && st.in_box >= st.next_log) {
                swofty::log("[immersive-pitstops] car %u: %.1f s in the box, status %u, failsafe timer %.2f / %.2f%s%s", car,
                            st.in_box, status, timer, limit, st.held ? " (held)" : "", st.retired ? " (retired)" : "");
                st.next_log = st.in_box + 2.0f;
            }
            // the retirement roll, once per stuck stop: when the stop runs past the pit lane's own limit
            if (!st.rolled && timer > limit - kMargin) {
                st.rolled = true;
                int chance = int(retire_chance), roll = int(rnd() % 100);
                bool hit = g_status_vtable && roll < chance;
                g_wants_retire[car].store(hit);
                swofty::log("[immersive-pitstops] car %u: stuck wheel nut (%.1f s in the box); retirement roll %d against "
                            "%d %%: %s", car, st.in_box, roll, chance, hit ? "the car retires when the crew finish" : "it gets fixed");
            }
            if (g_wants_retire[car].load() && !st.retired) {
                if (g_retire_now[car].exchange(false))
                    retire(car, st, "the crew finished");
            }
            // keep a retired car in its box: no failsafe and no release
            if (st.retired) {
                if (timer > limit - kMargin)
                    write_float(rec + kTimer, std::fmax(0.0f, limit - kMargin));
                if (status == kCrewDone)
                    write_float(rec + kReleaseTimer, 0.0f);
                st.since_retire += dt;
                void *sys = g_pit_system.load();
                if (!st.player && !st.to_garage && st.since_retire >= kToGarage && sys && orig_set_status) {
                    st.to_garage = true;
                    swofty::log("[immersive-pitstops] car %u: retired car put in its garage", car);
                    orig_set_status(sys, uint8_t(car), kInGarage);
                }
                continue;
            }
            if (!bool(hold_box) || st.released)
                continue;
            if (st.in_box > float(int(max_hold))) {
                st.released = true;
                swofty::log("[immersive-pitstops] car %u: %d s in the box, the game's failsafe may release it now", car,
                            int(max_hold));
                continue;
            }
            // the pit lane adds dt and compares after this wrap hands over, so keep the timer a margin under the limit
            if (timer > limit - kMargin) {
                if (!write_float(rec + kTimer, std::fmax(0.0f, limit - kMargin))) {
                    off("the failsafe timer can't be written", car);
                    break;
                }
                if (!st.held) {
                    st.held = true;
                    swofty::log("[immersive-pitstops] car %u: holding the failsafe at %.1f s in the box (limit %.2f s); the "
                                "crew finishing ends the stop", car, st.in_box, limit);
                }
            }
        }
    }
    orig_update(mgr, dt);
}

const swofty::detail::mixin reg_update{"Game_PitLane_UpdateMovement", SWOFTY_MIXIN_WRAP, 0, (void *)&update,
                                       (void **)&orig_update};

// Diagnostic (0.2.4, with pit_log): the name of every crew sequence a pit stop plays, to find which versions the
// AI's ~10 s mistake stops use (0.2.3 test). PitStopInstance::playSequence (launcher binding
// Game_PitStopInstance_PlaySequence, address only, 2 arguments per the RE notes; the game itself logs "playSequence
// %d %s") is passed straight through; its second argument is read, guarded, as a name or a pointer to one.
uint64_t (*orig_play)(void *, void *, void *, void *) = nullptr;
std::atomic<int> g_play_logged{0};

std::string seq_name(uintptr_t p)
{
    std::string s;
    if (p && swofty::mem::c_string(p, s, 160) && s.size() >= 4 && swofty::mem::printable_utf8(s))
        return s;
    uintptr_t q = 0;
    for (uintptr_t off : {uintptr_t(0), uintptr_t(8), uintptr_t(0x10)})
        if (p && swofty::mem::read(p + off, q) && q && swofty::mem::c_string(q, s, 160) && s.size() >= 4 &&
            swofty::mem::printable_utf8(s))
            return s + (off ? " (at +" + std::to_string(off) + ")" : " (via pointer)");
    return {};
}

uint64_t play_seq(void *inst, void *a2, void *a3, void *a4)
{
    if (bool(pit_log) && g_play_logged.load() < 400) {
        g_play_logged.fetch_add(1);
        std::string n = seq_name(reinterpret_cast<uintptr_t>(a2));
        if (n.empty())
            n = seq_name(reinterpret_cast<uintptr_t>(a3));
        swofty::log("[immersive-pitstops] crew sequence: %s (instance %p, args %p %p)", n.empty() ? "?" : n.c_str(), inst,
                    a2, a3);
    }
    return orig_play(inst, a2, a3, a4);
}

const swofty::detail::mixin reg_play{"Game_PitStopInstance_PlaySequence", SWOFTY_MIXIN_WRAP, 0, (void *)&play_seq,
                                     (void **)&orig_play};

// --- 3. Loose wheel ---------------------------------------------------------------------------------------------
// The wheel comes off the way Terminal Damage tears one off (its susp::tear_off, 0.76): the car's suspension damage
// component is [car + 0x5d0] (it points back at the car at +8); the game's damage of corner i (RL, RR, FL, FR) is the
// float at +0x130 + 4i and its wheel detach limit the XOR pair +0x58 ^ +0x60. Writing the damage just past the limit
// makes the game's own check tear the wheel off on its next update, and the game rates the car terminal about 1 s
// later. Game thread. The car object is the launcher's live car handle (cars::live::cars()).
namespace loose {
enum State { none, pending, loose, pitting, fixed, missed, done };
std::atomic<int> g_state{none};
std::atomic<int> g_car{-1};          // your car with the loose wheel
std::atomic<int> g_corner{0};        // which wheel (0 RL, 1 RR, 2 FL, 3 FR)
std::atomic<bool> g_crossed{false};  // the car crossed the line on track while loose
std::atomic<int> g_tear[kCars];      // tear-off requests for the game thread: corner + 1
constexpr float kFallback = 20.0f;   // seconds after the tear-off before the car is retired if still running
constexpr const char *kCorner[] = {"rear left", "rear right", "front left", "front right"};
double g_missed_for = 0;
double g_crossed_for = 0;           // a crossing counts only if no red flag follows within kCrossWait
constexpr double kCrossWait = 1.5;  // a red flag moves cars into the pit lane, which reads as a line crossing
std::atomic<bool> g_rebuilt[kCars]; // the car was built again (a restart): forget its loose wheel
// flashbacks after the wheel came off: your car's progress (lap, metres) going backwards means one
int g_tear_lap = 0;                 // the lap your car was on when its wheel came off (past the line)
int g_prev_lap = -1;
float g_prev_m = 0;
double g_fb_quiet = -1;             // >= 0: a flashback was seen; seconds since the last backward step

// AI cars: a wheel left loose comes off on the out-lap
struct Ai {
    int state = 0;   // 0 none, 1 rolled (waiting to be on track), 2 on track (counting down), 3 torn off, 4 done
    int corner = 0;
    double t = 0, delay = 0;
};
Ai g_ai[kCars];                       // flow thread only
std::atomic<bool> g_ai_reset{false};  // a new session: clear g_ai on the flow thread
std::atomic<int> g_ai_rolled[kCars]; // set by the pit event (game thread): corner + 1

int random_corner() { return int(rnd() % 4); }

// Game thread: tear the wheel off.
bool tear_off(uint32_t car, int corner)
{
    uintptr_t h = 0;
    for (auto &c : swofty::cars::live::cars())
        if (c.index() == car)
            h = reinterpret_cast<uintptr_t>(c.handle());
    uintptr_t comp = 0, back = 0;
    uint32_t a = 0, b = 0;
    if (!h || !swofty::mem::read(h + 0x5d0, comp) || !comp || !swofty::mem::read(comp + 8, back) || back != h ||
        !swofty::mem::read(comp + 0x58, a) || !swofty::mem::read(comp + 0x60, b)) {
        swofty::log("[immersive-pitstops] loose wheel: car %u's suspension damage isn't where expected (car %p); no wheel "
                    "off", car, reinterpret_cast<void *>(h));
        return false;
    }
    uint32_t bits = a ^ b;
    float w = 0, d = 0;
    std::memcpy(&w, &bits, 4);
    if (!(w >= 0.2f && w <= 20.0f) || !swofty::mem::read(comp + 0x130 + 4 * corner, d)) {
        swofty::log("[immersive-pitstops] loose wheel: car %u's wheel detach limit reads %.3f; no wheel off", car, w);
        return false;
    }
    if (!write_float(comp + 0x130 + 4 * corner, std::fmax(d, 1.02f * w)))
        return false;
    swofty::log("[immersive-pitstops] loose wheel: car %u's %s wheel comes off (damage %.2f -> %.2f, detach limit %.2f)",
                car, kCorner[corner], d, std::fmax(d, 1.02f * w), w);
    return true;
}

// Game thread, from the pit lane update: carry out the tear-off requests.
void game_tick()
{
    for (uint32_t car = 0; car < kCars; ++car)
        if (int r = g_tear[car].exchange(0))
            tear_off(car, r - 1);
}

// Flow thread, every frame: your car, then the AI cars.
void frame(double dt, void *)
{
    // a red flag: tyres may be changed, so every loose wheel still on its car is fixed
    auto rc = swofty::race_control::now();
    bool red = rc && (rc->red_flag == swofty::race_control::RedFlag::deployed ||
                      rc->red_flag == swofty::race_control::RedFlag::suspended);
    if (red) {
        int ps = g_state.load();
        if (ps == pending || ps == loose || ps == pitting) {
            g_state.store(none);
            swofty::hud::toast("RED FLAG", "The crew fix the loose wheel", 5.0f);
            swofty::log("[immersive-pitstops] loose wheel: red flag; car %d's wheel is fixed", g_car.load());
        }
        for (uint32_t i = 0; i < kCars; ++i)
            if (g_ai[i].state == 1 || g_ai[i].state == 2) {
                g_ai[i] = Ai{};
                swofty::log("[immersive-pitstops] loose wheel: red flag; AI car %u's wheel is fixed", i);
            }
    }
    // a restart builds the cars again
    for (uint32_t i = 0; i < kCars; ++i)
        if (g_rebuilt[i].exchange(false)) {
            g_ai[i] = Ai{};
            if (int(i) == g_car.load() && g_state.load() != none) {
                g_state.store(none);
                swofty::log("[immersive-pitstops] loose wheel: car %u was rebuilt (a restart); forgotten", i);
            }
        }
    int st = g_state.load(), car = g_car.load();
    if (st != none && car >= 0) {
        uint32_t pit = g_status[car].load();
        if (st == fixed) {
            g_state.store(none);
            swofty::log("[immersive-pitstops] loose wheel: car %u pitted; the wheel is on properly now", car);
        } else if (st == pending && (pit == 6 || pit == 7 || pit == 14 || pit == 1)) {
            g_state.store(loose);
            g_crossed.store(false);
            g_crossed_for = 0;
            swofty::hud::toast("LOOSE WHEEL", "The wheel isn't on properly. Box this lap!", 8.0f);
            swofty::log("[immersive-pitstops] loose wheel: car %u released with a loose %s wheel; box this lap", car,
                        kCorner[g_corner.load()]);
        } else if (st == loose && (pit == 2 || pit == 3 || pit == 4)) {
            g_state.store(pitting);
            swofty::log("[immersive-pitstops] loose wheel: car %u is in the pit lane", car);
        } else if (st == loose && g_crossed.load() && (g_crossed_for += dt) < kCrossWait) {
            // wait: a red flag right after the crossing means the crossing was the red flag moving the car
        } else if (st == loose && g_crossed.load()) {
            g_state.store(missed);
            g_missed_for = 0;
            g_tear[car].store(g_corner.load() + 1);
            g_tear_lap = -1;
            g_prev_lap = -1;
            g_fb_quiet = -1;
            if (auto pc = swofty::live::car(uint32_t(car)); pc && swofty::live::has(*pc, SWOFTY_LIVE_HAVE_PROGRESS))
                g_tear_lap = pc->lap;
            swofty::hud::toast("LOOSE WHEEL", "You didn't box: the wheel has come off", 6.0f);
            swofty::log("[immersive-pitstops] loose wheel: car %u stayed out; its wheel comes off", car);
        } else if (st == missed) {
            g_missed_for += dt;
            auto c = swofty::live::car(uint32_t(car));
            // a flashback brings the wheel back: watch the car's progress for a backward jump, wait until the
            // rewind settles, then make it loose again (back before the line) or take it off again (still past it)
            if (c && swofty::live::has(*c, SWOFTY_LIVE_HAVE_PROGRESS)) {
                int lap = c->lap;
                float m = c->lap_m;
                bool back = g_prev_lap >= 0 && (lap < g_prev_lap || (lap == g_prev_lap && m < g_prev_m - 20.0f));
                g_prev_lap = lap, g_prev_m = m;
                if (back) {
                    g_fb_quiet = 0;
                    g_missed_for = 0; // the fallback starts again after a flashback
                }
                else if (g_fb_quiet >= 0 && (g_fb_quiet += dt) >= 1.0) {
                    g_fb_quiet = -1;
                    if (g_tear_lap >= 0 && lap < g_tear_lap) {
                        g_state.store(loose);
                        g_crossed.store(false);
                        g_crossed_for = 0;
                        swofty::hud::toast("LOOSE WHEEL", "The wheel still isn't on properly. Box this lap!", 6.0f);
                        swofty::log("[immersive-pitstops] loose wheel: car %d flashed back before the line (lap %d, the "
                                    "wheel came off on %d): loose again", car, lap, g_tear_lap);
                        return;
                    }
                    g_missed_for = 0;
                    g_tear[car].store(g_corner.load() + 1);
                    swofty::log("[immersive-pitstops] loose wheel: car %d flashed back past the line (lap %d): the "
                                "wheel comes off again", car, lap);
                }
            }
            auto status = c ? swofty::race_control::status(*c) : std::nullopt;
            if (status && *status != swofty::race_control::Status::running) {
                g_state.store(done);
                swofty::log("[immersive-pitstops] loose wheel: car %u is out of the race (status %s)", car,
                            swofty::race_control::name(*status));
            } else if (g_missed_for > kFallback && g_fb_quiet < 0) {
                g_state.store(done);
                swofty::log("[immersive-pitstops] loose wheel: car %u still running %.0f s after its wheel came off; "
                            "retiring it", car, g_missed_for);
                if (post_retired(uint32_t(car)))
                    swofty::hud::toast("RETIRED", "Loose wheel: the team retires the car", 6.0f);
            }
        }
    }
    if (g_ai_reset.exchange(false))
        for (auto &a : g_ai)
            a = Ai{};
    for (uint32_t i = 0; i < kCars; ++i) {
        Ai &a = g_ai[i];
        if (int r = g_ai_rolled[i].exchange(0)) {
            a = Ai{1, r - 1, 0, 5.0 + double(rnd() % 21)};  // off 5-25 s into the out-lap
            continue;
        }
        if (a.state == 1 && g_status[i].load() == 1) {
            a.state = 2;
            a.t = 0;
        } else if (a.state == 2 && (a.t += dt) >= a.delay) {
            a.state = 3;
            a.t = 0;
            g_tear[i].store(a.corner + 1);
        } else if (a.state == 3 && (a.t += dt) >= 30.0) {
            a.state = 4;
            auto c = swofty::live::car(i);
            auto status = c ? swofty::race_control::status(*c) : std::nullopt;
            if (!status || *status == swofty::race_control::Status::running) {
                swofty::log("[immersive-pitstops] loose wheel: AI car %u still running 30 s after its wheel came off; "
                            "retiring it", i);
                post_retired(i);
            }
        }
    }
}
} // namespace loose

} // namespace

SWOFTY_ON_LOAD
{
    swofty::log("[immersive-pitstops] loaded 0.2.14: pit box hold %s, at most %d s; retirement %d %% of stuck wheel nuts "
                "(when the crew finish); log %s", bool(hold_box) ? "on" : "off", int(max_hold), int(retire_chance),
                bool(pit_log) ? "on" : "off");
    find_status_request();
    auto r = swofty::events::on<swofty::sessions::events::Pit>().after(
        [](const swofty::sessions::events::Pit &e, const swofty::events::Context &) {
            if (e.car >= kCars)
                return;
            if (e.status == kInPitBox && e.previous != kInPitBox) {
                g_stop[e.car].fetch_add(1);
                g_wants_retire[e.car].store(false);
                g_retire_now[e.car].store(false);
            }
            // the crew finished a stuck wheel nut whose roll landed: retire now, before the release
            if (e.previous == kInPitBox && e.status == kCrewDone && g_wants_retire[e.car].load())
                g_retire_now[e.car].store(true);
            // the loose wheel (your car, races): the stop that fixes it, or the roll after a normal stop
            // (only races have box stops: in practice and qualifying the car is wheeled into its garage)
            if (e.previous == kInPitBox && e.status == kCrewDone) {
                auto me = swofty::live::player();
                if (me && me->vehicle == e.car) {
                    int ls = loose::g_state.load();
                    if (ls == loose::pitting) {
                        loose::g_state.store(loose::fixed);
                    } else if (ls == loose::none && !g_stops[e.car].rolled && !g_wants_retire[e.car].load()) {
                        int chance = int(loose_chance), roll = int(rnd() % 100);
                        bool hit = roll < chance;
                        swofty::log("[immersive-pitstops] car %u: loose wheel roll %d against %d %%%s", e.car, roll, chance,
                                    hit ? ": the wheel isn't on properly" : "");
                        if (hit) {
                            loose::g_car.store(int(e.car));
                            loose::g_corner.store(loose::random_corner());
                            loose::g_state.store(loose::pending);
                        }
                    }
                } else if (!g_stops[e.car].rolled && !g_wants_retire[e.car].load() &&
                           ![&] { auto c = swofty::live::car(e.car); return c && swofty::pitlane::safety_car(*c); }()) {
                    // an AI car: a very rare wheel left loose comes off on its out-lap
                    float chance = float(ai_loose_chance), roll = float(rnd() % 10000) / 100.0f;
                    if (roll < chance) {
                        int corner = loose::random_corner();
                        loose::g_ai_rolled[e.car].store(corner + 1);
                        swofty::log("[immersive-pitstops] AI car %u: loose wheel roll %.2f against %.2f %%: its %s wheel "
                                    "will come off on the out-lap", e.car, roll, chance, loose::kCorner[corner]);
                    }
                }
            }
            g_status[e.car].store(e.status);
            if (bool(pit_log) && (e.status == kInPitBox || e.previous == kInPitBox || e.previous == kCrewDone))
                swofty::log("[immersive-pitstops] car %u pit status %u -> %u", e.car, e.previous, e.status);
        });
    auto s = swofty::events::subscribe(SWOFTY_EVENT_SESSION_START, [](const SwoftyEvent *, void *) {
        for (uint32_t i = 0; i < kCars; ++i) {
            g_status[i].store(0);
            g_wants_retire[i].store(false);
            g_retire_now[i].store(false);
        }
        g_off.store(false);
        // a new session builds the cars again, so the loose wheel's settings are gone with it
        loose::g_state.store(loose::none);
        loose::g_car.store(-1);
        for (uint32_t i = 0; i < kCars; ++i) {
            loose::g_tear[i].store(0);
            loose::g_ai_rolled[i].store(0);
        }
        loose::g_ai_reset.store(true);
    });
    // crossing the line on track with the wheel still loose: the stop was missed
    auto lp = swofty::events::on<swofty::sessions::events::Lap>().after(
        [](const swofty::sessions::events::Lap &e, const swofty::events::Context &) {
            int car = loose::g_car.load();
            if (loose::g_state.load() == loose::loose && car >= 0 && int(e.car.index()) == car &&
                g_status[car].load() == 1)
                loose::g_crossed.store(true);
        });
    // a restart builds the cars again (no session.start): forget their loose wheels
    swofty::events::on<swofty::cars::events::Built>().after(
        [](const swofty::cars::events::Built &e, const swofty::events::Context &) {
            if (e.car.index() < kCars)
                loose::g_rebuilt[e.car.index()].store(true);
        });
    swofty::events::on_frame(loose::frame);
    swofty::log("[immersive-pitstops] loose wheel %d %% of your stops; lap events %s", int(loose_chance),
                lp == swofty::events::Result::ok ? "on" : "NOT AVAILABLE");
    swofty::log("[immersive-pitstops] session.pit %s, session start %s, pit lane update %s, pit status %s",
                r == swofty::events::Result::ok ? "on" : "NOT AVAILABLE", s == swofty::events::Result::ok ? "on" : "not available",
                swofty::api && swofty::api->resolve && swofty::api->resolve("Game_PitLane_UpdateMovement") ? "found" : "NOT FOUND",
                swofty::api && swofty::api->resolve && swofty::api->resolve("Game_Pitlane_SetVehicleStatus") ? "found"
                                                                                                           : "NOT FOUND");
}

SWOFTY_ADDON()

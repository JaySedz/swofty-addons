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
// 4. Terminal Damage (0.2.15, optional). When your own car's stuck-nut retirement roll landed and Terminal Damage is
//    running, the stuck wheel (from the crew sequence's name) is torn off instead of the retirement message being
//    sent; Terminal Damage rates the car broken (its public API, read only, reports it), and the car is put back in its
//    box state, where Terminal Damage's crew push a broken car into the garage. If Terminal Damage isn't there or
//    doesn't react within a few seconds, the game's own retirement is used as before.
//
// Offsets and the pattern are our own (from the launcher's notes and Terminal Damage), so this may stop working after
// a game update: every read is guarded, the plugin checks the pit lane record's garage (+0xe0) against the garage the
// launcher's live state reports before it writes anything, and a mismatch turns it off for the session (logged).
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
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
#include <swofty/systems/speech.hpp>

// Fixed in 1.0 (settings until 0.2.28): the box hold is always on with a 120 s safety cap, a stop you didn't call keeps
// the crew in the garage for 1 s after you stop in the box, and each stop's crew speed varies by up to 30 %.
constexpr bool hold_box = true;    // keep the failsafe off while a car's crew work
constexpr int max_hold = 120;      // seconds in the box after which the game's failsafe is let back in
constexpr int uncalled_delay = 0;  // extra seconds in the box before the crew of an uncalled stop run out (1.0.1: 0;
                                   // their run-out from the garage is the penalty)
constexpr int stop_spread = 30;    // % each stop's crew speed is varied by
SWOFTY_SETTING(int, retire_chance, 25);    // % of stuck wheel nuts that end the car's race
SWOFTY_SETTING(int, loose_chance, 3);      // % of your normal race stops that leave a loose wheel
SWOFTY_SETTING(float, ai_loose_chance, 1.0f);
SWOFTY_SETTING(bool, call_stop, true);
SWOFTY_SETTING(int, stuck_chance, 3);
SWOFTY_SETTING(bool, ferrari_mode, false); // 1.0.14: Ferrari's stuck nut and loose wheel chances at 50 % (a joke)  // % of stops the plugin turns into a stuck wheel nut (1.0.13)  // stops you don't call with Box This Lap find the crew in the garage  // % of AI stops whose loose wheel comes off on the out-lap
SWOFTY_SETTING(bool, pit_log, false);      // log each stop's timer and limit (for testing)

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

// Ferrari Mode (1.0.14): a car is a Ferrari when its team badge (the launcher's live data, e.g. "ferrari_26") says so.
constexpr int kFerrariChance = 50;
bool ferrari(uint32_t car)
{
    if (!bool(ferrari_mode))
        return false;
    auto b = swofty::live::team_badge(car);
    if (!b)
        return false;
    std::string k = *b;
    for (char &c : k)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return k.find("ferrari") != std::string::npos;
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

int32_t rel32(uintptr_t at)
{
    int32_t r = 0;
    swofty::mem::read(at, r);
    return r;
}

// From the launcher's bindings (0.2.17): Game_Game_RetireLocalPlayer (the pause menu's Retire) calls
// Game::setLocalPlayersStatuses, which loads the message bus holder (`mov rcx, [rip+g_Game_MessageBus]`) and then the
// message's type (`lea rdx, [rip+vtable]`) a few instructions later. Both anchors are the launcher's, kept up to date
// with the game; only the shape of those two instructions is assumed. Checked on the 2026-11-16 build: the same bus
// and vtable as Terminal Damage's pattern.
bool find_by_bindings()
{
    uintptr_t retire = uintptr_t(swofty::api->resolve("Game_Game_RetireLocalPlayer"));
    uintptr_t holder = uintptr_t(swofty::api->resolve("g_Game_MessageBus"));
    if (!retire || !holder) {
        swofty::log("[immersive-pitstops] retirement: launcher bindings %s%s missing (an older launcher)",
                    retire ? "" : "Game_Game_RetireLocalPlayer ", holder ? "" : "g_Game_MessageBus");
        return false;
    }
    uint8_t op = 0;
    for (uintptr_t c = retire; c < retire + 0x100; ++c) {
        if (!swofty::mem::read(c, op) || op != 0xE8)
            continue;
        uintptr_t fn = c + 5 + int64_t(rel32(c + 1));
        uint8_t b[3];
        for (uintptr_t i = fn; i < fn + 0x600; ++i) {
            if (!swofty::mem::read(i, b) || b[0] != 0x48 || b[1] != 0x8B || b[2] != 0x0D || // mov rcx, [rip+x]
                i + 7 + int64_t(rel32(i + 3)) != holder)
                continue;
            for (uintptr_t j = i + 7; j < i + 0x30; ++j) {
                if (!swofty::mem::read(j, b) || b[0] != 0x48 || b[1] != 0x8D || b[2] != 0x15) // lea rdx, [rip+x]
                    continue;
                uintptr_t vt = j + 7 + int64_t(rel32(j + 3)), first = 0;
                if (!swofty::mem::read(vt, first) || !first)
                    break;
                g_bus_holder = holder;
                g_status_vtable = reinterpret_cast<const void *>(vt);
                return true;
            }
        }
    }
    swofty::log("[immersive-pitstops] retirement: the status request isn't where the launcher's bindings lead on this "
                "build");
    return false;
}

void find_status_request()
{
    if (!swofty::api || !swofty::api->scan || !swofty::api->resolve)
        return;
    g_post = reinterpret_cast<uint64_t (*)(void *, void *)>(swofty::api->resolve("Ne_Post_BusMessage"));
    if (find_by_bindings()) {
        swofty::log("[immersive-pitstops] retirement: status request found from the launcher's bindings (bus post %s)",
                    g_post ? "bound" : "NOT BOUND");
        return;
    }
    // fallback for launchers without those bindings: Terminal Damage's byte pattern
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
    swofty::log("[immersive-pitstops] retirement: status request found by the byte pattern (bus post %s)",
                g_post ? "bound" : "NOT BOUND");
}

// --- 7. Crew stay in the garage (0.2.23) ----------------------------------------------------------------------------
// The pit lane calls the crew out of the garage with MessageCarEnteredPitLane {vtable, 0, u8 vehicle @+0x10}, sent
// from Game_PitLane_EnterLimitZone (a launcher binding; "prep garage crew to perform a box stop") through
// Ne_Post_BusMessage on g_Game_MessageBus, right as the car enters the pit lane. On a race stop you didn't call, the
// plugin keeps that message back (its wrap of the launcher binding Ne_Post_BusMessage drops it), so the crew stay in
// the garage, and sends the same message itself once you have stood in the pit lane for uncalled_delay seconds (or
// the car is in its box, or 45 s have passed): the crew run out then. Terminal Damage drops the same message for a
// broken car; a car Terminal Damage has broken never counts as uncalled. The message type is read from the code of
// Game_PitLane_EnterLimitZone, as the retirement message's is: `mov rax, [rip+g_Game_MessageBus]` then
// `lea rcx, [rip+vtable]` a few instructions later.
namespace hold {
const void *g_vt = nullptr;
std::atomic<bool> g_held{false};
std::atomic<int> g_car{-1};
std::atomic<bool> g_reposting{false};
std::atomic<bool> g_release{false}; // the crew's frozen start is over: call them out now (0.2.24)
float g_t = 0, g_stand = 0, g_box = 0; // game thread
std::atomic<uintptr_t> g_garage_inst{0}; // your crew, held in the garage
std::atomic<bool> g_garage_used{false};   // this stop's crew were held in the garage (no second freeze at the car)

void find()
{
    if (!swofty::api || !swofty::api->resolve)
        return;
    uintptr_t fn = uintptr_t(swofty::api->resolve("Game_PitLane_EnterLimitZone"));
    uintptr_t holder = uintptr_t(swofty::api->resolve("g_Game_MessageBus"));
    if (!fn || !holder) {
        swofty::log("[immersive-pitstops] crew hold: launcher bindings %s%s missing; the crew wait in the box instead",
                    fn ? "" : "Game_PitLane_EnterLimitZone ", holder ? "" : "g_Game_MessageBus");
        return;
    }
    uint8_t b[3];
    for (uintptr_t i = fn; i < fn + 0x800; ++i) {
        if (!swofty::mem::read(i, b) || b[0] != 0x48 || b[1] != 0x8B || b[2] != 0x05 || // mov rax, [rip+x]
            i + 7 + int64_t(rel32(i + 3)) != holder)
            continue;
        for (uintptr_t j = i + 7; j < i + 0x30; ++j) {
            if (!swofty::mem::read(j, b) || b[0] != 0x48 || b[1] != 0x8D || b[2] != 0x0D) // lea rcx, [rip+x]
                continue;
            uintptr_t vt = j + 7 + int64_t(rel32(j + 3)), first = 0;
            if (swofty::mem::read(vt, first) && first) {
                g_vt = reinterpret_cast<const void *>(vt);
                swofty::log("[immersive-pitstops] crew hold: the crew's call-out message found from the launcher's bindings");
                return;
            }
            break;
        }
    }
    swofty::log("[immersive-pitstops] crew hold: the call-out message isn't where the launcher's bindings lead; the crew "
                "wait in the box instead");
}
} // namespace hold

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

// Terminal Damage's public API (its terminal_damage_api.h, version 1; Terminal Damage by nyannoying). Only read: it
// reports your car's state. Optional: without Terminal Damage the game's own retirement is used.
struct TdPlayerState {
    uint32_t size, state;
    int32_t vehicle_id;
    uint32_t damage_mask, player_out, reserved;
};
struct TdApi {
    uint32_t version, size;
    int (*get_state)(TdPlayerState *out, uint32_t out_size);
    uint64_t (*subscribe)(void (*)(const TdPlayerState *, void *), void *);
    void (*unsubscribe)(uint64_t);
};
static_assert(sizeof(TdPlayerState) == 24 && sizeof(TdApi) == 32, "Terminal Damage API v1 layout");
enum { TD_UNAVAILABLE, TD_RUNNING, TD_LIMPING, TD_RETIRING, TD_IN_GARAGE, TD_RETIRED };

const TdApi *td_api()
{
    HMODULE m = GetModuleHandleW(L"terminal_damage_probe.dll");
    if (!m)
        return nullptr;
    auto get = reinterpret_cast<const TdApi *(*)(uint32_t)>(GetProcAddress(m, "TerminalDamageGetApi"));
    const TdApi *api = get ? get(1) : nullptr;
    return api && api->version == 1 && api->size >= sizeof(TdApi) && api->get_state ? api : nullptr;
}

// Terminal Damage's player state, or TD_UNAVAILABLE.
uint32_t td_state()
{
    const TdApi *api = td_api();
    TdPlayerState s{};
    s.size = sizeof s;
    if (!api || !api->get_state(&s, sizeof s))
        return TD_UNAVAILABLE;
    return s.state;
}

// Which wheel a stop's crew sequence is about: the "wheel on" mistakes are ...stopflon/fron/rlon/rron (and the fwa
// versions). Corner as the tear-off counts them: 0 RL, 1 RR, 2 FL, 3 FR; -1 none.
int seq_corner(const std::string &name)
{
    std::string s = name;
    for (char &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    if (s.find("rlon") != std::string::npos) return 0;
    if (s.find("rron") != std::string::npos) return 1;
    if (s.find("flon") != std::string::npos) return 2;
    if (s.find("fron") != std::string::npos) return 3;
    return -1;
}
// the last "wheel on" crew sequences: when they started and which wheel
struct SeqSeen {
    std::atomic<uint64_t> at{0};
    std::atomic<int> corner{-1};
};
SeqSeen g_seq[8];
std::atomic<uint32_t> g_seq_next{0};
std::atomic<uint64_t> g_box_at[kCars]; // when each car last reached its box

// The stuck wheel of a car's stop: a "wheel on" sequence started within 3 s of the car reaching its box.
// The rear right (the fixed-length stuck nut's wheel) if none is found.
int stuck_corner(uint32_t car)
{
    uint64_t box = g_box_at[car].load(), best = ~0ull;
    int corner = 1;
    for (auto &s : g_seq) {
        uint64_t at = s.at.load();
        if (!at || at + 1000 < box || at > box + 3000)
            continue;
        uint64_t d = at > box ? at - box : box - at;
        if (d < best)
            best = d, corner = s.corner.load();
    }
    return corner;
}

// Your stuck-nut retirement with Terminal Damage (0.2.15): the stuck wheel is torn off (Terminal Damage's method,
// below), Terminal Damage rates the car broken, then the car is put back in its box state (4), which is where
// Terminal Damage's crew push into the garage starts. Phases per car, game thread.
enum { td_none, td_waiting, td_handed, td_fallback };
std::atomic<int> g_td[kCars];
double g_td_t[kCars];
std::atomic<uint32_t> g_td_seen[kCars];
constexpr double kTdBroken = 6.0, kTdPush = 5.0;

void (*orig_set_status)(void *, uint8_t, uint32_t) = nullptr;

void set_status(void *self, uint8_t car, uint32_t status)
{
    g_pit_system.store(self);
    orig_set_status(self, car, status);
}

const swofty::detail::mixin reg_status{"Game_Pitlane_SetVehicleStatus", SWOFTY_MIXIN_WRAP, 0, (void *)&set_status,
                                       (void **)&orig_set_status};

namespace loose { void game_tick(); bool tear_off(uint32_t car, int corner); }

constexpr bool kTdHandOff = false; // the Terminal Damage garage push hand-off (0.2.15), see retire()

void retire(uint32_t car, Stop &st, const char *when)
{
    // your car with Terminal Damage running: its crew push the car into the garage. Off since 0.2.16: on the
    // 2026-11-16 game build Terminal Damage 0.76.4's pit state and setController hooks find nothing, so the push never
    // starts (0.2.15 test: the torn-off wheel flailed on its tethers, then the game's retirement teleported the car).
    // Turn back on once Terminal Damage is updated for that build.
    if (kTdHandOff && st.player && td_state() == TD_RUNNING && g_pit_system.load() && orig_set_status) {
        int corner = stuck_corner(car);
        if (loose::tear_off(car, corner)) {
            st.retired = true; // held in the box as before while Terminal Damage takes over
            st.since_retire = 0;
            g_td_t[car] = 0;
            g_td[car].store(td_waiting);
            swofty::log("[immersive-pitstops] car %u: the team gives up on the stuck wheel nut (%s, %.1f s in the box); "
                        "the wheel is taken off for Terminal Damage's push into the garage", car, when, st.in_box);
            swofty::hud::toast("RETIRED", "Stuck wheel nut: the team retires the car", 6.0f);
            return;
        }
        swofty::log("[immersive-pitstops] car %u: Terminal Damage is running but the wheel couldn't be taken off; the "
                    "game's own retirement instead", car);
    }
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

void (*orig_update)(void *, float) = nullptr;

// --- 5. Call your stop (0.2.18; crew freeze 0.2.20) ----------------------------------------------------------------
// Picking Box This Lap in the MFD radio menu makes the engineer confirm (an eng_pit_conf_* line, caught with the
// launcher's speech.line event). A race stop you didn't call finds the crew not ready: when the crew's StartJob
// sequence begins, their animation is held still for uncalled_delay seconds (6, below), then the stop runs. A loose
// wheel you were told to box for counts as called. (0.2.18-0.2.19 held the pit lane's crew-ready byte +0x104
// instead; the 2026-11-23 test showed the car goes into its box whatever that byte says.)
namespace call {
std::atomic<bool> g_called{false};
std::atomic<bool> g_uncalled_stop{false}; // your current stop wasn't called
struct Approach {
    bool active = false;
};
Approach g_a; // game thread

bool race_session()
{
    auto s = swofty::live::session();
    return s && s->type >= 15 && s->type <= 17; // the telemetry session types of a race
}

bool tick(uintptr_t m, uint32_t car, float dt);
bool uncalled_now(uint32_t car, bool *told);
} // namespace call

// --- 6. Crew speed per stop (0.2.20) -------------------------------------------------------------------------------
// The game rolls each driver's stop time once per session (2026-11-23 test: 2.65 s, 2.65 s, 2.66 s), so a session's
// stops all look the same. The launcher binding Sess_PitStopUpdateJob_Update(job) advances a crew's animation by the
// frame time at job+0x10 (job+0x08 the crew's PitStopInstance), scaled by the crew rate; scaling that frame time per
// stop spreads the stops (stop_spread %), and setting it to 0 holds the crew still (the uncalled stop). A crew is
// matched to a car when its StartJob sequence starts: the car entering its box at that moment.
namespace crew {
struct Job {
    uintptr_t inst = 0;
    int car = -1;
    float rate = 1, freeze = 0;
    bool garage = false; // the crew's run-out from the garage, held until you have waited in the box (0.2.25)
    bool runout = false; // that run-out, let go: played at the crew's normal pace (1.0.1)
    // a stuck wheel nut (1.0.4): its 60 s struggle runs from s0 for whole repeats of d seconds to end (sequence time);
    // at s0 + len the crew's sequence clock jumps ahead by whole repeats, to the same pose near the end
    bool stuck = false, cut = false, ours = false;
    float s0 = 0, d = 0, end = 0, len = 0;
    int tries = 0; // frames the sequence clock didn't read sanely
};
// Each stuck nut sequence's struggle window, from tools/make_stuck_nut.py (frames at 30 fps): the pause starts where
// the struggle ends (b) and runs `extra` frames, whole repeats of b - a.
struct Window {
    const char *wheel;
    bool fwa;
    int a, b, extra;
};
// 1.0.6: stuck nuts are on the rear wheels only; the front wheels' "on" files hold the rear stuck nut of the same side
// (the game kept naming the front left mistake), so a "...FLON" sequence has the rear left's window.
constexpr Window kWindows[] = {
    {"flon", false, 65, 125, 1800}, {"fron", false, 75, 124, 1813}, {"rlon", false, 78, 129, 1785},
    {"rron", false, 79, 122, 1806}, {"flon", true, 65, 125, 1800},  {"fron", true, 76, 124, 1824},
    {"rlon", true, 79, 133, 1782},  {"rron", true, 79, 124, 1800},
};
// How long a stuck nut lasts: short ones are common, a full minute is about one stuck nut in 25 (at Normal's ~3 % of
// stops, roughly once a season across the grid).
float stuck_length()
{
    struct { float s; int w; } t[] = {{5, 45}, {10, 30}, {20, 14}, {30, 7}, {60, 4}};
    int r = int(rnd() % 100);
    for (auto &e : t) {
        if (r < e.w)
            return e.s;
        r -= e.w;
    }
    return 5;
}
std::atomic<uintptr_t> g_inst_of[kCars]; // each car's crew instance, from its last StartJob (team-mates share one)
// Every team's crew plays F1InGarageDoIdle when the session loads, and the crews are laid out in garage order: in the
// 2026-11-23 log the crews sorted by address matched the garage slot (live garage / 2) of cars 0, 16 and 21. So your
// crew is the one at your garage slot in that sorted list, even before anyone has stopped (0.2.26). A stop seen with
// another order (a track whose garages run the other way) flips the list.
uintptr_t g_seen[24];
int g_seen_n = 0; // under g_mu

// under g_mu
uintptr_t crew_of_slot(int slot)
{
    if (slot < 0 || g_seen_n == 0)
        return 0;
    uintptr_t sorted[24];
    int n = g_seen_n;
    for (int i = 0; i < n; ++i)
        sorted[i] = g_seen[i];
    std::sort(sorted, sorted + n);
    bool reversed = false;
    for (uint32_t c = 0; c < kCars; ++c) { // check the order against the stops seen
        uintptr_t ci = g_inst_of[c].load();
        auto lc = ci ? swofty::live::car(c) : std::nullopt;
        auto g = lc ? swofty::pitlane::garage(*lc) : std::nullopt;
        if (!ci || !g)
            continue;
        int cs = int(*g) / 2;
        if (cs < n && sorted[cs] != ci && sorted[n - 1 - cs] == ci)
            reversed = true;
    }
    int k = reversed ? n - 1 - slot : slot;
    return k < n ? sorted[k] : 0;
}

// A car's crew: its own last stop's, its team-mate's, or the garage order's. under g_mu
uintptr_t crew_of_car(uint32_t car)
{
    if (uintptr_t i = g_inst_of[car].load())
        return i;
    auto lc = swofty::live::car(car);
    auto g = lc ? swofty::pitlane::garage(*lc) : std::nullopt;
    if (!g)
        return 0;
    int slot = int(*g) / 2;
    for (uint32_t c = 0; c < kCars; ++c) {
        if (c == car)
            continue;
        auto lo = swofty::live::car(c);
        auto go = lo ? swofty::pitlane::garage(*lo) : std::nullopt;
        if (go && int(*go) / 2 == slot)
            if (uintptr_t i = g_inst_of[c].load())
                return i;
    }
    return crew_of_slot(slot);
}
Job g_jobs[16];
std::mutex g_mu;
std::atomic<uint64_t> g_box3_at[kCars]; // when each car began entering its box (status 3)
std::atomic<float> g_extra[kCars];
std::atomic<bool> g_stuck_seq[kCars]; // a rear stuck nut sequence started on this stop (1.0.11: only then a retirement roll)      // seconds the stop was made longer (the stuck nut test allows for it)

float spread_rate()
{
    int sp = int(stop_spread);
    if (sp <= 0)
        return 1.0f;
    // 0.2.20 used a peaked spread: in testing 5 stops came out x0.93-x1.03 (2.4-2.6 s), too alike. Now even across
    // +-spread, and one stop in six is fumbled a little: 1-2.5 x spread slower on top (at 15 %: 2.8-3.3 s for 2.4 s).
    float x = float(rnd() % 2001) / 1000.0f - 1.0f; // -1..1, even
    float f = float(sp) / 100.0f;
    float k = 1.0f + x * f; // the stop takes k times as long
    if (rnd() % 6 == 0)
        k += f * (1.0f + float(rnd() % 1501) / 1000.0f);
    return 1.0f / std::fmax(0.5f, k);
}
} // namespace crew

// A car handed to Terminal Damage: hold it in its box until Terminal Damage's push takes it, or retire it the game's
// way if Terminal Damage doesn't react. Game thread.
void td_tick(uintptr_t m, uint32_t car, float dt)
{
    int ph = g_td[car].load();
    uint32_t status = g_status[car].load();
    uintptr_t rec = 0;
    float timer = 0, limit = 0;
    if (swofty::mem::read(m + kVehicles + car * kStride, rec) && rec && swofty::mem::read(rec + kTimer, timer) &&
        swofty::mem::read(rec + kLimit, limit)) {
        if ((status == kInPitBox || status == kCrewDone) && timer > limit - kMargin)
            write_float(rec + kTimer, std::fmax(0.0f, limit - kMargin));
        if (status == kCrewDone)
            write_float(rec + kReleaseTimer, 0.0f);
    }
    if (ph == td_fallback)
        return;
    g_td_t[car] += dt;
    uint32_t td = td_state();
    if (ph == td_waiting) {
        if (td == TD_LIMPING || td == TD_RETIRING) {
            g_td[car].store(td_handed);
            g_td_t[car] = 0;
            void *sys = g_pit_system.load();
            swofty::log("[immersive-pitstops] car %u: Terminal Damage rates the car broken (state %u); back to its box "
                        "state for the push into the garage", car, td);
            if (td == TD_LIMPING && sys && orig_set_status)
                orig_set_status(sys, uint8_t(car), kInPitBox);
            return;
        }
        if (g_td_t[car] < kTdBroken)
            return;
        swofty::log("[immersive-pitstops] car %u: Terminal Damage didn't rate the car broken in %.0f s (state %u); the "
                    "game's own retirement", car, kTdBroken, td);
    } else { // td_handed
        if (td == TD_RETIRING || td == TD_IN_GARAGE || td == TD_RETIRED) {
            if (td != g_td_seen[car].exchange(td))
                swofty::log("[immersive-pitstops] car %u: Terminal Damage state %u", car, td);
            return;
        }
        if (g_td_t[car] < kTdPush)
            return;
        swofty::log("[immersive-pitstops] car %u: Terminal Damage didn't start its push in %.0f s (state %u); the game's "
                    "own retirement", car, kTdPush, td);
    }
    g_td[car].store(td_fallback);
    if (!post_retired(car))
        swofty::log("[immersive-pitstops] car %u: retirement message could not be sent", car);
}

void hold_tick(float dt);

void update(void *mgr, float dt)
{
    uintptr_t m = reinterpret_cast<uintptr_t>(mgr);
    loose::game_tick(); // wheel tear-offs asked for on the flow thread
    hold_tick(dt);
    if (m && !g_off.load())
        if (auto me = swofty::live::player())
            call::tick(m, uint32_t(me->vehicle), dt);
    if (m && !g_off.load()) {
        int64_t player = -1;
        if (auto me = swofty::live::player())
            player = int64_t(me->vehicle);
        for (uint32_t car = 0; car < kCars; ++car) {
            if (g_td[car].load() != td_none) {
                td_tick(m, car, dt);
                continue;
            }
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
            // 1.0.11: only a stop whose crew played a rear stuck nut sequence (2026-11-23: an AI front wing change at
            // x0.51 crew speed ran 17.7 s, past the limit, and was retired as a stuck nut)
            if (!st.rolled && timer > limit - kMargin && st.in_box - crew::g_extra[car].load() > limit - kMargin &&
                crew::g_stuck_seq[car].load()) {
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
    std::string n = seq_name(reinterpret_cast<uintptr_t>(a2));
    if (n.empty())
        n = seq_name(reinterpret_cast<uintptr_t>(a3));
    // 0.2.24 test: with the call-out held back, the crew still played F1InGarageToReady (the run-out) ~8 s before the
    // car reached its box; something else calls them too. While your hold is on, your crew's run-out is held at its
    // first frame, in the garage. Your crew: the instance of your last StartJob, or, before your first stop, the one
    // starting its run-out while yours is the only car in the pit lane.
    static std::atomic<uint64_t> s_init_at{0};
    if (n.size() >= 13 && _strnicmp(n.c_str(), "pitinitialise", 13) == 0) { // the crews are set up: a new list
        uint64_t now = GetTickCount64();
        if (now - s_init_at.exchange(now) > 3000) {
            std::lock_guard<std::mutex> lk(crew::g_mu);
            crew::g_seen_n = 0;
        }
    }
    if (n.size() >= 16 && _strnicmp(n.c_str(), "f1ingaragedoidle", 16) == 0) {
        std::lock_guard<std::mutex> lk(crew::g_mu);
        bool have = false;
        for (int i = 0; i < crew::g_seen_n; ++i)
            have = have || crew::g_seen[i] == uintptr_t(inst);
        if (!have && crew::g_seen_n < 24)
            crew::g_seen[crew::g_seen_n++] = uintptr_t(inst);
    }
    if (hold::g_held.load() && n.size() >= 17 && _strnicmp(n.c_str(), "f1ingaragetoready", 17) == 0) {
        auto me = swofty::live::player();
        if (me && me->vehicle < kCars) {
            uint32_t p = me->vehicle;
            uintptr_t known = crew::g_inst_of[p].load();
            int slot = -1;
            if (auto lc = swofty::live::car(p))
                if (auto g = swofty::pitlane::garage(*lc))
                    slot = int(*g) / 2;
            for (uint32_t c = 0; !known && c < kCars && slot >= 0; ++c) { // your team-mate's last stop
                auto lc = swofty::live::car(c);
                auto g = lc ? swofty::pitlane::garage(*lc) : std::nullopt;
                if (c != p && g && int(*g) / 2 == slot)
                    known = crew::g_inst_of[c].load();
            }
            if (!known) {
                std::lock_guard<std::mutex> lk(crew::g_mu);
                known = crew::crew_of_slot(slot);
            }
            bool alone = true;
            for (uint32_t c = 0; c < kCars; ++c)
                if (c != p && (g_status[c].load() == 2 || g_status[c].load() == 3))
                    alone = false;
            bool mine = known ? known == uintptr_t(inst) : alone;
            if (mine) {
                std::lock_guard<std::mutex> lk(crew::g_mu);
                crew::Job *slot = nullptr;
                for (auto &j : crew::g_jobs)
                    if (j.inst == uintptr_t(inst))
                        slot = &j;
                for (auto &j : crew::g_jobs)
                    if (!slot && (j.inst == 0 || j.car < 0))
                        slot = &j;
                if (slot) {
                    *slot = crew::Job{uintptr_t(inst), int(p), 1.0f, 1e9f, true};
                    hold::g_garage_inst.store(uintptr_t(inst));
                    hold::g_garage_used.store(true);
                    // the wait in the box and the run-out: not a stuck wheel nut
                    crew::g_extra[p].store(float(int(uncalled_delay)) + 5.0f);
                }
            }
            swofty::log("[immersive-pitstops] car %u's crew start their run-out (%s)%s", p, n.c_str(),
                        mine ? ": held in the garage" : known ? ": another team's crew" : ": not sure it's yours (other cars in the lane), not held");
        }
    }
    if (n.size() >= 10 && _strnicmp(n.c_str(), "f1startjob", 10) == 0) { // a stop begins: which car, how fast
        int car = -1;
        uint64_t best = 0, now = GetTickCount64();
        {
            // 0.2.27: by the crew first (2026-11-23: your held crew's start came 0.6 s after car 16 began entering its
            // box, and was matched to car 16), then the car that most recently began entering its box
            std::lock_guard<std::mutex> lk(crew::g_mu);
            for (uint32_t c = 0; c < kCars && car < 0; ++c) {
                uint32_t st = g_status[c].load();
                if ((st == 3 || st == 4) && crew::crew_of_car(c) == uintptr_t(inst))
                    car = int(c);
            }
        }
        for (uint32_t c = 0; c < kCars && car < 0; ++c) {
            uint32_t st = g_status[c].load();
            uint64_t at = crew::g_box3_at[c].load();
            if ((st == 3 || st == 4) && at && at + 15000 > now && at > best)
                best = at;
        }
        for (uint32_t c = 0; c < kCars && car < 0 && best; ++c)
            if (crew::g_box3_at[c].load() == best)
                car = int(c);
        // 1.0.13: the plugin's own stuck wheel nut. The game's crew mistake chance never gave the addon control (no
        // mistakes in ~160 stops below 100 %), so at a normal stop's start the plugin rolls stuck_chance itself and, on
        // a hit, hands the game a rear wheel's "wheel on" sequence (the 60 s stuck nut in erp/) instead: the name
        // passed to PitStopInstance::playSequence is what it looks up (0x1403b3777 calls the lookup with it).
        bool ours = false;
        bool is_ferrari = car >= 0 && ferrari(uint32_t(car));
        int stuck_pc = is_ferrari ? kFerrariChance : int(stuck_chance);
        if (car >= 0 && stuck_pc > 0) {
            std::string lower = n, raw;
            for (char &c : lower)
                c = char(std::tolower(static_cast<unsigned char>(c)));
            const std::string stem = "f1startjobdostandardstop";
            bool normal = lower.compare(0, stem.size(), stem) == 0 &&
                          (lower.size() == stem.size() || lower.substr(stem.size()) == "258" ||
                           lower.substr(stem.size()) == "21");
            bool direct = swofty::mem::c_string(reinterpret_cast<uintptr_t>(a2), raw, 160) && raw == n;
            int roll = int(rnd() % 10000);
            if (is_ferrari)
                swofty::log("[immersive-pitstops] car %d (Ferrari Mode): %s, stuck nut roll %.2f against %d %%", car,
                            n.c_str(), roll / 100.0, stuck_pc);
            if (normal && direct && roll < stuck_pc * 100) {
                static const char kRL[] = "F1StartJobDoStandardStopRLON";
                static const char kRR[] = "F1StartJobDoStandardStopRRON";
                const char *pick = rnd() % 2 ? kRR : kRL;
                a2 = const_cast<char *>(pick);
                n = pick;
                ours = true;
                auto me = swofty::live::player();
                if (me && int(me->vehicle) == car)
                    swofty::hud::toast("STUCK WHEEL NUT", pick == kRR ? "Rear right: the crew are fighting it"
                                                                      : "Rear left: the crew are fighting it",
                                       6.0f);
            }
        }
        if (car >= 0) {
            auto me = swofty::live::player();
            bool mine = me && int(me->vehicle) == car;
            // uncalled: the crew's start is held at its first frame for the delay. With the garage hold (0.2.24) the crew
            // are still in the garage then (the pit lane brings the car into its box and starts the crew's sequence
            // without waiting for them: 0.2.23 test), so they stay there until the call-out goes with the unfreeze.
            // 0.2.28: no freeze when the crew were already held in the garage for this stop (0.2.27 froze them again at
            // the car: they ran out and stood still for the delay a second time)
            bool uncalled = mine && call::g_uncalled_stop.exchange(false);
            bool in_garage = mine && hold::g_garage_used.exchange(false);
            float freeze = uncalled && !in_garage ? float(int(uncalled_delay)) : 0.0f;
            float rate = crew::spread_rate();
            {
                std::lock_guard<std::mutex> lk(crew::g_mu);
                crew::Job *slot = nullptr;
                for (auto &j : crew::g_jobs)
                    if (j.inst == uintptr_t(inst))
                        slot = &j;
                for (auto &j : crew::g_jobs)
                    if (!slot && (j.inst == 0 || j.car < 0))
                        slot = &j;
                if (slot) {
                    *slot = crew::Job{uintptr_t(inst), car, rate, freeze};
                    // a stuck nut sequence ("...ON"): roll its length
                    std::string lower = n;
                    for (char &c : lower)
                        c = char(std::tolower(static_cast<unsigned char>(c)));
                    for (auto &w : crew::kWindows) {
                        // rear wheels only: the front wheels' files are the game's stock short ones (1.0.10+)
                        if (std::string(w.wheel) != "rlon" && std::string(w.wheel) != "rron")
                            continue;
                        if (lower.size() < 4 || lower.compare(lower.size() - 4, 4, w.wheel) != 0 ||
                            (lower.find("fwa") != std::string::npos) != w.fwa)
                            continue;
                        slot->stuck = true;
                        if (std::string(w.wheel) == "rlon" || std::string(w.wheel) == "rron")
                            crew::g_stuck_seq[car].store(true);
                        slot->s0 = float(w.b) / 30.0f;
                        slot->d = float(w.b - w.a) / 30.0f;
                        slot->end = float(w.b + w.extra) / 30.0f;
                        // ours: a rolled length; the game's own "rear wheel on" mistake: no pause at all, so it plays
                        // at its stock length (the rear files are always the 60 s ones)
                        slot->ours = ours;
                        slot->len = ours ? crew::stuck_length() : 0.0f;
                        if (ours)
                            swofty::log("[immersive-pitstops] car %d: stuck wheel nut (%s), %.0f s this time", car, n.c_str(),
                                        slot->len);
                        else
                            swofty::log("[immersive-pitstops] car %d: the game's own wheel-on mistake (%s): stock length",
                                        car, n.c_str());
                        break;
                    }
                }
                crew::g_inst_of[car].store(uintptr_t(inst));
            }
            crew::g_extra[car].store(std::fmax(crew::g_extra[car].load(), freeze + 2.6f * (1.0f / rate - 1.0f)));
            if (bool(pit_log) || freeze > 0)
                swofty::log("[immersive-pitstops] car %d's crew start (%s): speed x%.2f%s", car, n.c_str(), rate,
                            freeze > 0 ? ", held still first (not called)" : "");
        }
    }
    if (int corner = seq_corner(n); corner >= 0) { // a "wheel on" mistake: remember its wheel (0.2.15)
        SeqSeen &s = g_seq[g_seq_next.fetch_add(1) % 8];
        s.corner.store(corner);
        s.at.store(GetTickCount64());
    }
    if (bool(pit_log) && g_play_logged.load() < 400) {
        g_play_logged.fetch_add(1);
        swofty::log("[immersive-pitstops] crew sequence: %s (instance %p, args %p %p)", n.empty() ? "?" : n.c_str(), inst,
                    a2, a3);
    }
    return orig_play(inst, a2, a3, a4);
}

const swofty::detail::mixin reg_play{"Game_PitStopInstance_PlaySequence", SWOFTY_MIXIN_WRAP, 0, (void *)&play_seq,
                                     (void **)&orig_play};

void (*orig_job)(void *) = nullptr;

void job_update(void *job)
{
    uintptr_t j = reinterpret_cast<uintptr_t>(job), inst = 0;
    float dt = 0;
    if (j && swofty::mem::read(j + 8, inst) && inst && swofty::mem::read(j + 0x10, dt) && dt > 0.0f && dt < 0.5f) {
        int car = -1;
        float ndt = dt;
        bool ready_now = false;
        {
            std::lock_guard<std::mutex> lk(crew::g_mu);
            for (auto &jb : crew::g_jobs) {
                if (jb.inst != inst || jb.car < 0)
                    continue;
                uint32_t st = g_status[jb.car].load();
                if (st < (jb.garage ? 2u : 3u) || st > 5) { // the stop is over
                    jb.car = -1;
                    break;
                }
                car = jb.car;
                ndt = dt * jb.rate;
                if (jb.runout) {
                    // 1.0.1: started with the car already in its box, the game plays the run-out at a catch-up rate
                    // (PitStopInstance +0x3bc, the crew rate the update job scales the frame time by, per the
                    // launcher's research notes), and it looked sped up; dividing it out plays it at the normal pace
                    float r = 0;
                    if (swofty::mem::read(inst + 0x3bc, r) && r > 1.05f && r < 20.0f) {
                        ndt = dt / r;
                        static uint64_t s_logged = 0;
                        if (s_logged != inst) {
                            s_logged = inst;
                            swofty::log("[immersive-pitstops] car %d: the crew's run-out was at x%.2f; played at normal pace",
                                        jb.car, r);
                        }
                    }
                }
                if (jb.freeze > 0) {
                    jb.freeze -= dt;
                    ndt = 0.0f;
                    ready_now = jb.freeze <= 0;
                }
                if (jb.stuck && !jb.cut) {
                    // the crew's sequence clock: PitStopInstance +0x3b0 is the crew sequence instance, its time in
                    // seconds at +0x5c (the launcher's research notes on Sess_PitStopUpdateJob_Update). Checked sane
                    // before any write; otherwise the stuck nut plays its full minute.
                    uintptr_t seq = 0;
                    float t = 0;
                    if (!swofty::mem::read(inst + 0x3b0, seq) || !seq || !swofty::mem::read(seq + 0x5c, t) ||
                        !(t >= 0.0f && t < jb.end + 30.0f)) {
                        // the sequence is made fresh on each play (+0x3b0 is replaced): keep trying for a few seconds
                        if (++jb.tries > 300) {
                            jb.cut = true;
                            swofty::log("[immersive-pitstops] car %d: the crew's sequence clock doesn't read sanely; the "
                                        "stuck nut plays in full", jb.car);
                        }
                    } else if (t >= jb.s0 + jb.len) {
                        jb.cut = true;
                        if (t < jb.end - jb.d) {
                            float jump = std::floor((jb.end - t) / jb.d) * jb.d;
                            write_float(seq + 0x5c, t + jump);
                            swofty::log("[immersive-pitstops] car %d: the stuck nut comes free after %.1f s (sequence %.2f -> "
                                        "%.2f)", jb.car, t - jb.s0, t, t + jump);
                        }
                    }
                }
                break;
            }
        }
        if (car >= 0 && ndt != dt)
            write_float(j + 0x10, ndt);
        if (ready_now) {
            swofty::log("[immersive-pitstops] car %d: the crew are ready, the stop starts", car);
            if (hold::g_held.load())
                hold::g_release.store(true);
        }
    }
    orig_job(job);
}

const swofty::detail::mixin reg_job{"Sess_PitStopUpdateJob_Update", SWOFTY_MIXIN_WRAP, 0, (void *)&job_update,
                                    (void **)&orig_job};


uint64_t (*orig_post)(void *, void *) = nullptr;

uint64_t post(void *bus, void *msg)
{
    if (hold::g_vt && msg && !hold::g_reposting.load() && !g_off.load()) {
        const void *vt = nullptr;
        uint8_t car = 0xff;
        if (swofty::mem::read(reinterpret_cast<uintptr_t>(msg), vt) && vt == hold::g_vt &&
            swofty::mem::read(reinterpret_cast<uintptr_t>(msg) + 0x10, car) && car < kCars) {
            auto me = swofty::live::player();
            bool told = false;
            if (me && me->vehicle == car && !hold::g_held.load() && call::uncalled_now(car, &told)) {
                hold::g_t = 0;
                hold::g_stand = 0;
                hold::g_box = 0;
                hold::g_garage_inst.store(0);
                hold::g_garage_used.store(false);
                hold::g_car.store(int(car));
                hold::g_held.store(true);
                swofty::log("[immersive-pitstops] car %u entered the pit lane without calling: the crew stay in the garage",
                            car);
                return 0;
            }
        }
    }
    return orig_post(bus, msg);
}

const swofty::detail::mixin reg_post{"Ne_Post_BusMessage", SWOFTY_MIXIN_WRAP, 0, (void *)&post, (void **)&orig_post};

// Game thread: send the held call-out once you've waited.
void hold_tick(float dt)
{
    if (!hold::g_held.load())
        return;
    int car = hold::g_car.load();
    if (car < 0 || car >= int(kCars)) {
        hold::g_held.store(false);
        return;
    }
    uint32_t status = g_status[car].load();
    hold::g_t += dt;
    if (auto c = swofty::live::car(uint32_t(car)); c && swofty::live::has(*c, SWOFTY_LIVE_HAVE_SPEED) &&
                                                   std::fabs(c->speed) < 1.5f && (status == 2 || status == 3))
        hold::g_stand += dt;
    const char *why = nullptr;
    if (status == kInPitBox || status == kCrewDone)
        hold::g_box += dt;
    if (uintptr_t gi = hold::g_garage_inst.load(); gi && status == kInPitBox && hold::g_box >= float(int(uncalled_delay))) {
        // you have waited in the box: let the crew's run-out go, with the call-out below
        std::lock_guard<std::mutex> lk(crew::g_mu);
        for (auto &j : crew::g_jobs)
            if (j.inst == gi && j.garage)
                j.freeze = 0, j.garage = false, j.runout = true;
        hold::g_garage_inst.store(0);
        hold::g_release.store(true);
    }
    if (hold::g_release.exchange(false))
        why = "the crew's wait in the box is over";
    else if (uncalled_delay > 0 && status < kInPitBox && hold::g_stand >= float(int(uncalled_delay)))
        why = "you waited in the pit lane";
    else if (hold::g_t > 45.0f + float(int(uncalled_delay)))
        why = "time limit";
    else if (status < 2 && hold::g_t > 2.0f)
        why = "the car left the pit lane";
    if (!why)
        return;
    hold::g_held.store(false);
    hold::g_release.store(false);
    if (uintptr_t gi = hold::g_garage_inst.exchange(0)) { // the hold ends another way: let the run-out go
        std::lock_guard<std::mutex> lk(crew::g_mu);
        for (auto &j : crew::g_jobs)
            if (j.inst == gi && j.garage)
                j.freeze = 0, j.garage = false, j.runout = true;
    }
    uintptr_t holder = 0, bus = 0;
    struct {
        const void *vtable;
        uint64_t zero;
        uint8_t vehicle;
        uint8_t pad[0x2f];
    } msg{};
    msg.vtable = hold::g_vt;
    msg.vehicle = uint8_t(car);
    if (!g_post || !g_bus_holder || !swofty::mem::read(g_bus_holder, holder) || !holder ||
        !swofty::mem::read(holder + 8, bus) || !bus) {
        swofty::log("[immersive-pitstops] car %d: the crew's call-out could not be sent", car);
        return;
    }
    hold::g_reposting.store(true);
    g_post(reinterpret_cast<void *>(bus), &msg);
    hold::g_reposting.store(false);
    swofty::log("[immersive-pitstops] car %d: the crew are called out (%s; %.1f s in the pit lane, %.1f s standing)", car,
                why, hold::g_t, hold::g_stand);
}

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

bool call::uncalled_now(uint32_t car, bool *told_out)
{
    int ls = loose::g_state.load();
    bool told = loose::g_car.load() == int(car) && (ls == loose::loose || ls == loose::pitting);
    if (told_out)
        *told_out = told;
    bool called = g_called.load() || told;
    return !called && bool(call_stop) && swofty::speech::live() && race_session() && td_state() != TD_LIMPING &&
           td_state() != TD_RETIRING;
}

bool call::tick(uintptr_t, uint32_t car, float)
{
    if (car >= kCars)
        return false;
    uint32_t status = g_status[car].load();
    Approach &a = g_a;
    if (status != 2 && status != 3) { // 2 in the pit lane, 3 entering the box
        a = Approach{};
        return false;
    }
    if (!a.active) {
        a.active = true;
        bool told = false;
        bool uncalled = uncalled_now(car, &told);
        bool called = g_called.load() || told;
        g_uncalled_stop.store(uncalled);
        swofty::log("[immersive-pitstops] car %u in the pit lane: stop %s", car,
                    called ? (told ? "called (loose wheel)" : "called (Box This Lap)")
                           : (uncalled ? "NOT called: the crew aren't ready" : "not called (no delay here)"));
        if (uncalled)
            swofty::hud::toast("CREW NOT READY", "You didn't call your stop", 5.0f);
    }
    return true;
}

} // namespace

SWOFTY_ON_LOAD
{
    swofty::log("[immersive-pitstops] loaded 1.0.0: pit box hold %s, at most %d s; retirement %d %% of stuck wheel nuts "
                "(when the crew finish, the game's retirement); log %s", bool(hold_box) ? "on" : "off", int(max_hold),
                int(retire_chance), bool(pit_log) ? "on" : "off");
    find_status_request();
    hold::find();
    auto r = swofty::events::on<swofty::sessions::events::Pit>().after(
        [](const swofty::sessions::events::Pit &e, const swofty::events::Context &) {
            if (e.car >= kCars)
                return;
            if (g_td[e.car].load() != td_none) { // handed to Terminal Damage: only follow its status
                g_status[e.car].store(e.status);
                if (bool(pit_log))
                    swofty::log("[immersive-pitstops] car %u pit status %u -> %u (Terminal Damage's retirement)", e.car,
                                e.previous, e.status);
                return;
            }
            if (e.status == 3)
                crew::g_box3_at[e.car].store(GetTickCount64());
            if (e.status == 2) {
                crew::g_extra[e.car].store(0.0f);
                crew::g_stuck_seq[e.car].store(false);
            }
            if (e.status == kInPitBox && e.previous != kInPitBox) {
                g_box_at[e.car].store(GetTickCount64());
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
                if (auto me = swofty::live::player(); me && me->vehicle == e.car && call::g_called.exchange(false))
                    swofty::log("[immersive-pitstops] car %u: the called stop is done", e.car);
                auto me = swofty::live::player();
                if (me && me->vehicle == e.car) {
                    int ls = loose::g_state.load();
                    if (ls == loose::pitting) {
                        loose::g_state.store(loose::fixed);
                    } else if (ls == loose::none && !g_stops[e.car].rolled && !g_wants_retire[e.car].load() &&
                               !crew::g_stuck_seq[e.car].load()) { // 1.0.17: not after any stuck nut
                        int chance = ferrari(e.car) ? kFerrariChance : int(loose_chance), roll = int(rnd() % 100);
                        if (ferrari(e.car))
                            swofty::log("[immersive-pitstops] car %u (Ferrari Mode): loose wheel at %d %%", e.car, chance);
                        bool hit = roll < chance;
                        swofty::log("[immersive-pitstops] car %u: loose wheel roll %d against %d %%%s", e.car, roll, chance,
                                    hit ? ": the wheel isn't on properly" : "");
                        if (hit) {
                            loose::g_car.store(int(e.car));
                            loose::g_corner.store(loose::random_corner());
                            loose::g_state.store(loose::pending);
                        }
                    }
                } else if (!g_stops[e.car].rolled && !g_wants_retire[e.car].load() && !crew::g_stuck_seq[e.car].load() &&
                           ![&] { auto c = swofty::live::car(e.car); return c && swofty::pitlane::safety_car(*c); }()) {
                    // an AI car: a very rare wheel left loose comes off on its out-lap
                    float chance = ferrari(e.car) ? float(kFerrariChance) : float(ai_loose_chance);
                    float roll = float(rnd() % 10000) / 100.0f;
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
            g_td[i].store(td_none);
            g_td_seen[i].store(0);
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
        call::g_called.store(false);
        call::g_uncalled_stop.store(false);
        hold::g_held.store(false);
        {
            std::lock_guard<std::mutex> lk(crew::g_mu);
            for (auto &j : crew::g_jobs)
                j = crew::Job{};
        }
        for (uint32_t i = 0; i < kCars; ++i)
            crew::g_inst_of[i].store(0);
        hold::g_release.store(false);
    });
    // crossing the line on track with the wheel still loose: the stop was missed
    // Box This Lap: the engineer's confirmation
    auto sp = swofty::events::on<swofty::speech::events::Line>().after(
        [](const swofty::speech::events::Line &l, const swofty::events::Context &) {
            if (!swofty::speech::engineer(l) || !swofty::speech::stem(l).starts_with("eng_pit_conf"))
                return;
            if (!call::g_called.exchange(true)) {
                swofty::log("[immersive-pitstops] Box This Lap confirmed (%.*s): the crew get ready",
                            int(swofty::speech::stem(l).size()), swofty::speech::stem(l).data());
                if (bool(call_stop))
                    swofty::hud::toast("BOX CONFIRMED", "The crew are getting ready", 4.0f);
            }
        });
    swofty::log("[immersive-pitstops] crew speed per stop: %d %% spread; crew update job %s", int(stop_spread),
                swofty::api && swofty::api->resolve && swofty::api->resolve("Sess_PitStopUpdateJob_Update") ? "found"
                                                                                                          : "NOT FOUND");
    swofty::log("[immersive-pitstops] call your stop: %s; speech events %s", bool(call_stop) ? "on" : "off",
                sp == swofty::events::Result::ok && swofty::speech::live() ? "on" : "NOT AVAILABLE (every stop counts as called)");
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
            if (e.car.index() < kCars) {
                loose::g_rebuilt[e.car.index()].store(true);
                g_td[e.car.index()].store(td_none); // a restart: the car is racing again
                g_td_seen[e.car.index()].store(0);
            }
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

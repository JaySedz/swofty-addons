// Helmet Cam: feeds the visor pass (fx/visor.hlsl) once a frame with the head's sway and the team's strip row.
//
// Head sway: the camera car's longitudinal and lateral g from the launcher's live state drive a damped spring (a
// head on a neck); braking dips the view, acceleration lifts it, cornering rolls it and shifts it a little towards
// the corner. The pass moves the picture by that amount; nothing in the game's camera changes.
// Team strip: the camera car's team badge picks a row of tex/strips.png.
//
// Fallback: without the live state (older core, unverified game build, no session), the sway goes back to rest and
// the generic strip shows; without postfx data (older core) the pass draws a still visor.
//
// 0.1.2 camera probe (stage 2 groundwork, log only, writes nothing): wraps the game's free-look camera modifier update
// (launcher binding Game_FreeLookCameraModifier_Update) to learn whether it runs every frame in the cockpit view and
// what its head yaw / pitch fields hold. The wrap calls the game's update first and passes its arguments and result
// straight through; it only notes which modifier ran. The fields are read on the frame thread with guarded reads.
// Where the binding doesn't resolve, the core doesn't install the wrap and nothing changes.
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <optional>
#include <cmath>
#include <cstdint>
#include <string>

#include <swofty/events.hpp>
#include <swofty/live.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>
#include <swofty/postfx.hpp>
#include <swofty/telemetry.hpp>

namespace {

constexpr const char *kId = "sedz.helmet-cam";
constexpr const char *kPass = "visor";
constexpr const char *kVersion = "0.1.8";

SWOFTY_SETTING(int, sway_brake, 50);  // 0.1.3: braking / acceleration dip, apart from cornering
SWOFTY_SETTING(int, sway_corner, 70); // 0.1.3: cornering roll and look
SWOFTY_SETTING(bool, verbose, false);
SWOFTY_SETTING(std::string_view, rain_mode, "auto"); // 0.1.7: visor rain from the weather, off, or always (to try it)
SWOFTY_SETTING(int, rain_amount, 70);
SWOFTY_SETTING(bool, camera_probe, true);
SWOFTY_SETTING(int, head_turn_amount, 10); // 0.1.5: the look into corners through the game's camera, 0 = off

// ---- camera probe and head turn ------------------------------------------------------------------------------
// 0.1.2's probe found the free-look modifier updates every frame in the cockpit view, with the look input axes at
// +0x30 (yaw) / +0x34 (pitch), -1 to 1, and the head offsets it builds from them at +0x4c (yaw, about +-5.6 at a
// held look) / +0x48 (pitch), easing back to 0 when the input stops. 0.1.3's head turn (off by default) writes the
// look into the corner to +0x4c before and after the game's update, only while the player isn't looking around.
namespace probe {

constexpr const char *kFreeLook = "Game_FreeLookCameraModifier_Update";
constexpr uintptr_t kAxisYaw = 0x30, kAxisPitch = 0x34, kOffYaw = 0x4c;
std::atomic<uintptr_t> g_last{0};   // the modifier the last call updated
std::atomic<uint32_t> g_calls{0};   // calls since the last log line
std::atomic<uint32_t> g_total{0};
std::atomic<bool> g_turn_on{false}; // set by the frame thread: write the head turn
std::atomic<float> g_turn{0.f};     // the head turn to write (the game's +0x4c units)
std::atomic<float> g_game_left{0.f}; // +0x4c as the game's update left it (before our post-write)
std::atomic<uint32_t> g_writes{0}, g_user{0};

// guarded single-float access (no C++ objects here: __try)
bool get_f(uintptr_t at, float &out)
{
    __try {
        out = *reinterpret_cast<volatile float *>(at);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool put_f(uintptr_t at, float v)
{
    __try {
        *reinterpret_cast<volatile float *>(at) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uint64_t (*orig)(void *, void *, void *, void *) = nullptr;
// the update's own arguments and result pass straight through (its prototype isn't in the launcher's bindings).
// With the head turn off this is the game's update and two counters, as in 0.1.2.
uint64_t wrap(void *self, void *a, void *b, void *c)
{
    uintptr_t s = reinterpret_cast<uintptr_t>(self);
    bool turn = s && g_turn_on.load(std::memory_order_relaxed);
    bool looking = false;
    float v = 0.f;
    if (turn) {
        float ay = 0.f, ap = 0.f;
        looking = !get_f(s + kAxisYaw, ay) || !get_f(s + kAxisPitch, ap) || std::fabs(ay) > 0.02f || std::fabs(ap) > 0.02f;
        v = g_turn.load(std::memory_order_relaxed);
        if (!looking)
            put_f(s + kOffYaw, v); // before: the update may build on it
    }
    uint64_t r = orig(self, a, b, c);
    if (turn && !looking) {
        float left = 0.f;
        if (get_f(s + kOffYaw, left))
            g_game_left.store(left, std::memory_order_relaxed);
        if (put_f(s + kOffYaw, v)) // after: whatever reads it later this frame
            g_writes.fetch_add(1, std::memory_order_relaxed);
    } else if (turn) {
        g_user.fetch_add(1, std::memory_order_relaxed); // the player's own look wins
    }
    g_last.store(s, std::memory_order_relaxed);
    g_calls.fetch_add(1, std::memory_order_relaxed);
    g_total.fetch_add(1, std::memory_order_relaxed);
    return r;
}
const swofty::detail::mixin reg{kFreeLook, SWOFTY_MIXIN_WRAP, 0, (void *)&wrap, (void **)&orig};

// the modifiers seen (a new one = a new camera); the last 8
uintptr_t g_seen[8] = {};
int g_seen_n = 0;
double g_wait = 0, g_age = 0;
float g_prev[24] = {};

bool remember(uintptr_t p)
{
    for (int i = 0; i < g_seen_n; i++)
        if (g_seen[i] == p)
            return false;
    if (g_seen_n < 8)
        g_seen[g_seen_n++] = p;
    else
        g_seen[std::rand() % 8] = p;
    return true;
}

// the head turn: the eyes into the corner, through the game's free-look yaw (+0x4c). Units are the game's (a held
// full look builds about 5.6); +0x4c follows +g_lat (0.1.3 had it reversed).
// 0.1.5: driven only by sustained cornering load. 0.1.4 also used the steering and the instant lateral g, so every
// correction and move across the track swung the head (too aggressive). Now: lateral g smoothed over ~0.5 s, a 1 g
// deadzone (lane changes, kerbs and straight-line wobbles stay under it), then a gentle slope and a low cap.
float g_turn_x = 0.f, g_turn_v = 0.f, g_lat_f = 0.f;
bool g_turn_logged = false;
double g_turn_wait = 0;

void head_turn(double dt, float g_lat, bool have_car)
{
    float amount = std::clamp(float(int(head_turn_amount)) / 100.f, 0.f, 1.f);
    bool on = amount > 0.f && have_car;
    float t = float(std::min(dt, 0.1));
    if (t > 0.f)
        g_lat_f += (g_lat - g_lat_f) * (1.f - std::exp(-t / 0.5f));
    if (!have_car)
        g_lat_f = 0.f;
    float load = std::fabs(g_lat_f) - 1.0f; // g past the deadzone
    float target = on && load > 0.f ? std::copysign(std::min(load * 0.8f, 2.5f), g_lat_f) * amount : 0.f;
    if (t > 0.f) {
        float w = 6.2831853f * 0.7f; // slow and smooth: a head settling into the corner, not snapping
        g_turn_v += (w * w * (target - g_turn_x) - 2.f * 1.0f * w * g_turn_v) * t;
        g_turn_x += g_turn_v * t;
        if (!std::isfinite(g_turn_x) || !std::isfinite(g_turn_v))
            g_turn_x = g_turn_v = 0.f;
    }
    // switched off: keep writing while the head eases back to the centre, then leave the field to the game
    bool write = on || std::fabs(g_turn_x) > 0.01f;
    if (!write)
        g_turn_x = g_turn_v = 0.f;
    g_turn.store(g_turn_x, std::memory_order_relaxed);
    g_turn_on.store(write, std::memory_order_relaxed);
    if (on != g_turn_logged) {
        g_turn_logged = on;
        swofty::log("[helmet-cam] head turn: %s", on ? "on (the look into corners goes to the game's free-look camera)"
                                                      : "off (the game's own camera)");
    }
    g_turn_wait -= dt;
    if (on && bool(verbose) && g_turn_wait <= 0) {
        g_turn_wait = 1.0;
        uint32_t w = g_writes.exchange(0), u = g_user.exchange(0);
        swofty::log("[helmet-cam] head turn: lat %.2f smoothed %.2f -> target %.2f now %.2f, %u write(s)/s, %u frame(s) "
                    "left to your own look; the game's update left %.2f", g_lat, g_lat_f, target, g_turn_x, w, u,
                    g_game_left.load());
    }
}

// once a second for the first 10 minutes of calls, then every 10 s: the call rate and the fields
void frame(double dt, float steer, float g_lat)
{
    if (!bool(camera_probe))
        return;
    g_wait -= dt;
    if (g_wait > 0)
        return;
    uint32_t calls = g_calls.exchange(0);
    uintptr_t self = g_last.load();
    if (!calls) {
        g_wait = 1.0;
        return;
    }
    g_age += 1.0;
    g_wait = g_age < 600 ? 1.0 : 10.0;
    if (self && remember(self))
        swofty::log("[helmet-cam] probe: a free-look modifier at %p (new; %d seen)", (void *)self, g_seen_n);
    // +0x28..+0x87 as floats: the notes put yaw +0x30, pitch +0x34, previous +0x38/+0x3c, offsets +0x48/+0x4c
    float f[24] = {};
    if (!self || !swofty::mem::read(self + 0x28, f)) {
        swofty::log("[helmet-cam] probe: %u call(s), modifier %p can't be read", calls, (void *)self);
        return;
    }
    char changed[25] = {};
    for (int i = 0; i < 24; i++)
        changed[i] = f[i] != g_prev[i] ? '*' : '.';
    std::copy(std::begin(f), std::end(f), std::begin(g_prev));
    swofty::log("[helmet-cam] probe: %u call(s)/s, steer %.2f lat %.2f | yaw %.4f pitch %.4f prev %.4f %.4f off %.4f %.4f | "
                "changed +28..+84 %s",
                calls, steer, g_lat, f[2], f[3], f[4], f[5], f[8], f[9], changed);
    if (bool(verbose))
        swofty::log("[helmet-cam] probe: +28 %g %g %g %g | +38 %g %g %g %g | +48 %g %g %g %g | +58 %g %g %g %g | +68 %g %g %g %g | "
                    "+78 %g %g %g %g",
                    f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11], f[12], f[13], f[14], f[15], f[16],
                    f[17], f[18], f[19], f[20], f[21], f[22], f[23]);
}

} // namespace probe

// The data the pass reads (fx/visor.hlsl): 32 bytes.
struct VisorData {
    float shift_x, shift_y; // picture heights: + = the camera looks right / down
    float roll;             // radians, + = the picture turns clockwise
    float speed_kmh;
    uint32_t strip_row;     // 0 = generic
    float rpm;              // 0 to 1
    float rain;             // 0.1.7: visor rain, 0 to 1
    float pad;
};
static_assert(sizeof(VisorData) == 32);

// Team badge key (as the live state names it, such as "mercedes_26") -> row of tex/strips.png. First match wins.
struct Row {
    const char *key;
    uint32_t row;
};
constexpr Row kRows[] = {
    {"ferrari", 1},  {"mercedes", 2}, {"redbull", 3},     {"red_bull", 3},    {"mclaren", 4},  {"forceindia", 5},
    {"force_india", 5}, {"aston", 5}, {"renault", 6},     {"alpine", 6},      {"williams", 7}, {"haas", 8},
    {"sauber", 9},   {"audi", 9},     {"tororosso", 10},  {"toro_rosso", 10}, {"racingbulls", 10},
    {"cadillac", 11}, {"konnersport", 12},
};

uint32_t row_for(const std::string &badge)
{
    std::string k = badge;
    std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    for (const Row &r : kRows)
        if (k.find(r.key) != std::string::npos)
            return r.row;
    return 0;
}

// A head on a neck: a damped spring per axis.
struct Spring {
    float x = 0, v = 0;
    void step(float target, float dt, float hz, float damping)
    {
        float w = 6.2831853f * hz;
        float a = w * w * (target - x) - 2.f * damping * w * v;
        v += a * dt;
        x += v * dt;
        if (!std::isfinite(x) || !std::isfinite(v))
            x = v = 0;
    }
};

struct State {
    Spring pitch, side, roll;
    int64_t vehicle = -1;
    uint32_t row = 0;
    double row_wait = 0;
    double log_wait = 0;
    int last_result = -100;
    bool live_logged = false;
    float rain = 0.f;        // 0.1.7: the visor's wetness, eased
    float rain_target = 0.f;
    double rain_wait = 0;
    int weather = -1;
} G;

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// 0.1.7: how wet the visor gets, from the game's weather (its UDP telemetry, as Driver Display reads it): light rain
// about half, heavy rain most, a storm all. Without telemetry (the game's UDP output off) "auto" stays dry; "always"
// rains regardless, to try it. Rain arrives over a few seconds and dries off slower.
float visor_rain(double dt)
{
    G.rain_wait -= dt;
    if (G.rain_wait <= 0) {
        G.rain_wait = 0.5;
        std::string_view mode = rain_mode;
        float target = 0.f;
        int weather = -1;
        if (mode == "always") {
            target = 1.f;
        } else if (mode != "off" && swofty::telemetry::available()) {
            swofty::telemetry::SessionState s{};
            s.version = swofty::telemetry::version;
            if (swofty::telemetry::session(&s) == swofty::telemetry::Result::ok && s.udp_format) {
                weather = int(s.weather);
                target = weather == 3 ? 0.5f : weather == 4 ? 0.8f : weather >= 5 ? 1.f : 0.f;
            }
        }
        if (weather != G.weather) {
            G.weather = weather;
            swofty::log("[helmet-cam] visor rain: weather %d (%s) -> %.0f %%", weather,
                        weather < 0 ? "no telemetry" : weather >= 3 ? "rain" : "dry", target * 100.f);
        }
        G.rain_target = target;
    }
    float tau = G.rain_target > G.rain ? 4.f : 15.f;
    float t = float(std::min(dt, 0.1));
    G.rain += (G.rain_target - G.rain) * (1.f - std::exp(-t / tau));
    return clampf(G.rain * float(int(rain_amount)) / 100.f, 0.f, 1.f);
}

void on_frame(double dt_d, void *)
{
    float dt = float(dt_d);
    if (!(dt > 0.f) || dt > 0.25f) // a hitch or a long pause: start from rest rather than jump
        dt = 0.f;

    namespace live = swofty::live;
    std::optional<live::Car> car = live::available() ? live::subject() : std::nullopt;
    if (!car)
        car = live::available() ? live::player() : std::nullopt;

    float brake_amount = clampf(float(int(sway_brake)) / 100.f, 0.f, 1.f);
    float corner_amount = clampf(float(int(sway_corner)) / 100.f, 0.f, 1.f);
    float g_long = 0, g_lat = 0, steer = 0, speed = 0, rpm = 0;
    if (car) {
        if (live::has(*car, SWOFTY_LIVE_HAVE_G)) {
            g_long = clampf(car->g_long, -6.f, 6.f);
            g_lat = clampf(car->g_lat, -6.f, 6.f);
        }
        if (live::has(*car, SWOFTY_LIVE_HAVE_STEER))
            steer = clampf(car->steer, -1.f, 1.f);
        if (live::has(*car, SWOFTY_LIVE_HAVE_SPEED))
            speed = car->speed * 3.6f;
        if (live::has(*car, SWOFTY_LIVE_HAVE_RPM) && car->rpm_limit > 0.f)
            rpm = clampf(car->rpm / car->rpm_limit, 0.f, 1.f);
        if (!G.live_logged) {
            G.live_logged = true;
            swofty::log("[helmet-cam] live state: camera car %u (g %s)", car->vehicle,
                        live::has(*car, SWOFTY_LIVE_HAVE_G) ? "yes" : "no, the view stays still");
        }

        // the team's strip: on a new car at once, then every 2 s (a car change mid-session, a career team change)
        G.row_wait -= dt_d;
        if (int64_t(car->vehicle) != G.vehicle || G.row_wait <= 0) {
            G.row_wait = 2.0;
            auto badge = live::team_badge(car->vehicle);
            uint32_t row = badge ? row_for(*badge) : 0;
            if (int64_t(car->vehicle) != G.vehicle || row != G.row)
                swofty::log("[helmet-cam] camera car %u, team badge \"%s\": strip row %u", car->vehicle,
                            badge ? badge->c_str() : "", row);
            G.vehicle = int64_t(car->vehicle);
            G.row = row;
        }
    } else if (G.vehicle != -1) {
        G.vehicle = -1;
        G.row = 0;
    }

    // targets (picture heights / radians) at full sway. Signs from 0.1.0's log (Melbourne T1, a right-hander: steer
    // +0.43, g_lat -2.02; braking: g_long -3.5):
    // - braking throws the head forward and the view dips; acceleration lifts it
    // - the eyes look into the corner (towards the apex: right in a right-hander), led a touch by the steering
    // - the load pushes the head to the outside of the corner, so the horizon tilts against the turn
    // 0.1.1: about 3x 0.1.0's amounts, which moved the view only 1.5 % of the screen at 3.5 g.
    // 0.1.3: braking and cornering have their own sliders; braking at 100 is 3/4 of 0.1.2's 100 (too strong there).
    float t_pitch = car ? -g_long * 0.009f * brake_amount : 0.f;
    // 0.1.5: no steering term (small corrections twitched the view); the real head turn does the looking
    float t_side = car ? -g_lat * 0.004f * corner_amount : 0.f;
    float t_roll = car ? -g_lat * 0.014f * corner_amount : 0.f;
    if (dt > 0.f) {
        G.pitch.step(t_pitch, dt, 1.8f, 0.45f);
        G.side.step(t_side, dt, 1.1f, 0.70f);
        G.roll.step(t_roll, dt, 1.5f, 0.50f);
    }

    VisorData d{};
    d.shift_x = clampf(G.side.x, -0.07f, 0.07f);
    d.shift_y = clampf(G.pitch.x, -0.07f, 0.07f);
    d.roll = clampf(G.roll.x, -0.09f, 0.09f);
    d.speed_kmh = speed;
    d.strip_row = G.row;
    d.rpm = rpm;
    d.rain = visor_rain(dt_d);
    auto r = swofty::postfx::set_data(kId, kPass, d);
    if (int(r) != G.last_result) {
        G.last_result = int(r);
        const char *why = r == swofty::postfx::Result::ok            ? "the visor pass reads the head and strip data"
                          : r == swofty::postfx::Result::no_pass     ? "the visor pass isn't running now (off, another view, or not ready)"
                          : r == swofty::postfx::Result::unavailable ? "this core has no postfx data: the visor stays still"
                                                                      : "the visor pass refused the data";
        swofty::log("[helmet-cam] postfx data: %s", why);
    }

    if (bool(verbose) && car) {
        G.log_wait -= dt_d;
        if (G.log_wait <= 0) {
            G.log_wait = 1.0;
            swofty::log("[helmet-cam] g long %.2f lat %.2f steer %.2f speed %.0f -> shift %.4f %.4f roll %.4f", g_long, g_lat,
                        steer, speed, d.shift_x, d.shift_y, d.roll);
        }
    }

    probe::head_turn(dt_d, g_lat, car.has_value() && live::has(*car, SWOFTY_LIVE_HAVE_G));
    probe::frame(dt_d, steer, g_lat);
}

} // namespace

SWOFTY_ON_LOAD
{
    auto r = swofty::events::on_frame(on_frame);
    swofty::log("[helmet-cam] loaded %s: live state %s, postfx data %s, frame tick %s", kVersion,
                swofty::live::available() ? "available" : "not available (yet)",
                swofty::postfx::available() ? "available" : "not on this core (still visor)",
                r == swofty::events::Result::ok ? "on" : "not available (still visor)");
    uintptr_t at = swofty::api && swofty::api->resolve ? swofty::api->resolve(probe::kFreeLook) : 0;
    swofty::log("[helmet-cam] probe: %s %s", probe::kFreeLook,
                !bool(camera_probe) ? "not watched (camera probe off)"
                : at                ? "resolves: its calls are counted and its fields logged (nothing is changed)"
                                    : "doesn't resolve on this build: no probe");
}

SWOFTY_ADDON()

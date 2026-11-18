// Values from the game's telemetry packets for the player's car: brake bias, fuel mix, differential, tyre wear and
// the game's own last lap time. udp_tap.hpp fills them from the packets the game sends; without the game's UDP
// telemetry on they stay empty (fresh() is false) and the dash shows "-" there.
#pragma once
#include <windows.h>

#include <atomic>
#include <cstdint>

namespace udp_tap {

struct Values {
    std::atomic<uint32_t> packets{0};
    std::atomic<uint64_t> status_tick{0}, damage_tick{0}, setup_tick{0}, lap_tick{0}, tel_tick{0}; // GetTickCount64 of the last
    std::atomic<int> mix{-1}, bbal{-1}, diff{-1}, ers_mode{-1};
    std::atomic<int> pit_limiter{-1}; // car status pitLimiterStatus: 0 off, 1 on
    std::atomic<int> fia_flag{0}; // the flag the game shows this car: -1 unknown, 0 none, 1 green, 2 blue, 3 yellow
    std::atomic<float> fuel_laps{-999.f};
    std::atomic<float> wear[4] = {{-1.f}, {-1.f}, {-1.f}, {-1.f}}; // FL FR RL RR %
    std::atomic<int> tyre_inner[4] = {{-1}, {-1}, {-1}, {-1}};      // FL FR RL RR deg C (carcass, as the game shows)
    std::atomic<int> brake_temp[4] = {{-1}, {-1}, {-1}, {-1}};      // FL FR RL RR deg C
    std::atomic<int> dmg_fwl{0}, dmg_fwr{0}, dmg_rw{0}, dmg_floor{0}, dmg_diffuser{0}, dmg_sidepod{0};
    std::atomic<int> dmg_gearbox{0}, dmg_engine{0}, wear_ice{0}; // %
    std::atomic<int> dmg_brake[4] = {{0}, {0}, {0}, {0}}; // FL FR RL RR %
    // presses of "UDP Action 1".."12" (the game's controls; any device): counted when a button goes down
    std::atomic<uint32_t> action_presses[12] = {};
    std::atomic<uint32_t> last_lap_ms{0};
    // lap data: the game's own clock for the lap in progress (it rewinds with a flashback, stops in pauses and
    // replays), the distance round the lap (negative before the line on a race's opening lap), the lap number
    // (1 on the first) and whether the game has invalidated the lap
    std::atomic<uint32_t> cur_lap_ms{0};
    std::atomic<float> lap_dist{-1.f};
    std::atomic<int> lap_num{0};
    std::atomic<bool> lap_invalid{false};
    std::atomic<int> format{0}, player{-1};
    std::atomic<int> total_laps{0}; // the session's laps (session packet), 0 when not a race
    // session packet: the pit window (ideal and latest lap, 0 when none), the position the car would rejoin in if
    // it pitted now (the MFD's pit panel), the session type, the weather now and in the forecast sample 10 min on
    std::atomic<uint64_t> session_tick{0};
    std::atomic<int> session_size{0}, session_type{-1};
    std::atomic<int> pit_ideal{0}, pit_latest{0}, pit_rejoin{0};
    std::atomic<int> weather{-1}, weather10{-1}, rain10{-1}, forecast_min{-1};
    // lap data: pit stops, penalty seconds, warnings, track limit warnings, unserved drive throughs / stop-gos, the
    // result status (2 running, 3 finished), the pit lane timers (the last stop's time in the lane and stationary)
    std::atomic<int> stops{-1}, pen_s{-1}, warns{-1}, tl_warns{-1}, unserved_dt{-1}, unserved_sg{-1};
    std::atomic<int> result{-1}, pit_status{-1}, lane_active{0}, lane_ms{0}, stop_ms{0};
    // car status: the tyre on the car (visual compound: 16 soft, 17 medium, 18 hard, 7 inter, 8 wet) and its age
    std::atomic<int> tyre_visual{-1}, tyre_age{-1};
    // events for the player's car: a penalty (type, infringement, seconds or places), and the chequered flag
    std::atomic<uint32_t> pen_seq{0}, chqf_seq{0};
    std::atomic<int> pen_type{-1}, pen_infr{-1}, pen_time{0};
    // DRS (2025 rules): open (car telemetry), allowed here and the distance to the next zone (car status)
    std::atomic<int> drs_open{-1}, drs_allowed{-1}, drs_distance{-1};
    // 2026 season pack, packet 16 ("car telemetry 2"): overtake available / active, the 2026 rules flag (as Wheel
    // Dash read them), and the player car's first 16 bytes for mapping the rest (straight mode)
    std::atomic<uint64_t> t2_tick{0};
    std::atomic<int> ot_available{-1}, ot_active{-1}, regs2026{-1};
    // straight mode (2026), mapped from a 0.4.0 log: active @0, available @1, metres to the next zone u16 @2
    std::atomic<int> st_active{-1}, st_available{-1}, st_distance{-1};
    std::atomic<uint64_t> t2_raw[2] = {}; // bytes 0..15, little-endian
    std::atomic<int> t2_size{0};
    std::atomic<bool> from_launcher{false}; // the values stand in from the launcher's telemetry (no part damage)
};
inline Values g;

// fresh: a packet of that kind arrived in the last 3 s
inline bool fresh(const std::atomic<uint64_t> &tick)
{
    uint64_t t = tick.load();
    return t && GetTickCount64() - t < 3000;
}

} // namespace udp_tap

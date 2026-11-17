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
    std::atomic<uint64_t> status_tick{0}, damage_tick{0}, setup_tick{0}, lap_tick{0}; // GetTickCount64 of the last
    std::atomic<int> mix{-1}, bbal{-1}, diff{-1}, ers_mode{-1};
    std::atomic<float> fuel_laps{-999.f};
    std::atomic<float> wear[4] = {{-1.f}, {-1.f}, {-1.f}, {-1.f}}; // FL FR RL RR %
    std::atomic<int> dmg_fwl{0}, dmg_fwr{0}, dmg_rw{0}, dmg_floor{0}, dmg_diffuser{0}, dmg_sidepod{0};
    std::atomic<uint32_t> last_lap_ms{0};
    std::atomic<int> format{0}, player{-1};
};
inline Values g;

// fresh: a packet of that kind arrived in the last 3 s
inline bool fresh(const std::atomic<uint64_t> &tick)
{
    uint64_t t = tick.load();
    return t && GetTickCount64() - t < 3000;
}

} // namespace udp_tap

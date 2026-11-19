// The game's own UDP telemetry, read in the game as it is sent (no socket of this addon's). Code that touches
// network functions is reviewed by hand on the launcher (not auto-approved): that's expected for this addon.
//
// F1 25 hands each telemetry packet to ws2_32's sendto (or WSASendTo). The tap swaps the game module's import
// slots for those two functions for wrappers that read the packet and then call what the slot held before
// (ws2_32 itself, or another tap such as the launcher's), so the packet still goes out unchanged and every
// other telemetry app (SimHub, ...) still receives it. The slots are found by the functions' addresses, not by
// any game offset: they hold the address of ws2_32's sendto once Windows has loaded the game.
//
// The decoded values follow EA's F1 25 UDP specification (the 2026 season pack, packet format 2026, sends 24
// cars instead of 22; the per-car size is worked out from each packet's size). Only the player's car is read.
#pragma once
#include <winsock2.h>
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "packet_values.hpp"

namespace udp_tap {

// the decoded values live in packet_values.hpp

template <class T> inline T rd(const uint8_t *p)
{
    T v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

constexpr size_t kHeader = 29;

// Sector times for every car from the lap data (F1 25 spec: sector1TimeMSPart u16 @8 + minutes u8 @10,
// sector2TimeMSPart u16 @11 + minutes u8 @13, sector u8 @36 (0..2), currentLapNum @33, currentLapInvalid @37,
// lastLapTimeInMS @0). A sector is complete when a car's sector index steps on within a lap (S1, S2), and S3 when it
// starts its next lap from sector 3 (the last lap's time less S1 and S2). Flashbacks (a step back) only re-sync.
struct SectorState {
    bool init = false;
    int sector = 0, lap = 0, s1 = 0, s2 = 0;
    bool invalid = false;
};
inline SectorState g_sec_cars[24];
inline int g_sec_best[3] = {0, 0, 0}, g_sec_pb[3] = {0, 0, 0}; // ms, 0 none
inline uint64_t g_sec_uid = 0;

inline void sector_done(int car, int me, int k, int ms, bool invalid)
{
    if (ms <= 0 || ms > 600000)
        return;
    if (car == me) {
        int col = !invalid && (!g_sec_best[k] || ms < g_sec_best[k]) ? 2
                  : !invalid && (!g_sec_pb[k] || ms < g_sec_pb[k])   ? 1
                                                                      : 0;
        g.sector_ms[k] = ms;
        g.sector_col[k] = col;
        g.sector_seq++;
    }
    if (invalid)
        return;
    if (!g_sec_best[k] || ms < g_sec_best[k])
        g_sec_best[k] = ms;
    if (car == me && (!g_sec_pb[k] || ms < g_sec_pb[k]))
        g_sec_pb[k] = ms;
}

inline void sectors(const uint8_t *d, size_t per, int cars, int me)
{
    uint64_t uid = rd<uint64_t>(d + 7); // the header's sessionUID: a new session starts the bests again
    if (uid != g_sec_uid) {
        g_sec_uid = uid;
        for (auto &c : g_sec_cars) c = SectorState{};
        for (int k = 0; k < 3; k++) {
            g_sec_best[k] = g_sec_pb[k] = 0;
            g.sector_ms[k] = -1;
            g.sector_col[k] = 0;
        }
    }
    for (int i = 0; i < cars && i < 24; i++) {
        const uint8_t *c = d + kHeader + per * i;
        int sector = c[36], lap = c[33];
        int s1 = rd<uint16_t>(c + 8) + c[10] * 60000, s2 = rd<uint16_t>(c + 11) + c[13] * 60000;
        int last = int(rd<uint32_t>(c));
        bool invalid = c[37] != 0;
        SectorState &st = g_sec_cars[i];
        if (!st.init || lap <= 0 || sector > 2) {
            st = SectorState{true, sector, lap, 0, 0, invalid};
            continue;
        }
        if (lap == st.lap && sector == st.sector + 1) {
            if (sector == 1) { st.s1 = s1; sector_done(i, me, 0, s1, st.invalid || invalid); }
            if (sector == 2) { st.s2 = s2; sector_done(i, me, 1, s2, st.invalid || invalid); }
        } else if (lap == st.lap + 1 && st.sector == 2 && sector == 0) {
            if (st.s1 > 0 && st.s2 > 0 && last > st.s1 + st.s2)
                sector_done(i, me, 2, last - st.s1 - st.s2, st.invalid);
            st.s1 = st.s2 = 0;
            st.invalid = false;
        } else if (lap != st.lap) {
            st.s1 = st.s2 = 0;
            st.invalid = false;
        }
        st.sector = sector;
        st.lap = lap;
        st.invalid = st.invalid || invalid;
    }
}

inline void decode(const uint8_t *d, size_t n)
{
    if (n < kHeader)
        return;
    uint16_t format = rd<uint16_t>(d);
    if (format < 2024 || format > 2030)
        return;
    int id = d[6], me = d[27];
    int cars = format >= 2026 ? 24 : 22;
    if (me < 0 || me >= cars)
        return;
    g.packets++;
    g.format = format;
    if (uint64_t uid = rd<uint64_t>(d + 7); uid != g.session_uid) {
        g.session_uid = uid;
        g.session_type = -1;
        g.session_left = -1;
        g.session_uid_seq++;
    }
    g.player = me;
    uint64_t now = GetTickCount64();
    switch (id) {
    case 1: { // session: weather, track temperature, air temperature, then totalLaps
        if (n > kHeader + 3)
            g.total_laps = d[kHeader + 3];
        // F1 25 spec: sessionType @6; numWeatherForecastSamples @126, the samples (8 bytes: sessionType,
        // timeOffset min, weather, track temp, its change, air temp, its change, rain %) @127 (64 of them);
        // pitStopWindowIdealLap @653, pitStopWindowLatestLap @654, pitStopRejoinPosition @655
        if (n >= kHeader + 656) {
            const uint8_t *s = d + kHeader;
            int type = s[6];
            g.session_type = type;
            g.session_left = rd<uint16_t>(s + 9); // sessionTimeLeft
            g.weather = s[0];
            int count = s[126] <= 64 ? s[126] : 0;
            int w10 = -1, r10 = -1, at = -1;
            for (int i = 0; i < count; i++) {
                const uint8_t *f = s + 127 + 8 * i;
                if (f[0] == type && f[1] >= 10) { // the first sample 10 min or more ahead
                    w10 = f[2];
                    r10 = f[7];
                    at = f[1];
                    break;
                }
            }
            g.weather10 = w10;
            g.rain10 = r10;
            g.forecast_min = at;
            g.pit_ideal = s[653];
            g.pit_latest = s[654];
            g.pit_rejoin = s[655];
            g.session_size = int(n);
            g.session_tick = now;
        }
        break;
    }
    case 2: { // lap data; 2 bytes after the cars (time trial indices)
        size_t per = (n - kHeader - 2) / cars;
        if (per < 38)
            return;
        const uint8_t *c = d + kHeader + per * me;
        g.last_lap_ms = rd<uint32_t>(c);      // lastLapTimeInMS
        g.cur_lap_ms = rd<uint32_t>(c + 4);   // currentLapTimeInMS
        g.lap_dist = rd<float>(c + 20);       // lapDistance
        g.sc_delta = rd<float>(c + 28);       // safetyCarDelta
        g.lap_num = c[33];                    // currentLapNum
        g.lap_invalid = c[37] != 0;           // currentLapInvalid
        if (per >= 51) {
            g.pit_status = c[34];             // pitStatus: 0 none, 1 pitting, 2 in the pit area
            g.stops = c[35];                  // numPitStops
            g.pen_s = c[38];                  // penalties (seconds)
            g.warns = c[39];                  // totalWarnings
            g.tl_warns = c[40];               // cornerCuttingWarnings
            g.unserved_dt = c[41];            // numUnservedDriveThroughPens
            g.unserved_sg = c[42];            // numUnservedStopGoPens
            g.result = c[45];                 // resultStatus: 2 active, 3 finished, ...
            g.lane_active = c[46];            // pitLaneTimerActive
            g.lane_ms = rd<uint16_t>(c + 47); // pitLaneTimeInLaneInMS
            g.stop_ms = rd<uint16_t>(c + 49); // pitStopTimerInMS
        }
        if (per >= 38)
            sectors(d, per, cars, me);
        g.lap_tick = now;
        break;
    }
    case 3: { // event: a 4-letter code; "BUTN" carries the buttons held down (u32), UDP Action 1..12 from bit 20;
              // "PENA": penaltyType, infringementType, vehicleIdx, otherVehicleIdx, time, lapNum, placesGained;
              // "CHQF": the chequered flag
        if (n >= kHeader + 11 && std::memcmp(d + kHeader, "PENA", 4) == 0) {
            const uint8_t *e = d + kHeader + 4;
            if (e[2] == me) {
                g.pen_type = e[0];
                g.pen_infr = e[1];
                g.pen_time = e[4];
                g.pen_seq++;
            }
            return;
        }
        if (n >= kHeader + 4 && std::memcmp(d + kHeader, "CHQF", 4) == 0) {
            g.chqf_seq++;
            return;
        }
        if (n >= kHeader + 4 && std::memcmp(d + kHeader, "BUTN", 4) != 0) { // any other event: its code, for the log
            g.event_code = rd<uint32_t>(d + kHeader);
            g.event_seq++;
        }
        // "DRSE" (2025 content) or "OVEN" (overtake enabled, the 2026 pack: it sends OVEN at the start of lap 2,
        // not DRSE; seen in a 1.0.0 log)
        if (n >= kHeader + 4 && (std::memcmp(d + kHeader, "DRSE", 4) == 0 || std::memcmp(d + kHeader, "OVEN", 4) == 0)) {
            g.drse_seq++;
            return;
        }
        if (n >= kHeader + 5 && std::memcmp(d + kHeader, "STLG", 4) == 0) {
            g.lights = d[kHeader + 4];
            g.lights_seq++;
            return;
        }
        if (n >= kHeader + 4 && std::memcmp(d + kHeader, "LGOT", 4) == 0) {
            g.lights = 0;
            g.lgot_seq++;
            return;
        }
        if (n >= kHeader + 9 && std::memcmp(d + kHeader, "FTLP", 4) == 0) {
            g.ftlp_car = d[kHeader + 4];
            g.ftlp_time = rd<float>(d + kHeader + 5);
            g.ftlp_seq++;
            return;
        }
        if (n >= kHeader + 5 && std::memcmp(d + kHeader, "RTMT", 4) == 0) {
            g.rtmt_car = d[kHeader + 4];
            g.rtmt_seq++;
            return;
        }
        if (n >= kHeader + 6 && std::memcmp(d + kHeader, "COLL", 4) == 0) {
            g.coll_a = d[kHeader + 4];
            g.coll_b = d[kHeader + 5];
            g.coll_seq++;
            return;
        }
        if (n >= kHeader + 6 && std::memcmp(d + kHeader, "SCAR", 4) == 0) {
            g.scar_type = d[kHeader + 4];
            g.scar_event = d[kHeader + 5];
            g.scar_seq++;
            return;
        }
        if (n >= kHeader + 4 && std::memcmp(d + kHeader, "DRSD", 4) == 0) {
            g.drsd_reason = n > kHeader + 4 ? int(d[kHeader + 4]) : -1;
            g.drsd_seq++;
            return;
        }
        if (n < kHeader + 8 || std::memcmp(d + kHeader, "BUTN", 4) != 0)
            return;
        static uint32_t held = 0;
        uint32_t now_held = rd<uint32_t>(d + kHeader + 4);
        uint32_t down = now_held & ~held;
        held = now_held;
        for (int i = 0; i < 12; i++)
            if (down & (0x00100000u << i))
                g.action_presses[i]++;
        break;
    }
    case 5: { // car setups: front wing, rear wing, on-throttle diff, ...; a float after the cars
        size_t per = (n - kHeader - 4) / cars;
        if (per < 4)
            return;
        g.diff = d[kHeader + per * me + 2];
        g.setup_tick = now;
        break;
    }
    case 6: { // car telemetry: brakesTemperature u16[4] @22, tyresInnerTemperature u8[4] @34 (RL RR FL FR); 3 bytes
              // after the cars (MFD panel indices, suggested gear)
        size_t per = (n - kHeader - 3) / cars;
        if (per < 38)
            return;
        const uint8_t *c = d + kHeader + per * me;
        static const int order[4] = {2, 3, 0, 1}; // packet RL RR FL FR -> FL FR RL RR
        for (int i = 0; i < 4; i++) {
            g.brake_temp[order[i]] = rd<uint16_t>(c + 22 + 2 * i);
            g.tyre_inner[order[i]] = c[34 + i];
        }
        g.drs_open = c[18];                 // drs: 0 off, 1 on
        // engineTemperature @38: a u8 in the 2026 format (59 bytes a car), a u16 before (60 bytes); the launcher's
        // notes (udp_telemetry_logic.hpp) and the f1-packets 2026 spec
        if (format >= 2026 && per >= 39)
            g.engine_temp = c[38];
        else if (format < 2026 && per >= 40)
            g.engine_temp = rd<uint16_t>(c + 38);
        g.tel_tick = now;
        break;
    }
    case 16: { // car telemetry 2 (2026 season pack): overtake available @4, active @5, 2026 rules @8 (Wheel Dash)
        size_t per = (n - kHeader) / cars;
        if (per < 9)
            return;
        const uint8_t *c = d + kHeader + per * me;
        g.st_active = c[0];
        g.st_available = c[1];
        g.st_distance = rd<uint16_t>(c + 2);
        g.ot_available = c[4];
        g.ot_active = c[5];
        g.regs2026 = c[8];
        uint64_t lo = 0, hi = 0;
        std::memcpy(&lo, c, per >= 8 ? 8 : per);
        if (per > 8)
            std::memcpy(&hi, c + 8, per >= 16 ? 8 : per - 8);
        g.t2_raw[0] = lo;
        g.t2_raw[1] = hi;
        g.t2_size = int(per);
        g.t2_tick = now;
        break;
    }
    case 7: { // car status
        size_t per = (n - kHeader) / cars;
        if (per < 42)
            return;
        const uint8_t *c = d + kHeader + per * me;
        g.mix = c[2];                       // fuelMix: 0 lean, 1 standard, 2 rich, 3 max
        g.bbal = c[3];                      // frontBrakeBias %
        g.pit_limiter = c[4];               // pitLimiterStatus: 0 off, 1 on
        g.fuel_laps = rd<float>(c + 13);    // fuelRemainingLaps (the MFD's margin)
        g.ers_mode = c[41];                 // ersDeployMode
        g.drs_allowed = c[22];              // drsAllowed: 0 not allowed, 1 allowed
        g.drs_distance = rd<uint16_t>(c + 23); // drsActivationDistance: metres to the next zone, 0 none
        g.fia_flag = int8_t(c[28]);         // vehicleFiaFlags
        g.tyre_actual = c[25];              // actualTyreCompound (C1..C6, inter, wet)
        g.tyre_visual = c[26];              // visualTyreCompound
        g.tyre_age = c[27];                 // tyresAgeLaps
        g.status_tick = now;
        g.from_launcher = false;
        break;
    }
    case 10: { // car damage: tyresWear[4] (RL RR FL FR), tyresDamage[4], brakesDamage[4], tyreBlisters[4] (F1 25),
               // then the wings, floor, diffuser, sidepod
        size_t per = (n - kHeader) / cars;
        if (per < 34)
            return;
        const uint8_t *c = d + kHeader + per * me;
        g.wear[2] = rd<float>(c + 0);
        g.wear[3] = rd<float>(c + 4);
        g.wear[0] = rd<float>(c + 8);
        g.wear[1] = rd<float>(c + 12);
        g.dmg_tyre[2] = c[16]; // tyresDamage RL RR FL FR @16
        g.dmg_tyre[3] = c[17];
        g.dmg_tyre[0] = c[18];
        g.dmg_tyre[1] = c[19];
        g.dmg_brake[2] = c[20]; // brakesDamage RL RR FL FR @20
        g.dmg_brake[3] = c[21];
        g.dmg_brake[0] = c[22];
        g.dmg_brake[1] = c[23];
        size_t w = per >= 46 ? 28 : 24;
        g.dmg_fwl = c[w];
        g.dmg_fwr = c[w + 1];
        g.dmg_rw = c[w + 2];
        g.dmg_floor = c[w + 3];
        g.dmg_diffuser = c[w + 4];
        g.dmg_sidepod = c[w + 5];
        if (per >= w + 14) { // drsFault, ersFault, gearBoxDamage, engineDamage, then the power unit's wear
            g.dmg_gearbox = c[w + 8];
            g.dmg_engine = c[w + 9];
            g.wear_ice = c[w + 13];
        }
        g.damage_tick = now;
        break;
    }
    default:
        break;
    }
}

inline void decode_safe(const char *buf, int len)
{
    if (!buf || len <= 0 || len > 4096)
        return;
    __try {
        decode(reinterpret_cast<const uint8_t *>(buf), size_t(len));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// ------------------------------------------------------------------ the wrappers
using SendTo = int(WSAAPI *)(SOCKET, const char *, int, int, const sockaddr *, int);
using WsaSendTo = int(WSAAPI *)(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD, const sockaddr *, int, LPWSAOVERLAPPED,
                                LPWSAOVERLAPPED_COMPLETION_ROUTINE);
constexpr int kSlots = 8;
inline std::atomic<SendTo> g_next_sendto[kSlots];
inline std::atomic<WsaSendTo> g_next_wsa[kSlots];

inline std::atomic<uint32_t> g_calls[2][kSlots]; // calls through each wrapper (sendto, WSASendTo)

template <int I> int WSAAPI hook_sendto(SOCKET s, const char *buf, int len, int flags, const sockaddr *to, int tolen)
{
    g_calls[0][I]++;
    decode_safe(buf, len);
    return g_next_sendto[I].load()(s, buf, len, flags, to, tolen);
}

template <int I>
int WSAAPI hook_wsa(SOCKET s, LPWSABUF bufs, DWORD count, LPDWORD sent, DWORD flags, const sockaddr *to, int tolen,
                    LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE done)
{
    g_calls[1][I]++;
    if (bufs && count == 1)
        decode_safe(bufs[0].buf, int(bufs[0].len));
    return g_next_wsa[I].load()(s, bufs, count, sent, flags, to, tolen, ov, done);
}

inline const SendTo kSendtoHooks[kSlots] = {&hook_sendto<0>, &hook_sendto<1>, &hook_sendto<2>, &hook_sendto<3>,
                                            &hook_sendto<4>, &hook_sendto<5>, &hook_sendto<6>, &hook_sendto<7>};
inline const WsaSendTo kWsaHooks[kSlots] = {&hook_wsa<0>, &hook_wsa<1>, &hook_wsa<2>, &hook_wsa<3>,
                                            &hook_wsa<4>, &hook_wsa<5>, &hook_wsa<6>, &hook_wsa<7>};

// ------------------------------------------------------------------ installing
struct Result {
    int sendto_slots = 0, wsa_slots = 0, scanned_mb = 0;
    const char *how = "";
    char where[256] = ""; // each slot: section name + RVA
};

inline void note_slot(Result &r, HMODULE mod, const char *label, void **slot, const char *what)
{
    auto *base = reinterpret_cast<uint8_t *>(mod);
    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + reinterpret_cast<IMAGE_DOS_HEADER *>(base)->e_lfanew);
    auto *sec = IMAGE_FIRST_SECTION(nt);
    uintptr_t rva = reinterpret_cast<uint8_t *>(slot) - base;
    char name[9] = "?";
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++)
        if (rva >= sec->VirtualAddress && rva < sec->VirtualAddress + sec->Misc.VirtualSize) {
            std::memcpy(name, sec->Name, 8);
            name[8] = 0;
        }
    size_t n = std::strlen(r.where);
    if (n < sizeof r.where - 64)
        std::snprintf(r.where + n, sizeof r.where - n, "%s%s %s:%s+%#llx", n ? ", " : "", what, label, name,
                      static_cast<unsigned long long>(rva));
}

inline bool write_slot(void **slot, void *value)
{
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old))
        return false;
    InterlockedExchangePointer(slot, value);
    DWORD tmp = 0;
    VirtualProtect(slot, sizeof(void *), old, &tmp);
    return true;
}

// Every pointer-aligned slot of a module's readable memory that holds `target`. Unreadable or uncommitted pages are
// skipped (the game has sections it decrypts at run time). 0.2.0 skipped code sections and found one slot the game
// never called through; this protected exe keeps its import slots where they're marked as code.
template <class F> inline int scan_for(HMODULE mod, void *target, F &&found, int &scanned_mb)
{
    auto *base = reinterpret_cast<uint8_t *>(mod);
    auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    auto *sec = IMAGE_FIRST_SECTION(nt);
    int hits = 0;
    size_t bytes = 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        uint8_t *p = base + sec->VirtualAddress, *end = p + sec->Misc.VirtualSize;
        while (p < end) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(p, &mbi, sizeof mbi))
                break;
            uint8_t *rend = static_cast<uint8_t *>(mbi.BaseAddress) + mbi.RegionSize;
            if (rend > end)
                rend = end;
            bool readable = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
                            (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                            PAGE_EXECUTE_READWRITE));
            if (readable) {
                __try {
                    for (uint8_t *q = reinterpret_cast<uint8_t *>((uintptr_t(p) + 7) & ~uintptr_t(7)); q + 8 <= rend; q += 8)
                        if (*reinterpret_cast<void **>(q) == target) {
                            found(reinterpret_cast<void **>(q));
                            hits++;
                        }
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                }
                bytes += size_t(rend - p);
            }
            p = rend;
        }
    }
    scanned_mb = int(bytes >> 20);
    return hits;
}

inline Result install()
{
    Result r;
    HMODULE ws2 = GetModuleHandleW(L"ws2_32.dll");
    if (!ws2) {
        r.how = "ws2_32.dll is not loaded";
        return r;
    }
    void *real_sendto = reinterpret_cast<void *>(GetProcAddress(ws2, "sendto"));
    void *real_wsa = reinterpret_cast<void *>(GetProcAddress(ws2, "WSASendTo"));
    // First the game's own slots. When the launcher has already put its UDP hook there (it does when a career
    // has custom drivers whose ids it rewrites in the packets), they hold the launcher's function instead, and the
    // address of the real sendto is where the launcher keeps it to call on: the tap goes in there, behind the
    // launcher's hook (the game -> the launcher's hook -> the tap -> ws2_32), so both run. (0.3.1 found no slot
    // in a career with custom drivers and read no packets.)
    auto tap_in = [&](HMODULE mod, const char *label) {
        int mb = 0;
        if (real_sendto)
            scan_for(mod, real_sendto, [&](void **slot) {
                if (r.sendto_slots >= kSlots)
                    return;
                int i = r.sendto_slots;
                g_next_sendto[i] = reinterpret_cast<SendTo>(*slot);
                if (write_slot(slot, reinterpret_cast<void *>(kSendtoHooks[i]))) {
                    r.sendto_slots++;
                    note_slot(r, mod, label, slot, "sendto");
                }
            }, mb);
        r.scanned_mb += mb;
        if (real_wsa)
            scan_for(mod, real_wsa, [&](void **slot) {
                if (r.wsa_slots >= kSlots)
                    return;
                int i = r.wsa_slots;
                g_next_wsa[i] = reinterpret_cast<WsaSendTo>(*slot);
                if (write_slot(slot, reinterpret_cast<void *>(kWsaHooks[i]))) {
                    r.wsa_slots++;
                    note_slot(r, mod, label, slot, "WSASendTo");
                }
            }, mb);
    };
    tap_in(GetModuleHandleW(nullptr), "game");
    if (!r.sendto_slots && !r.wsa_slots)
        if (HMODULE core = GetModuleHandleW(L"swofty_core.dll"))
            tap_in(core, "launcher");
    r.how = r.sendto_slots || r.wsa_slots ? "tapped" : "no slot holds sendto or WSASendTo, in the game or behind the launcher's hook";
    return r;
}

} // namespace udp_tap

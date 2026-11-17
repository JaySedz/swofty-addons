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
    g.player = me;
    uint64_t now = GetTickCount64();
    switch (id) {
    case 2: { // lap data: lastLapTimeInMS first; 2 bytes after the cars (time trial indices)
        size_t per = (n - kHeader - 2) / cars;
        if (per < 4)
            return;
        const uint8_t *c = d + kHeader + per * me;
        g.last_lap_ms = rd<uint32_t>(c);
        g.lap_tick = now;
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
    case 7: { // car status
        size_t per = (n - kHeader) / cars;
        if (per < 42)
            return;
        const uint8_t *c = d + kHeader + per * me;
        g.mix = c[2];                       // fuelMix: 0 lean, 1 standard, 2 rich, 3 max
        g.bbal = c[3];                      // frontBrakeBias %
        g.fuel_laps = rd<float>(c + 13);    // fuelRemainingLaps (the MFD's margin)
        g.ers_mode = c[41];                 // ersDeployMode
        g.status_tick = now;
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
        size_t w = per >= 46 ? 28 : 24;
        g.dmg_fwl = c[w];
        g.dmg_fwr = c[w + 1];
        g.dmg_rw = c[w + 2];
        g.dmg_floor = c[w + 3];
        g.dmg_diffuser = c[w + 4];
        g.dmg_sidepod = c[w + 5];
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

inline void note_slot(Result &r, void **slot, const char *what)
{
    auto *base = reinterpret_cast<uint8_t *>(GetModuleHandleW(nullptr));
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
    if (n < sizeof r.where - 48)
        std::snprintf(r.where + n, sizeof r.where - n, "%s%s %s+%#llx", n ? ", " : "", what, name,
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

// Every pointer-aligned slot of the game module's readable memory that holds `target`. Unreadable or uncommitted
// pages are skipped (the game has sections it decrypts at run time). 0.2.0 skipped code sections and found one
// slot the game never called through; this protected exe keeps its import slots where they're marked as code.
template <class F> inline int scan_for(void *target, F &&found, int &scanned_mb)
{
    auto *base = reinterpret_cast<uint8_t *>(GetModuleHandleW(nullptr));
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
    int mb = 0;
    if (real_sendto)
        scan_for(real_sendto, [&](void **slot) {
            if (r.sendto_slots >= kSlots)
                return;
            int i = r.sendto_slots;
            g_next_sendto[i] = reinterpret_cast<SendTo>(*slot);
            if (write_slot(slot, reinterpret_cast<void *>(kSendtoHooks[i]))) {
                r.sendto_slots++;
                note_slot(r, slot, "sendto");
            }
        }, mb);
    r.scanned_mb = mb;
    if (real_wsa)
        scan_for(real_wsa, [&](void **slot) {
            if (r.wsa_slots >= kSlots)
                return;
            int i = r.wsa_slots;
            g_next_wsa[i] = reinterpret_cast<WsaSendTo>(*slot);
            if (write_slot(slot, reinterpret_cast<void *>(kWsaHooks[i]))) {
                r.wsa_slots++;
                note_slot(r, slot, "WSASendTo");
            }
        }, mb);
    r.how = r.sendto_slots || r.wsa_slots ? "tapped" : "no import slot holds sendto or WSASendTo (another tap may own them)";
    return r;
}

} // namespace udp_tap

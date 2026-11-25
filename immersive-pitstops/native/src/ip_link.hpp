#pragma once
// The link between Immersive Pitstops (sedz.immersive-pitstops) and Driver Display (sedz.driver-display), version 1.
// The SAME file is in both repos (native/src/ip_link.hpp): change both together, and bump `version` for any change to
// the layout (new fields go at the end of Car, new functions at the end of State).
//
// Immersive Pitstops publishes State as a launcher service (swofty::services::publish); Driver Display looks it up
// (swofty::services::when). Either addon on its own never sees the other: no service, no change. Nothing here
// changes what Immersive Pitstops does in the game; it only tells another addon what it did.
#include <cstdint>

namespace ip_link {

enum : uint8_t {
    retire_none = 0,
    retire_stuck_nut = 1,   // the team retired the car in the box after a stuck wheel nut
    retire_loose_wheel = 2, // the team retired the car after a loose wheel (it came off / the player didn't box)
};

struct Car {
    uint32_t size;     // the caller sets sizeof(Car) as it knows it; the publisher writes no more than that
    uint8_t stuck;     // 1: in the box now, with a stuck wheel nut sequence playing on this stop
    uint8_t loose;     // 1: released with a loose wheel, until it's fixed at a stop, comes off or the car retires
    uint8_t wheel_off; // 1: the loose wheel has come off (stays until the car's next stop or a new session)
    uint8_t retired;   // retire_*: Immersive Pitstops retired the car (set BEFORE the retirement is posted)
    float box_secs;    // seconds stationary in the box on the current stop (0 when not in the box)
};

struct State {
    static constexpr const char *name = "sedz.immersive-pitstops.state";
    static constexpr uint32_t version = 1;
    uint32_t size; // sizeof(State)
    // car: the live vehicle id (the same index as swofty::live::car / the UDP packets). Fills *out (out->size set
    // by the caller) and returns true; false when the car is out of range or the addon is off this session.
    // Callable from any thread; reads atomics only.
    bool (*car)(uint32_t car, Car *out);
    // 1.2.25 / Immersive Pitstops 1.0.23, appended within version 1: true while Immersive Pitstops' Ferrari Mode
    // option is on. A reader takes it only when size reaches past it (an older Immersive Pitstops publishes the
    // table without it: size == offsetof(State, ferrari_mode)), so both addons still link across versions.
    bool (*ferrari_mode)();
};

} // namespace ip_link

// Helmet Cam 0.1.6: whether the helmet shows, with a memory, so the launcher's view tracking can blip without a flash.
//
// The core's view reads "unknown" for a frame or a few at lights out and around in-game menus. A pass limited to the
// cockpit view is cut hard in those frames (0.1.5 flashed). These passes run in every view instead and keep a fade
// (0 to 1) in a small persistent texture:
// - the cockpit view, confirmed and not paused / replay / photo / menu / cutscene / spectating: fade in (0.15 s)
// - a known other view or one of those states: off at once
// - unknown: hold for 0.35 s after the last confirmed cockpit frame (lights out, a menu opening), then fade out
// The visor pass reads the fade and leaves the picture untouched at 0.
//
// state_a (persistent, 3 x 1, r32f) holds last frame's values; "update" writes this frame's to state_b, "keep" copies
// them back to state_a. Texels: 0 fade, 1 time of the last confirmed cockpit frame, 2 time of this frame.
//
// 0.1.8: "update" lists the `previous` picture as an input it never reads. The core (launcher 1.10.x) clears
// persistent textures whenever the view serial differs from the one it last recorded, and it records the serial only
// while some pass reads `previous`. Without it, state_a was wiped every frame and the helmet never showed (0.1.7).
#include "swofty/postfx.hlsli"

#define HOLD_S 0.35
#define FADE_IN_S 0.15
#define FADE_OUT_S 0.20

float4 update(float2 uv, uint2 pixel)
{
    float fade = input_at(0, int2(0, 0)).x;
    float last_cockpit = input_at(0, int2(1, 0)).x;
    float last_t = input_at(0, int2(2, 0)).x;
    float t = swofty_time;
    uint blocked = SWOFTY_VIEW_PAUSED | SWOFTY_VIEW_REPLAY | SWOFTY_VIEW_PHOTO | SWOFTY_VIEW_MENU | SWOFTY_VIEW_CUTSCENE |
                   SWOFTY_VIEW_SPECTATING;
    bool valid = swofty_view_valid != 0;
    bool cockpit = valid && swofty_view == SWOFTY_VIEW_COCKPIT && (swofty_view_flags & blocked) == 0;

    // 0.1.8: the memory is wiped (the core clears persistent textures on a view change; 0.1.7 found it wiped every
    // frame) or this is the first frame: no memory to go on, so the current view decides at once (as 0.1.5 did).
    // The helmet never vanishes for want of state.
    if (last_t <= 0)
    {
        float now = cockpit ? 1.0 : 0.0;
        float v0 = pixel.x == 0 ? now : (pixel.x == 1 ? (cockpit ? t : -100.0) : t);
        return float4(v0, 0, 0, 1);
    }
    // a restart of the pass clock, or a long gap (no 3D picture for a while): start again from the current view
    if (!(t >= last_t) || t - last_t > 1.0)
    {
        fade = cockpit ? 1.0 : 0.0;
        last_cockpit = cockpit ? t : -100;
        last_t = t;
    }
    float dt = clamp(t - last_t, 0.0, 0.1);
    if (cockpit)
        last_cockpit = t;

    if (cockpit)
        fade = min(1.0, fade + dt / FADE_IN_S);
    else if (valid)
        fade = 0; // a known other view, pause, replay, photo, menu, cutscene: at once
    else if (t - last_cockpit > HOLD_S)
        fade = max(0.0, fade - dt / FADE_OUT_S); // unknown for longer than a blip: a real menu
    // else: unknown for a moment: hold

    float v = pixel.x == 0 ? fade : (pixel.x == 1 ? last_cockpit : t);
    return float4(v, 0, 0, 1);
}

float4 keep(float2 uv, uint2 pixel)
{
    return input_at(0, int2(pixel));
}

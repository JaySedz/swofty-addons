// Helmet Cam: the view from inside a helmet, drawn on the cockpit camera's picture before the HUD.
//
// The world moves (head sway from the addon's code, road vibration); the helmet does not: the eye port's edges,
// the brow padding, the visor's tint and smudges and the team's visor strip stay fixed to the screen, as they do
// for a camera inside the helmet. Everything is procedural except the strip atlas (tex/strips.png).
//
// Options (swofty.json): 0 helmet edge, 1 brow padding, 2 tint (0 clear, 1 light smoke, 2 dark smoke, 3 iridium),
// 3 smudges, 4 visor strip, 5 cornering sway, 6 vibration, 7 mirror the strip (1) or show it readable (0),
// 8 braking sway.
// Data from the addon's code (32 bytes, all optional: without it the visor is still, with the generic strip):
//   0 shift x, 4 shift y (picture heights, + = the camera looks right / down), 8 roll (radians, + = clockwise),
//   12 speed (km/h), 16 strip row (uint, 0 = generic), 20 rpm (0 to 1), 24 visor rain (0 to 1, 0.1.7).
#include "swofty/postfx.hlsli"

#define STRIP_ROWS 16.0

float hash21(float2 p)
{
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float value_noise(float2 p)
{
    float2 i = floor(p), f = frac(p);
    float2 u = f * f * (3 - 2 * f);
    return lerp(lerp(hash21(i), hash21(i + float2(1, 0)), u.x), lerp(hash21(i + float2(0, 1)), hash21(i + float2(1, 1)), u.x), u.y);
}

float fbm(float2 p)
{
    float v = 0, a = 0.5;
    [unroll] for (int i = 0; i < 4; i++)
    {
        v += a * value_noise(p);
        p = p * 2.03 + 17.1;
        a *= 0.5;
    }
    return v;
}

// The visor strip's colour (linear) and coverage at strip coordinates s (0 to 1 across, 0 to 1 down), softly
// out of focus: the strip sits a few centimetres from the eye.
float4 strip(float2 s, float row)
{
    s = saturate(s);
    float2 texel = 1.0 / float2(input_size(0));
    float4 acc = 0;
    [unroll] for (int y = -1; y <= 1; y++)
        [unroll] for (int x = -2; x <= 2; x++)
        {
            float2 at = s + float2(x * 2.5, y * 1.5) * texel;
            at.y = (row + clamp(at.y, 0.02, 0.98)) / STRIP_ROWS;
            acc += input(0, at);
        }
    acc /= 15;
    return float4(srgb_to_linear(acc.rgb), acc.a);
}

float3 tint_colour(float t)
{
    if (t < 0.5) return float3(0.97, 0.97, 0.98);  // clear
    if (t < 1.5) return float3(0.80, 0.80, 0.84);  // light smoke
    if (t < 2.5) return float3(0.52, 0.52, 0.56);  // dark smoke
    return float3(0.66, 0.58, 0.46);               // iridium, seen from inside: warm and dim
}

float4 visor(float2 uv, uint2 pixel);

// 0.1.7: rain on the visor. p: picture heights, centred, y down. speed01: 0 parked .. 1 flat out. rain: 0 .. 1.
// Slow, drops bead and run down; at speed the airflow over the visor pushes them out to the sides and a little up,
// stretched into streaks with a thinning tail. Three layers of drops on grids that move with the flow (no state), each
// drop forming and drying on its own clock so a still visor keeps changing.
// Returns xy: how the drop bends the view (picture heights), z: coverage, w: how central (1 middle .. 0 rim).
float4 drops(float2 p, float t, float speed01, float rain)
{
    float side = p.x < 0 ? -1.0 : 1.0;
    float fx = abs(p.x);
    float s2 = speed01 * speed01;
    float velx = lerp(0.01, 1.4, s2);            // outward
    float vely = lerp(0.03, -0.12, speed01);     // slow: running down; fast: pushed up
    float stretch = 1 + 3.5 * s2;
    // at speed the airflow clears the middle of the visor: drops survive towards the sides
    float half_w = 0.5 * float(swofty_size.x) / float(swofty_size.y);
    float keep = lerp(1.0, 0.15 + 0.85 * smoothstep(0.15, 0.85, fx / half_w), speed01) * lerp(1.0, 0.6, speed01);
    float4 best = 0;
    [unroll] for (int layer = 0; layer < 3; layer++)
    {
        float sc = layer == 0 ? 1.0 : (layer == 1 ? 0.65 : 0.42);
        float m = 1 + 0.35 * layer;
        float2 cs = float2(0.10, 0.05) * sc;
        float2 moved = float2(fx - velx * m * t, p.y - vely * m * t);
        float2 cell = floor(moved / cs);
        [unroll] for (int dx = -1; dx <= 1; dx++)
        {
            float2 c = cell + float2(dx, 0);
            float h1 = hash21(c + layer * 19.7);
            float h2 = hash21(c * 1.37 + 5.1 + layer * 3.3);
            float h3 = hash21(c * 2.11 + 9.7 + layer);
            if (h1 > rain * keep * (0.55 - 0.1 * layer))
                continue;
            float life = frac(t * (0.12 + 0.1 * h3) + h2);
            float alive = smoothstep(0.0, 0.08, life) * (1 - smoothstep(0.85, 1.0, life));
            float2 centre = (c + 0.5 + (float2(h2, h3) - 0.5) * float2(0.6, 0.5)) * cs;
            float2 local = moved - centre;
            float r = cs.y * (0.16 + 0.2 * h3) * lerp(1.0, 0.65, speed01);
            float2 e = local / float2(r * stretch, r);
            float body = 1 - smoothstep(0.75, 1.0, length(e));
            float tail = 0;
            float tail_len = r * stretch * 1.8 * speed01;
            if (local.x < 0 && tail_len > 0)
            {
                float k = saturate(-local.x / (tail_len + r * stretch));
                float w = r * (1 - k) * 0.3;
                tail = (1 - smoothstep(w * 0.5, w + 1e-5, abs(local.y))) * (1 - k);
            }
            float cov = max(body, tail * 0.65) * alive;
            if (cov > best.z)
            {
                float2 n = clamp(e, -1, 1) * body;
                best = float4(float2(n.x * side, n.y) * r * 1.6, cov, body * saturate(1 - length(e)));
            }
        }
    }
    return best;
}

// 0.1.6: the pass runs in every view; fx/state.hlsl decides how much of the helmet shows (input 1, texel 0), so a
// blip in the launcher's view tracking holds instead of flashing, and menus fade.
float4 effect(float2 uv, uint2 pixel)
{
    float4 original = scene_at(int2(pixel));
    float fade = saturate(input_at(1, int2(0, 0)).x);
    if (!(fade > 0.001))
        return original;
    float4 v = visor(uv, pixel);
    return fade >= 0.999 ? v : float4(lerp(original.rgb, v.rgb, fade), original.a);
}

float4 visor(float2 uv, uint2 pixel)
{
    float aspect = float(swofty_size.x) / float(swofty_size.y);
    float edge = option(0) / 100, brow = option(1) / 100, smudge = option(3) / 100;
    float strip_amount = option(4) / 100, vibration = option(6) / 100;
    float sway = max(option(5), option(8)) / 100; // cornering, braking (0.1.3): the zoom covers the bigger one
    bool mirror = option(7) > 0.5;

    bool has_data = data_size() >= 24;
    float2 shift = has_data ? float2(data_float(0), data_float(4)) : 0;
    float roll = has_data ? data_float(8) : 0;
    float speed = has_data ? data_float(12) : 0;
    float row = has_data ? float(min(data_uint(16), 15u)) : 0;
    // 0.1.10: Aston Martin Mode (the 2026 Aston Martin): the addon's code sends 3 (300 %, 0.1.11), and the slider has no say
    if (data_size() >= 32 && data_float(28) > 0)
        vibration = min(data_float(28), 3.0);
    // a bad buffer gives a wrong picture, never a wild one
    shift = clamp(shift, -0.08, 0.08);
    roll = clamp(roll, -0.10, 0.10);

    // road vibration, growing with speed: a fast buzz plus slower bumps
    float v = vibration * saturate(speed / 300);
    float t = swofty_time;
    shift += v * 0.0030 * float2(sin(t * 61.0) * 0.6 + sin(t * 97.3) * 0.4, sin(t * 73.7) * 0.7 + sin(t * 131.1) * 0.3);
    shift += v * 0.0035 * float2(0.4 * sin(t * 8.3 + 1.1), sin(t * 11.7) * 0.6 + sin(t * 17.9 + 2.0) * 0.4);

    // the world through the helmet: zoomed in so a moved picture never shows its own edge
    // 0.1.11: past 100 % (Aston Martin Mode) it zooms in more, so the bigger shake never shows the picture's edge
    float zoom = 1 + 0.12 * sway + 0.012 * min(vibration, 1.0) + 0.02 * max(vibration - 1.0, 0.0);
    float2 p = (uv - 0.5) * float2(aspect, 1);              // centred, in picture heights
    float c = cos(roll), s = sin(roll);
    float2 q = float2(c * p.x + s * p.y, -s * p.x + c * p.y) / zoom + shift;
    float2 suv = q / float2(aspect, 1) + 0.5;

    // a hint of colour fringe towards the edges of the visor's curve
    float r2 = dot(p, p);
    float2 fringe = (suv - 0.5) * r2 * 0.0025 * edge;
    float3 rgb;
    rgb.r = to_linear(scene(suv + fringe).rgb).r;
    rgb.g = to_linear(scene(suv).rgb).g;
    rgb.b = to_linear(scene(suv - fringe).rgb).b;

    // 0.1.7: rain on the visor (fixed to the helmet; the view through each drop is bent like a small lens)
    float rain = data_size() >= 28 ? saturate(data_float(24)) : 0;
    if (rain > 0.001)
    {
        float sp = saturate(speed / 280);
        // spray and mist at speed: the view softens
        float mist = rain * sp * 0.35;
        if (mist > 0.001)
        {
            float3 blur = 0;
            blur += to_linear(scene(suv + float2(0.006, 0)).rgb);
            blur += to_linear(scene(suv - float2(0.006, 0)).rgb);
            blur += to_linear(scene(suv + float2(0, 0.008)).rgb);
            blur += to_linear(scene(suv - float2(0, 0.008)).rgb);
            blur *= 0.25;
            rgb = lerp(rgb, blur * 0.92 + luminance(blur) * 0.1, mist);
        }
        float4 dr = drops(p, t, sp, rain);
        if (dr.z > 0.001)
        {
            float2 off = dr.xy / float2(aspect, 1);
            float3 through = to_linear(scene(suv - off * 2.0).rgb);
            through = through * 0.82 + luminance(through) * 0.08;
            through *= lerp(0.65, 1.05, saturate(dr.w * 2.5)); // darker at the rim, clear in the middle
            through += 0.04 * smoothstep(0.55, 0.8, dr.w);     // a small highlight
            rgb = lerp(rgb, through, dr.z * 0.9);
        }
    }

    // visor: tint and smudges (fixed to the helmet)
    rgb *= tint_colour(option(2));
    if (smudge > 0)
    {
        float2 n = uv * float2(aspect, 1);
        float haze = smoothstep(0.52, 0.85, fbm(n * 2.2 + 3.7)) * smudge;
        float streak = smoothstep(0.62, 0.95, fbm(float2(n.x * 9, n.y * 0.8) + 11.3)) * smudge * 0.5;
        float lum = luminance(rgb);
        // smudges scatter light: they lift dark areas towards the light around them and soften the bright ones
        rgb = lerp(rgb, rgb * 0.88 + lum * 0.18 + 0.008, saturate(haze * 0.8 + streak * 0.5));
    }

    float xn = (uv.x - 0.5) * 2;                         // -1 left edge, 1 right edge

    // the visor strip, just under the brow padding, following its curve
    float brow_edge = 0.02 + 0.13 * brow + 0.06 * brow * xn * xn;
    if (strip_amount > 0)
    {
        float top = brow_edge - 0.012, height = 0.055;
        float sv = (uv.y - top) / height;
        float su = (mirror ? 1 - uv.x : uv.x);
        su = 0.5 + (su - 0.5) * 0.92;
        float4 st = strip(float2(su, sv), row);
        float inside = smoothstep(-0.05, 0.05, sv) * (1 - smoothstep(0.95, 1.05, sv));
        // seen against the sky the strip is backlit: its print reads dim and dark
        float3 strip_rgb = st.rgb * 0.30;
        rgb = lerp(rgb, strip_rgb, saturate(st.a * inside * strip_amount));
    }

    // brow padding across the top, curving down towards the sides
    if (brow > 0)
    {
        float pad = 1 - smoothstep(brow_edge - 0.03, brow_edge + 0.012, uv.y);
        float fabric = 0.015 + 0.01 * value_noise(uv * float2(aspect, 1) * 60);
        rgb = lerp(rgb, rgb * 0.08 + fabric, pad * saturate(brow * 1.2));
    }

    // the eye port: a wide rounded opening, darker into the corners and at the bottom
    if (edge > 0)
    {
        float2 e = abs(p) / float2(0.5 * aspect * 1.04, 0.5 * 1.10);
        e.y *= (p.y > 0 ? 1.08 : 1.0);
        float d = pow(pow(e.x, 4) + pow(e.y, 4), 0.25);
        // the opening closes in as the slider goes up: at 100 the helmet frames the view, black at the corners
        float inner = lerp(0.98, 0.60, edge);
        float port = smoothstep(inner, inner + 0.28, d) * saturate(0.6 + edge);
        rgb = lerp(rgb, rgb * 0.03 + 0.004, saturate(port));
    }

    return float4(from_linear(max(rgb, 0)), scene_at(int2(pixel)).a);
}

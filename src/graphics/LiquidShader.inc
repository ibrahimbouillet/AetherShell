// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// D62/D63: the two materials, as one pixel shader. A line-by-line port of the fragment shader in
// tools/liquid-glass/welcome-bubble.html, which the owner validated: change both together.
// Included by LiquidRenderer.cpp as a raw string and compiled once per device.
R"HLSL(
cbuffer Scene : register(b0) {
    float4 gB[2];        // centre x, y, half width, half height (target pixels)
    float4 gS[2];        // corner radius, presence, bevel, second mode
    float4 gPicture;     // where the desktop picture lies: left, top, width, height (target pixels)
    float4 gLogo;        // the mark: centre x, y, half size, alpha
    float2 gSize;        // target pixels
    float gDpr;          // pixels per DIP
    float gK;            // union reach between the two bodies (pixels)
    float gStyle;        // 0 liquid, 1 grain
    float gLens;         // liquid: 0 a ball of water, 1 the bars' edges
    float gVeil;         // the welcome cover clearing
    float gBlur;
    float gBackground;   // 1: the picture fills the target; 0: transparent outside the shapes and their shadow
    float gShadow;       // scales the exterior shadow (0 where a window region would cut it)
    float gLodBias;      // the picture's texels per target pixel, as a mip level
    float gMarkLod;
};
Texture2D gPictureTex : register(t0);
Texture2D gMarkTex : register(t1);
SamplerState gLinear : register(s0);

struct VertexOut { float4 position : SV_Position; };
VertexOut VS(uint id : SV_VertexID) {
    VertexOut o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.position = float4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
    return o;
}

float sdRoundBox(float2 p, float2 b, float r) { float2 q = abs(p) - b + r; return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r; }
float smin(float a, float b, float k) { if (k <= 0.0) return min(a, b); float h = saturate(0.5 + 0.5 * (b - a) / k); return lerp(b, a, h) - k * h * (1.0 - h); }
float body(int i, float2 p) {
    float4 B = gB[i];
    if (gS[i].y <= 0.0) return 1e5;
    float2 q = p - B.xy;
    float m = min(B.z, B.w);
    float d = sdRoundBox(q, B.zw, min(gS[i].x, m));
    if (gS[i].w != 0.0) { float l = max(length(q), 1e-3); d -= gS[i].w * m * (2.0 * q.y * q.y / (l * l) - 1.0); }   // cos 2θ about the vertical
    return d;
}
float field(float2 p) { return smin(body(0, p), body(1, p), gK); }
float hash(float2 p) { p = frac(p * float2(443.897, 441.423)); p += dot(p, p.yx + 19.19); return frac((p.x + p.y) * p.x); }
float3 tap(float2 px, float lod) { return gPictureTex.SampleLevel(gLinear, saturate((px - gPicture.xy) / gPicture.zw), lod + gLodBias).rgb; }
float3 soft(float2 px, float lod) {   // four taps soften the mip steps of a blurred read
    if (lod < 0.75) return tap(px, lod);
    float2 t = 0.6 * exp2(lod);
    return 0.25 * (tap(px + t, lod) + tap(px + float2(-t.x, t.y), lod) + tap(px + float2(t.x, -t.y), lod) + tap(px - t, lod));
}

float4 PS(VertexOut input) : SV_Target {
    float2 p = input.position.xy;   // top-left origin, pixel centres
    bool background = gBackground > 0.5;
    float3 color = background ? soft(p, gBlur) * (1.0 - gVeil) : float3(0.0, 0.0, 0.0);
    float d0 = body(0, p), d1 = body(1, p);
    float d = smin(d0, d1, gK);
    if (d > 36.0 * gDpr) return background ? float4(color, 1.0) : float4(0.0, 0.0, 0.0, 0.0);   // far from every shape
    // The shadow under the water or the glass, outside it only.
    float shadowAlpha = 0.0;
    if (d > -1.0 * gDpr) {
        float ds = field(p - float2(0.0, lerp(4.0, 3.0, gLens) * gDpr));
        float shadow = (1.0 - smoothstep(-6.0 * gDpr, lerp(28.0, 20.0, gLens) * gDpr, ds)) * lerp(0.2, 0.26, gStyle) * gShadow;
        shadowAlpha = shadow * smoothstep(-1.0, 1.0, d);
        color *= 1.0 - shadowAlpha;
    }
    float4 outside = background ? float4(color, 1.0) : float4(0.0, 0.0, 0.0, shadowAlpha);
    float coverage = saturate(0.5 - d / max(gDpr, 1.0));
    if (coverage <= 0.0) return outside;

    float2 g = float2(field(p + float2(gDpr, 0.0)) - d, field(p + float2(0.0, gDpr)) - d);
    g = dot(g, g) > 1e-10 ? normalize(g) : float2(0.0, 1.0);   // outward
    // The water around this point: the bodies blended by nearness, so that the lens follows the drop, then
    // each lobe, without a seam across the bridge.
    float total = 0.0, lr = 0.0, bevel = 12.0 * gDpr, best = 1e5;
    float2 lc = float2(0.0, 0.0);
    [unroll] for (int i = 0; i < 2; i++) {
        if (gS[i].y <= 0.0) continue;
        float di = i == 0 ? d0 : d1;
        float m = max(min(gB[i].z, gB[i].w), 1.0);
        float w = exp(-max(di, -m) / (0.3 * m));
        lc += w * gB[i].xy; lr += w * m; total += w;
        if (di < best) { best = di; bevel = gS[i].z; }
    }
    lc /= max(total, 1e-6); lr /= max(total, 1e-6); bevel = max(bevel, 1.0);
    float s = 1.0 - saturate(-d / bevel);   // 1 at the edge, 0 where the surface is flat
    float2 q = p - lc;
    float rn = saturate(length(q) / max(lr, 1.0));
    float hairline = 1.0 - smoothstep(0.0, 1.25 * gDpr, -d);
    float mark = 0.0;
    if (gLogo.w > 0.0) {
        float2 lu = (p - gLogo.xy) * (1.0 - 0.12 * rn * rn) / gLogo.z * 0.5 + 0.5;   // upright, swelling with the drop
        if (all(lu > 0.0) && all(lu < 1.0)) mark = gMarkTex.SampleLevel(gLinear, lu, gMarkLod).a * gLogo.w;
    }

    float3 c;
    if (gStyle < 0.5) {
        // LIQUID. Pure water: no tint, no blur, no grain, no semi-transparency. It bends the picture: a ball of
        // water turns it over and takes in a wide view of it; a bar bends it only at its edges.
        float z = sqrt(max(1.0 - s * s, 0.0));
        float3 n = normalize(float3(g * s / max(z, 0.12), 1.0));
        float2 ball = -q * (1.0 + lerp(1.5, 2.6, rn * rn));
        float2 offset = lerp(ball, -n.xy * bevel * 1.15, gLens);
        c = tap(p + offset, gBlur * 0.5) * (1.0 - gVeil);   // one read: no colour separation, no fringe of any colour
        // The mark has no colour of its own: a lighter, frosted shape in the water.
        if (mark > 0.0) c = lerp(c, soft(p + offset, 3.0) * (1.0 - gVeil) * 1.22 + 0.14, mark * 0.9);
        // White light only. It enters at the top left (a sharp highlight, a sheen), and the water gathers it into
        // one bright place on the far side: the light's focus. The edge catches it, brightest towards the light.
        float2 through = normalize(float2(0.42, 0.78));
        float3 L = normalize(float3(-0.42, -0.78, 0.6));
        float3 H = normalize(L + float3(0.0, 0.0, 1.0));
        float spec = pow(max(dot(n, H), 0.0), lerp(70.0, 120.0, gLens)) * lerp(1.2, 0.8, gLens);
        float top = saturate(-dot(g, normalize(float2(0.35, 1.0))));
        float sheen = smoothstep(0.6, 0.97, s) * top * lerp(0.16, 0.12, gLens);
        float fresnel = pow(s, 6.0) * lerp(0.34, 0.24, gLens);
        float2 fp = q - through * 0.58 * lr;
        float focusDrop = exp(-dot(fp, fp) / (2.0 * (0.16 * lr) * (0.16 * lr))) * 0.5;
        float focusBar = smoothstep(0.55, 0.9, s) * (1.0 - smoothstep(0.95, 1.0, s)) * pow(max(dot(g, through), 0.0), 3.0) * 0.4;
        float rim = hairline * (0.22 + 0.5 * max(dot(g, -through), 0.0) + 0.25 * max(dot(g, through), 0.0));
        c += spec + sheen + fresnel + lerp(focusDrop, focusBar, gLens) + rim;
    } else {
        // GRAIN. Aether's frosted material: the picture blurred behind a semi-transparent neutral shade, with a
        // fixed grain and a hairline edge. No refraction and no light of its own.
        c = soft(p, 4.6) * (1.0 - gVeil);
        c = lerp(c, float3(0.115, 0.115, 0.115), 0.42);
        if (mark > 0.0) c = lerp(c, float3(0.92, 0.92, 0.92), mark * 0.75);
        c += (hash(floor(p / max(gDpr, 1.0))) - 0.5) * 0.06;
        c += hairline * 0.16;
    }
    return lerp(outside, float4(c, 1.0), coverage);
}
)HLSL"

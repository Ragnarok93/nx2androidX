#version 450
#extension GL_GOOGLE_include_directive : require
#define A_GPU 1
#define A_GLSL 1
#include "ffx_a.h"
#define FSR_EASU_F 1
#define FSR_RCAS_F 1
#include "ffx_fsr1.h"
#include "gsr1_filter.h"

// Descriptor and varying interface of the certified guest filter.
// Keep the original vertex stage and zero alpha on every covered sample.
layout(set = 1, binding = 1) uniform sampler2D tex;
layout(set = 1, binding = 2) uniform GuestFilter {
    vec4 sizes;
    vec4 bounds;
} guest;
layout(push_constant) uniform SpatialDraw {
    vec2 targetSize;
    float sharpness;
} draw;
layout(constant_id = 0) const int filterMode = 2;
layout(location = 0) noperspective in vec4 interpolated;
layout(location = 0) out vec4 color;

AF4 FsrEasuRF(AF2 uv) { return textureGather(tex, uv, 0); }
AF4 FsrEasuGF(AF2 uv) { return textureGather(tex, uv, 1); }
AF4 FsrEasuBF(AF2 uv) { return textureGather(tex, uv, 2); }

vec3 easu(vec2 uv, vec2 size) {
    AU4 c0, c1, c2, c3;
    FsrEasuCon(c0, c1, c2, c3, size.x, size.y, size.x, size.y, size.x, size.y);
    c0.zw = floatBitsToUint(fma(uv, size, vec2(-0.5)));
    AF3 rgb;
    FsrEasuF(rgb, AU2(0), c0, c1, c2, c3);
    return clamp(rgb, 0.0, 1.0);
}

vec2 rcasUV, rcasStep, rcasSize;
AF4 FsrRcasLoadF(ASU2 offset) {
    vec2 uv = clamp(rcasUV + vec2(offset) * rcasStep,
                    rcasStep * 0.5, 1.0 - rcasStep * 0.5);
    // Match the RGBA8 intermediate of the two-stage presentation path.
    return vec4(roundEven(easu(uv, rcasSize) * 255.0) / 255.0, 0.0);
}
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}

vec3 fsr(vec2 uv, vec2 size) {
    float strength = clamp(draw.sharpness, 0.0, 1.0);
    if (strength == 0.0) return easu(uv, size);
    rcasUV = uv;
    rcasStep = 1.0 / draw.targetSize;
    rcasSize = size;
    AU4 c;
    FsrRcasCon(c, -log2(max(strength, 1e-6)));
    AF1 r, g, b;
    FsrRcasF(r, g, b, AU2(0), c);
    return clamp(vec3(r, g, b), 0.0, 1.0);
}

vec2 minNumber(vec2 a, vec2 b) {
    return mix(mix(min(a, b), a, isnan(b)), b, isnan(a));
}
vec2 maxNumber(vec2 a, vec2 b) {
    return mix(mix(max(a, b), a, isnan(b)), b, isnan(a));
}
vec3 guestSmoothstep(vec2 uv) {
    vec2 texel = fma(uv, guest.sizes.xy, vec2(-0.5));
    vec2 cell = floor(texel);
    vec2 frac = texel - cell;
    vec2 coord = (fma(fma(vec2(-2.0), frac, vec2(3.0)), frac * frac, cell) + 0.5) * guest.sizes.zw;
    coord = maxNumber(minNumber(coord, guest.bounds.zw), guest.bounds.xy);
    return textureLod(tex, mix(coord, vec2(0.0), isnan(coord)), 0.0).rgb;
}

void main() {
    vec2 uv = interpolated.xy;
    vec2 size = vec2(textureSize(tex, 0));
    vec2 halfTexel = 0.5 / size;
    const vec2 epsilon = vec2(0.000002);
    vec2 step = 1.0 / draw.targetSize;
    vec2 dx = dFdx(uv), dy = dFdy(uv);
    bool fullSource = all(equal(guest.sizes.xy, size)) &&
        all(lessThanEqual(abs(guest.sizes.zw * size - 1.0), epsilon)) &&
        all(greaterThanEqual(guest.bounds.xy, vec2(0.0))) &&
        all(lessThanEqual(guest.bounds.xy, halfTexel + epsilon)) &&
        all(greaterThanEqual(guest.bounds.zw, 1.0 - halfTexel - epsilon)) &&
        all(lessThanEqual(guest.bounds.zw, vec2(1.0)));
    bool affineGrid = all(lessThanEqual(abs(abs(vec2(dx.x, dy.y)) - step), epsilon)) &&
        all(lessThanEqual(abs(vec2(dx.y, dy.x)), epsilon)) &&
        all(greaterThanEqual(uv, step * 0.5 - epsilon)) &&
        all(lessThanEqual(uv, 1.0 - step * 0.5 + epsilon));
    if (!fullSource || !affineGrid) {
        color = vec4(guestSmoothstep(uv), 0.0);
        return;
    }
    vec3 rgb = filterMode == 2 ? fsr(uv, size) : gsr1Filter(tex, uv, size, draw.sharpness).rgb;
    color = vec4(rgb, 0.0);
}

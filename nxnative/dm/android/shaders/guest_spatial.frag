#version 450
#extension GL_GOOGLE_include_directive : require
#define A_GPU 1
#define A_GLSL 1
#include "ffx_a.h"
#define FSR_EASU_F 1
#define FSR_RCAS_F 1
#include "ffx_fsr1.h"
#include "gsr1_filter.h"

#include "guest_filter_input.h"
layout(constant_id = 0) const int filterMode = 2;
// Same certified image view, but with reconstruction's own linear-clamp
// sampler. The original guest sampler is reserved for native fallback.
layout(set = 2, binding = 0) uniform sampler2D gridTex;

AF4 FsrEasuRF(AF2 uv) { return textureGather(gridTex, uv, 0); }
AF4 FsrEasuGF(AF2 uv) { return textureGather(gridTex, uv, 1); }
AF4 FsrEasuBF(AF2 uv) { return textureGather(gridTex, uv, 2); }

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

void main() {
    vec2 uv = interpolated.xy;
    vec2 size = vec2(textureSize(tex, 0));
    if (!guestFullGrid(uv, size)) {
        color = vec4(guestSmoothstep(uv), 0.0);
        return;
    }
    vec3 rgb = filterMode == 9 ? textureLod(gridTex, uv, 0.0).rgb :
        filterMode == 2 ? fsr(uv, size) : gsr1Filter(gridTex, uv, size, draw.sharpness).rgb;
    color = vec4(rgb, 0.0);
}

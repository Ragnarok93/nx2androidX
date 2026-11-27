#version 450
#extension GL_GOOGLE_include_directive : require
#define A_GPU 1
#define A_GLSL 1
#include "ffx_a.h"
#define FSR_RCAS_F 1
#include "ffx_fsr1.h"
#include "guest_filter_input.h"

layout(set = 2, binding = 0) uniform sampler2D reconstructed;

AF4 FsrRcasLoadF(ASU2 pos) {
    return texelFetch(reconstructed, clamp(pos, ivec2(0), textureSize(reconstructed, 0) - 1), 0);
}
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}

void main() {
    vec2 uv = interpolated.xy;
    vec2 targetPixel = uv * draw.targetSize;
    bool centered = all(lessThanEqual(abs(fract(targetPixel) - 0.5), vec2(0.002)));
    if (!guestFullGrid(uv, vec2(textureSize(tex, 0))) || !centered) {
        color = vec4(guestSmoothstep(uv), 0.0);
        return;
    }
    ivec2 size = textureSize(reconstructed, 0);
    ivec2 pos = clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1);
    float strength = clamp(draw.sharpness, 0.0, 1.0);
    if (strength == 0.0) {
        color = vec4(texelFetch(reconstructed, pos, 0).rgb, 0.0);
        return;
    }
    AU4 constants;
    FsrRcasCon(constants, -log2(max(strength, 1e-6)));
    AF1 r, g, b;
    FsrRcasF(r, g, b, AU2(pos), constants);
    color = vec4(clamp(vec3(r, g, b), 0.0, 1.0), 0.0);
}

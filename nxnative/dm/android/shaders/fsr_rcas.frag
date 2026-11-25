#version 450
#extension GL_GOOGLE_include_directive : require
// AMD FidelityFX FSR1 RCAS; strength 0 bypasses the filter, 100 is maximum.
#define A_GPU 1
#define A_GLSL 1
#include "ffx_a.h"
#define FSR_RCAS_F 1
#include "ffx_fsr1.h"
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(push_constant) uniform FsrParams {
    vec4 sizes;
    vec2 origin;
    float sharpness;
    float padding;
} params;
layout(location = 0) out vec4 color;
AF4 FsrRcasLoadF(ASU2 pos) {
    return texelFetch(tex, clamp(pos, ivec2(0), textureSize(tex, 0) - ivec2(1)), 0);
}
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}
void main() {
    ivec2 px = ivec2(gl_FragCoord.xy - params.origin);
    AU4 c;
    FsrRcasCon(c, 2.0 - 2.0 * clamp(params.sharpness, 0.0, 1.0));
    AF1 r, g, b;
    FsrRcasF(r, g, b, AU2(px), c);
    color = vec4(clamp(vec3(r, g, b), vec3(0.0), vec3(1.0)), texelFetch(tex, px, 0).a);
}

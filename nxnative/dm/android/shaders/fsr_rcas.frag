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
    vec4 sampleColor = texelFetch(tex, clamp(pos, ivec2(0), textureSize(tex, 0) - ivec2(1)), 0);
    // GSR2 history is FP16. Reproduce the removed 8-bit UNORM intermediate
    // conversion before RCAS, not after the nonlinear sharpening filter.
    // Ordinary FSR1 and bilinear RCAS already read the quantized image.
    if (params.padding > 0.5)
        sampleColor = unpackUnorm4x8(packUnorm4x8(sampleColor));
    return sampleColor;
}
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}
void main() {
    ivec2 px = ivec2(gl_FragCoord.xy - params.origin);
    AU4 c;
    // RCAS takes attenuation in stops and internally computes exp2(-stops).
    // Map the slider to linear filter gain: 1% means 0.01, not about 0.25.
    // At exactly zero the host bypasses this pass entirely.
    float strength = clamp(params.sharpness, 0.0, 1.0);
    FsrRcasCon(c, -log2(max(strength, 1e-6)));
    AF1 r, g, b;
    FsrRcasF(r, g, b, AU2(px), c);
    color = vec4(clamp(vec3(r, g, b), vec3(0.0), vec3(1.0)), FsrRcasLoadF(px).a);
}

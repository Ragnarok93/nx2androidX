#version 450
#extension GL_GOOGLE_include_directive : require
// AMD FidelityFX FSR1 EASU, 32-bit precision; the sample image is a cropped,
// exact-size copy of the game's window texture, before UI compositing.
#define A_GPU 1
#define A_GLSL 1
#include "ffx_a.h"
#define FSR_EASU_F 1
#include "ffx_fsr1.h"
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(push_constant) uniform FsrParams {
    vec4 sizes;     // input width/height, output width/height
    vec2 origin;
    float sharpness;
    float padding;
} params;
layout(location = 0) out vec4 color;
AF4 FsrEasuRF(AF2 uv) { return textureGather(tex, uv, 0); }
AF4 FsrEasuGF(AF2 uv) { return textureGather(tex, uv, 1); }
AF4 FsrEasuBF(AF2 uv) { return textureGather(tex, uv, 2); }
void main() {
    AU4 c0, c1, c2, c3;
    FsrEasuCon(c0, c1, c2, c3, params.sizes.x, params.sizes.y,
               params.sizes.x, params.sizes.y, params.sizes.z, params.sizes.w);
    AF3 rgb;
    FsrEasuF(rgb, AU2(gl_FragCoord.xy), c0, c1, c2, c3);
    color = vec4(clamp(rgb, vec3(0.0), vec3(1.0)), 1.0);
}

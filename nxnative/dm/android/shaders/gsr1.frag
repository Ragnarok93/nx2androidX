#version 450
#extension GL_GOOGLE_include_directive : require
#include "gsr1_filter.h"
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(push_constant) uniform GsrParams {
    vec4 sizes; // input width/height, output width/height
    vec2 origin;
    float sharpness;
    float padding;
} params;
layout(location = 0) out vec4 color;

void main() {
    color = gsr1Filter(tex, gl_FragCoord.xy / params.sizes.zw, params.sizes.xy, params.sharpness);
}

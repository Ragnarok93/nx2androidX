#version 450
// Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
// Adapted from sgsr/v1/include/glsl/sgsr1_shader_mobile.frag at
// d926f074bcb9d714e179f1ce0fcb9ee2eeb5074e. See licenses/sgsr1.txt.
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(push_constant) uniform GsrParams {
    vec4 sizes; // input width/height, output width/height
    vec2 origin;
    float sharpness;
    float padding;
} params;
layout(location = 0) out vec4 color;

vec2 weightY(float dx, float dy, float c, float deviation) {
    float x = (dx * dx + dy * dy) * 0.55 + clamp(abs(c) * deviation, 0.0, 1.0);
    float a = x - 4.0;
    float w = (x * a - a) * (a * a);
    return vec2(w, w * c);
}

void main() {
    vec2 uv = gl_FragCoord.xy / params.sizes.zw;
    vec4 base = textureLod(tex, uv, 0.0);
    vec2 pixelSize = 1.0 / params.sizes.xy;
    vec2 imgCoord = uv * params.sizes.xy + vec2(-0.5, 0.5);
    vec2 imgCoordPixel = floor(imgCoord);
    vec2 coord = imgCoordPixel * pixelSize;
    vec2 pl = imgCoord - imgCoordPixel;
    vec4 left = textureGather(tex, coord, 1);
    float edgeVote = abs(left.z - left.y) + abs(base.g - left.y) + abs(base.g - left.z);
    color = base;
    if (edgeVote <= 8.0 / 255.0) return;

    coord.x += pixelSize.x;
    vec4 right = textureGather(tex, coord + vec2(pixelSize.x, 0.0), 1);
    vec4 upDown;
    upDown.xy = textureGather(tex, coord - vec2(0.0, pixelSize.y), 1).wz;
    upDown.zw = textureGather(tex, coord + vec2(0.0, pixelSize.y), 1).yx;

    float mean = (left.y + left.z + right.x + right.w) * 0.25;
    left -= mean;
    right -= mean;
    upDown -= mean;
    float sum = dot(abs(left) + abs(right) + abs(upDown), vec4(1.0));
    float deviation = 2.181818 / max(sum, 1e-6);

    vec2 weights = weightY(pl.x,       pl.y + 1.0, upDown.x, deviation);
    weights +=     weightY(pl.x - 1.0, pl.y + 1.0, upDown.y, deviation);
    weights +=     weightY(pl.x - 1.0, pl.y - 2.0, upDown.z, deviation);
    weights +=     weightY(pl.x,       pl.y - 2.0, upDown.w, deviation);
    weights +=     weightY(pl.x + 1.0, pl.y - 1.0, left.x,   deviation);
    weights +=     weightY(pl.x,       pl.y - 1.0, left.y,   deviation);
    weights +=     weightY(pl.x,       pl.y,       left.z,   deviation);
    weights +=     weightY(pl.x + 1.0, pl.y,       left.w,   deviation);
    weights +=     weightY(pl.x - 1.0, pl.y - 1.0, right.x,  deviation);
    weights +=     weightY(pl.x - 2.0, pl.y - 1.0, right.y,  deviation);
    weights +=     weightY(pl.x - 2.0, pl.y,       right.z,  deviation);
    weights +=     weightY(pl.x - 1.0, pl.y,       right.w,  deviation);
    if (abs(weights.x) < 1e-6) return;

    float minY = min(min(left.y, left.z), min(right.x, right.w));
    float maxY = max(max(left.y, left.z), max(right.x, right.w));
    // The midpoint uses the upstream edge sharpness of 2. Ranging from 1
    // to 3 keeps the reconstruction filter active throughout the slider.
    float finalY = clamp((1.0 + 2.0 * params.sharpness) * weights.y / weights.x, minY, maxY);
    float delta = clamp(finalY - (base.g - mean), -23.0 / 255.0, 23.0 / 255.0);
    color.rgb = clamp(base.rgb + delta, 0.0, 1.0);
}

// Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
// Adapted from sgsr/v1/include/glsl/sgsr1_shader_mobile.frag at
// d926f074bcb9d714e179f1ce0fcb9ee2eeb5074e. See licenses/sgsr1.txt.
vec2 weightY(float dx, float dy, float c, float deviation) {
    float x = (dx * dx + dy * dy) * 0.55 + clamp(abs(c) * deviation, 0.0, 1.0);
    float a = x - 4.0;
    float w = (x * a - a) * (a * a);
    return vec2(w, w * c);
}

vec4 gsr1Filter(sampler2D tex, vec2 uv, vec2 inputSize, float strength) {
    vec4 base = textureLod(tex, uv, 0.0);
    vec2 pixelSize = 1.0 / inputSize;
    vec2 imgCoord = uv * inputSize + vec2(-0.5, 0.5);
    vec2 imgCoordPixel = floor(imgCoord);
    vec2 coord = imgCoordPixel * pixelSize;
    vec2 pl = imgCoord - imgCoordPixel;
    vec4 left = textureGather(tex, coord, 1);
    float edgeVote = abs(left.z - left.y) + abs(base.g - left.y) + abs(base.g - left.z);
    if (edgeVote <= 8.0 / 255.0) return base;

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
    if (abs(weights.x) < 1e-6) return base;

    float minY = min(min(left.y, left.z), min(right.x, right.w));
    float maxY = max(max(left.y, left.z), max(right.x, right.w));
    // Keep the full-strength edge correction inside the original local-range
    // and delta bounds. Apply strength after both clamps: multiplying before
    // them made the slider collapse to identical values on saturated edges.
    // Retain one third of the adaptive correction at zero so GSR1 does not
    // become bilinear; maximum is unchanged from the previous strength 3.
    float finalY = clamp(3.0 * weights.y / weights.x, minY, maxY);
    float delta = clamp(finalY - (base.g - mean), -23.0 / 255.0, 23.0 / 255.0);
    delta *= mix(1.0 / 3.0, 1.0, clamp(strength, 0.0, 1.0));
    return vec4(clamp(base.rgb + delta, 0.0, 1.0), base.a);
}

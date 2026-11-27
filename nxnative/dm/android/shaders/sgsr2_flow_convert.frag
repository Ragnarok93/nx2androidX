#version 450

layout(set = 0, binding = 1) uniform sampler2D CurrentColor;
layout(set = 0, binding = 2) uniform sampler2D PreviousColor;
layout(location = 0) out vec4 MotionRejection;
layout(std140, set = 0, binding = 0) uniform Frame {
    layout(offset = 120) float reset; // fragment_params.reset
} frame;

// Each output pixel covers one 8x8 source tile. Probe five locations and use
// a bounded search so the work scales with tiles, rather than output pixels.
const vec2 taps[5] = vec2[](vec2(0), vec2(-2,-2), vec2(2,-2), vec2(-2,2), vec2(2,2));
const vec2 directions[9] = vec2[](vec2(0), vec2(-1,0), vec2(1,0), vec2(0,-1), vec2(0,1),
                                 vec2(-1,-1), vec2(-1,1), vec2(1,-1), vec2(1,1));

float luma(sampler2D image, vec2 pixel, vec2 invSize) {
    vec3 rgb = textureLod(image, pixel * invSize, 0).rgb;
    return sqrt(max(0.0, dot(rgb, vec3(0.2126, 0.7152, 0.0722))));
}

float errorAt(sampler2D image, vec2 center, vec2 invSize, float reference[5]) {
    float error = 0.0;
    for (int i = 0; i < 5; ++i)
        error += abs(reference[i] - luma(image, center + taps[i], invSize));
    return error * 0.2;
}

void main() {
    MotionRejection = vec4(0, 0, 1, 0);
    if (frame.reset > 0.0) return;
    vec2 size = vec2(textureSize(CurrentColor, 0));
    vec2 invSize = 1.0 / size;
    vec2 center = floor(gl_FragCoord.xy) * 8.0 + 4.5;
    if (any(greaterThanEqual(center + 2.0, size))) return;

    float samples[5];
    float darkest = 1.0, brightest = 0.0;
    for (int i = 0; i < 5; ++i) {
        samples[i] = luma(CurrentColor, center + taps[i], invSize);
        darkest = min(darkest, samples[i]);
        brightest = max(brightest, samples[i]);
    }
    if (brightest - darkest < 0.025) return;

    vec2 best = vec2(0);
    float cost = errorAt(PreviousColor, center, invSize, samples);
    // Coarse-to-fine search. Preserve stationary content on ties.
    // Errors are nonnegative and an improvement must exceed 0.001.
    // Once this floor is reached no subsequent candidate can win.
    for (float stepSize = 4.0; stepSize >= 0.5 && cost > 0.001; stepSize *= 0.5) {
        vec2 origin = best;
        for (int j = 1; j < 9 && cost > 0.001; ++j) {
            vec2 delta = origin + directions[j] * stepSize;
            vec2 p = center + delta;
            if (any(lessThan(p, vec2(2.5))) || any(greaterThan(p, size - 2.5))) continue;
            float tested = errorAt(PreviousColor, p, invSize, samples);
            if (tested + 0.001 < cost) {
                cost = tested;
                best = delta;
            }
        }
    }
    if (cost > 0.035) return;

    // Reject a match if the reverse direction prefers a different
    // neighborhood. A second good forward match is also ambiguous.
    float reverseSamples[5];
    // The reverse test cannot reject when its nonnegative error plus 0.005
    // is already >= cost. Forward ambiguity must still be evaluated.
    bool reverseNeeded = cost > 0.005;
    if (reverseNeeded)
        for (int i = 0; i < 5; ++i)
            reverseSamples[i] = luma(PreviousColor, center + best + taps[i], invSize);
    for (int j = 1; j < 9; ++j) {
        vec2 p = center + directions[j] * 2.0;
        if (reverseNeeded) {
            if (errorAt(CurrentColor, p, invSize, reverseSamples) + 0.005 < cost) return;
        }
        p += best;
        if (any(lessThan(p, vec2(2.5))) || any(greaterThan(p, size - 2.5))) continue;
        if (errorAt(PreviousColor, p, invSize, samples) <= cost + 0.006) return;
    }

    // The Qualcomm accumulation pass expects current minus previous NDC,
    // with positive-up Y. best is current-to-previous in image pixels.
    MotionRejection = vec4(best * invSize * vec2(-2, 2), clamp(cost / 0.035, 0.0, 1.0), 0);
}

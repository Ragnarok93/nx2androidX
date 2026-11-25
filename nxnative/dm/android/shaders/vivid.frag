#version 450
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(push_constant) uniform DisplayParams {
    vec4 sizes;
    vec2 origin;
    float sharpness;
    float vivid; // 1 UNORM, 2 sRGB attachment
} params;
layout(location = 0) out vec4 color;
// GameNative Vulkan Vivid (utkarshdalal/GameNative, window.frag):
// display-referred saturation and contrast, not HDR10 output.
vec3 vivid_color(vec3 rgb) {
    if (params.vivid < 0.5) return rgb;
    bool srgb = params.vivid > 1.5;
    if (srgb) rgb = mix(12.92 * rgb, 1.055 * pow(max(rgb, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055, greaterThan(rgb, vec3(0.0031308)));
    float luma = dot(rgb, vec3(0.299, 0.587, 0.114));
    rgb = clamp((mix(vec3(luma), rgb, 1.25) - 0.5) * 1.08 + 0.5, 0.0, 1.0);
    if (srgb) rgb = mix(rgb / 12.92, pow((rgb + 0.055) / 1.055, vec3(2.4)), greaterThan(rgb, vec3(0.04045)));
    return rgb;
}
void main() {
    ivec2 px = ivec2(gl_FragCoord.xy - params.origin);
    vec4 src = texelFetch(tex, px, 0);
    color = vec4(vivid_color(src.rgb), src.a);
}

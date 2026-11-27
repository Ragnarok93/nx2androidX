// Descriptor and varying interface of the certified guest filter.
// Keep the original vertex stage and zero alpha on every covered sample.
layout(set = 1, binding = 1) uniform sampler2D tex;
layout(set = 1, binding = 2) uniform GuestFilter {
    vec4 sizes;
    vec4 bounds;
} guest;
layout(push_constant) uniform SpatialDraw {
    vec2 targetSize;
    float sharpness;
} draw;
layout(location = 0) noperspective in vec4 interpolated;
layout(location = 0) out vec4 color;

vec2 minNumber(vec2 a, vec2 b) {
    return mix(mix(min(a, b), a, isnan(b)), b, isnan(a));
}
vec2 maxNumber(vec2 a, vec2 b) {
    return mix(mix(max(a, b), a, isnan(b)), b, isnan(a));
}
vec3 guestSmoothstep(vec2 uv) {
    vec2 texel = fma(uv, guest.sizes.xy, vec2(-0.5));
    vec2 cell = floor(texel);
    vec2 frac = texel - cell;
    vec2 coord = (fma(fma(vec2(-2.0), frac, vec2(3.0)), frac * frac, cell) + 0.5) * guest.sizes.zw;
    coord = maxNumber(minNumber(coord, guest.bounds.zw), guest.bounds.xy);
    return textureLod(tex, mix(coord, vec2(0.0), isnan(coord)), 0.0).rgb;
}

bool guestFullGrid(vec2 uv, vec2 size) {
    vec2 halfTexel = 0.5 / size;
    const vec2 epsilon = vec2(0.000002);
    vec2 step = 1.0 / draw.targetSize;
    vec2 dx = dFdx(uv), dy = dFdy(uv);
    bool fullSource = all(equal(guest.sizes.xy, size)) &&
        all(lessThanEqual(abs(guest.sizes.zw * size - 1.0), epsilon)) &&
        all(greaterThanEqual(guest.bounds.xy, vec2(0.0))) &&
        all(lessThanEqual(guest.bounds.xy, halfTexel + epsilon)) &&
        all(greaterThanEqual(guest.bounds.zw, 1.0 - halfTexel - epsilon)) &&
        all(lessThanEqual(guest.bounds.zw, vec2(1.0)));
    bool affineGrid = all(lessThanEqual(abs(abs(vec2(dx.x, dy.y)) - step), epsilon)) &&
        all(lessThanEqual(abs(vec2(dx.y, dy.x)), epsilon)) &&
        all(greaterThanEqual(uv, step * 0.5 - epsilon)) &&
        all(lessThanEqual(uv, 1.0 - step * 0.5 + epsilon));
    return fullSource && affineGrid;
}

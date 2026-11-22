#version 450
layout(push_constant) uniform Params { vec4 color; float depth; } params;
layout(location = 0) out vec4 color;
void main() {
    gl_FragDepth = params.depth;
    color = params.color;
}

#version 450
layout(early_fragment_tests) in;
layout(push_constant) uniform Params { vec4 color; float depth; } params;
layout(location = 0) out vec4 color;
void main() {
    color = params.color;
}

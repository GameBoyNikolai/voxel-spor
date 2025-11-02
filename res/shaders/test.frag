#version 450

layout(location = 0) in vec3 in_color;
layout(location = 1) in vec2 uv;

layout(binding = 1) uniform sampler2D texture_sampler;

layout(location = 0) out vec4 out_color;

void main() {
    out_color = texture(texture_sampler, uv) * vec4(in_color, 1.0);
}
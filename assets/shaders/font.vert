#version 450 core
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uvIn;
layout(location = 2) in vec3 colorIn;
out vec2 uv;
out vec3 color;
void main() { gl_Position = vec4(position, 0.0, 1.0); uv = uvIn; color = colorIn; }

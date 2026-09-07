#version 450 core
layout(location = 0) in vec2 position;
layout(location = 1) in vec3 colorIn;
layout(location = 2) in vec2 uvIn;
out vec3 color;
out vec2 uv;
void main() { gl_Position = vec4(position, 0.0, 1.0); color = colorIn; uv = uvIn; }

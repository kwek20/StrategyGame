#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 textureCoordinate;
layout(location = 2) in vec4 color;
uniform mat4 viewProjection;
out vec2 particleUv;
out vec4 particleColor;
void main() {
    particleUv = textureCoordinate;
    particleColor = color;
    gl_Position = viewProjection * vec4(position, 1.0);
}

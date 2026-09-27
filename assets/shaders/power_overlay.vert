#version 450 core
layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec3 inColor;
uniform mat4 viewProjection;
out vec3 overlayColor;
void main() {
    gl_Position = viewProjection * vec4(inPosition, 1.0);
    overlayColor = inColor;
}

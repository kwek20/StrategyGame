#version 450 core
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
uniform mat4 viewProjection;
out vec3 vertexColor;
out vec3 vertexNormal;
out vec3 worldPosition;
void main() {
    gl_Position = viewProjection * vec4(inPosition, 1.0);
    vertexColor = inColor;
    vertexNormal = inNormal;
    worldPosition = inPosition;
}

#version 450 core
layout(location = 0) in vec3 inPosition;

uniform mat4 viewProjection;
uniform float waterLevel;

out vec3 worldPosition;
out float terrainHeight;

void main() {
    terrainHeight = inPosition.y;
    worldPosition = vec3(inPosition.x, waterLevel, inPosition.z);
    gl_Position = viewProjection * vec4(worldPosition, 1.0);
}

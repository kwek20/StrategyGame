#version 450 core
layout(location = 0) in vec3 inPosition;

uniform mat4 viewProjection;
uniform float waterLevel;
uniform float timeSeconds;

out vec3 worldPosition;
out float terrainHeight;

void main() {
    terrainHeight = inPosition.y;
    worldPosition = vec3(inPosition.x, waterLevel, inPosition.z);
    float depth = max(waterLevel - terrainHeight, 0.0);
    float shoreFade = smoothstep(0.08, 0.85, depth);
    float primary = sin(inPosition.x * 0.115 + inPosition.z * 0.073 + timeSeconds * 0.72);
    float secondary = sin(inPosition.x * -0.061 + inPosition.z * 0.149 + timeSeconds * 0.47);
    worldPosition.y += (primary * 0.035 + secondary * 0.020) * shoreFade;
    gl_Position = viewProjection * vec4(worldPosition, 1.0);
}

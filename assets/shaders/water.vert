#version 450 core
layout(location = 0) in vec3 inPosition;
layout(location = 5) in float inWaterSurfaceHeight;
layout(location = 6) in float inWaterCoverage;
layout(location = 7) in vec2 inWaterFlow;
layout(location = 8) in float inGeneratedWaterSurfaceHeight;
layout(location = 9) in float inGeneratedWaterCoverage;

uniform mat4 viewProjection;
uniform float waterLevel;
uniform float timeSeconds;
uniform int waterDebugMode;

out vec3 worldPosition;
out float terrainHeight;
out float localWaterLevel;
out float waterCoverage;
out vec2 waterFlow;
out float generatedDepth;
out float smoothedDepth;

void main() {
    terrainHeight = inPosition.y;
    bool showGenerated = waterDebugMode == 1;
    bool showDifference = waterDebugMode == 3;
    float selectedSurface = showGenerated ? inGeneratedWaterSurfaceHeight
                                          : inWaterSurfaceHeight;
    if (showDifference)
        selectedSurface = max(inGeneratedWaterSurfaceHeight, inWaterSurfaceHeight);
    localWaterLevel = max(selectedSurface, inPosition.y);
    waterCoverage = showGenerated ? inGeneratedWaterCoverage : inWaterCoverage;
    if (showDifference)
        waterCoverage = max(inGeneratedWaterCoverage, inWaterCoverage);
    waterFlow = inWaterFlow;
    generatedDepth = max(inGeneratedWaterSurfaceHeight - inPosition.y, 0.0);
    smoothedDepth = max(inWaterSurfaceHeight - inPosition.y, 0.0);
    worldPosition = vec3(inPosition.x, localWaterLevel, inPosition.z);
    float depth = max(localWaterLevel - terrainHeight, 0.0);
    float shoreFade = smoothstep(0.08, 0.85, depth);
    vec2 flow = length(inWaterFlow) > 0.1 ? normalize(inWaterFlow)
                                         : normalize(vec2(0.78, 0.62));
    vec2 crossFlow = vec2(-flow.y, flow.x);
    float primary = sin(dot(inPosition.xz, flow) * 0.14 - timeSeconds * 0.72);
    float secondary = sin(dot(inPosition.xz, crossFlow) * 0.11 - timeSeconds * 0.47);
    worldPosition.y += (primary * 0.035 + secondary * 0.020) * shoreFade;
    gl_Position = viewProjection * vec4(worldPosition, 1.0);
}

#version 450 core
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec4 inMaterialWeights;
layout(location = 4) in float inTraversalClass;
layout(location = 5) in float inWaterSurfaceHeight;
layout(location = 6) in float inWaterCoverage;
uniform mat4 viewProjection;
out vec3 vertexColor;
out vec3 vertexNormal;
out vec3 worldPosition;
out vec4 materialWeights;
out float traversalClass;
out float localWaterSurfaceHeight;
out float waterCoverage;
void main() {
    gl_Position = viewProjection * vec4(inPosition, 1.0);
    vertexColor = inColor;
    vertexNormal = inNormal;
    worldPosition = inPosition;
    materialWeights = inMaterialWeights;
    traversalClass = inTraversalClass;
    localWaterSurfaceHeight = inWaterSurfaceHeight;
    waterCoverage = inWaterCoverage;
}

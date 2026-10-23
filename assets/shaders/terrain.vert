#version 450 core
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec4 inMaterialWeights;
layout(location = 4) in float inTraversalClass;
layout(location = 5) in float inWaterSurfaceHeight;
layout(location = 6) in float inWaterCoverage;
uniform mat4 viewProjection;
uniform bool usePlacementSurface;
uniform sampler2D placementSurface;
uniform ivec2 placementSurfaceOrigin;
uniform ivec2 placementSurfaceSize;
uniform float terrainHalfExtent;
uniform float terrainVertexSpacing;
out vec3 vertexColor;
out vec3 vertexNormal;
out vec3 worldPosition;
out vec4 materialWeights;
out float traversalClass;
out float localWaterSurfaceHeight;
out float waterCoverage;
void main() {
    vec3 position = inPosition;
    vec3 normal = inNormal;
    if (usePlacementSurface) {
        ivec2 cell = ivec2(round((position.xz + terrainHalfExtent) / terrainVertexSpacing)) - placementSurfaceOrigin;
        if (all(greaterThanEqual(cell, ivec2(0))) && all(lessThan(cell, placementSurfaceSize))) {
            vec4 surface = texelFetch(placementSurface, cell, 0);
            position.y = surface.x;
            normal = surface.yzw;
        }
    }
    gl_Position = viewProjection * vec4(position, 1.0);
    vertexColor = inColor;
    vertexNormal = normal;
    worldPosition = position;
    materialWeights = inMaterialWeights;
    traversalClass = inTraversalClass;
    localWaterSurfaceHeight = inWaterSurfaceHeight;
    waterCoverage = inWaterCoverage;
}

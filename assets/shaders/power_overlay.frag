#version 450 core
in vec3 overlayColor;
in vec2 overlayPosition;
uniform bool clipPlacementDisk;
uniform vec2 placementCenter;
uniform float placementRadius;
uniform float overlayAlpha;
out vec4 outColor;
void main() {
    if (clipPlacementDisk && distance(overlayPosition, placementCenter) > placementRadius) discard;
    outColor = vec4(overlayColor, overlayAlpha);
}

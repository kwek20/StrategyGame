#version 450 core
in vec3 overlayColor;
uniform float overlayAlpha;
out vec4 outColor;
void main() { outColor = vec4(overlayColor, overlayAlpha); }

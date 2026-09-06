#version 450 core
uniform vec3 hudColor;
out vec4 outColor;
void main() { outColor = vec4(hudColor, 1.0); }

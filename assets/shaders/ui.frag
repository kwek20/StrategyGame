#version 450 core
in vec3 color;
in vec2 uv;
uniform sampler2D uiTexture;
uniform bool useTexture;
out vec4 outputColor;
void main() { outputColor = useTexture ? texture(uiTexture, uv) * vec4(color, 1.0)
                                      : vec4(color, 1.0); }

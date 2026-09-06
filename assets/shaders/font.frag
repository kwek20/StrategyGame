#version 450 core
in vec2 uv;
in vec3 color;
uniform sampler2D atlas;
out vec4 outColor;
void main() { float alpha = texture(atlas, uv).r; outColor = vec4(color, alpha); }

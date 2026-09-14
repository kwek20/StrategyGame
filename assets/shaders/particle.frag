#version 330 core
in vec2 particleUv;
in vec4 particleColor;
uniform sampler2D particleTexture;
uniform bool premultiplyAlpha;
out vec4 fragmentColor;
void main() {
    vec4 sprite = texture(particleTexture, particleUv);
    fragmentColor = sprite * particleColor;
    if (premultiplyAlpha) fragmentColor.rgb *= fragmentColor.a;
    if (fragmentColor.a < 0.003) discard;
}

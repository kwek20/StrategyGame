#version 330 core
in vec2 particleUv;
in vec4 particleColor;
uniform sampler2D particleTexture;
uniform bool premultiplyAlpha;
uniform bool luminanceMask;
out vec4 fragmentColor;
void main() {
    vec4 sprite = texture(particleTexture, particleUv);
    if (luminanceMask) sprite = vec4(1.0,1.0,1.0,max(sprite.r,max(sprite.g,sprite.b))*sprite.a);
    fragmentColor = sprite * particleColor;
    if (premultiplyAlpha) fragmentColor.rgb *= fragmentColor.a;
    if (fragmentColor.a < 0.003) discard;
}

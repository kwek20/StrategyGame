#version 450 core
in vec3 worldPosition;
in vec3 worldNormal;
in vec2 uv;
uniform vec3 cameraPosition;
uniform vec3 materialDiffuse;
uniform float materialOpacity;
uniform bool hasBaseColorTexture;
uniform sampler2D baseColorTexture;
uniform vec2 fogRange;
uniform bool rememberedEntity;
uniform vec3 rememberedTint;
out vec4 outColor;
void main() {
    vec3 lightDirection = normalize(vec3(-0.45, 0.82, 0.35));
    vec3 normal = normalize(worldNormal);
    float diffuseAmount = max(dot(normal, lightDirection), 0.0);
    vec3 viewDirection = normalize(cameraPosition - worldPosition);
    vec3 reflected = reflect(-lightDirection, normal);
    float specularAmount = pow(max(dot(viewDirection, reflected), 0.0), 24.0);
    vec4 base = vec4(materialDiffuse, materialOpacity);
    if (hasBaseColorTexture) base *= texture(baseColorTexture, uv);
    if (base.a < 0.05) discard;
    vec3 color = base.rgb * (0.28 + diffuseAmount * 0.82) + vec3(0.12) * specularAmount;
    if (rememberedEntity) { color = mix(color, rememberedTint, 0.72); base.a *= 0.58; }
    float fog = smoothstep(fogRange.x, fogRange.y, distance(cameraPosition, worldPosition));
    outColor = vec4(mix(color, vec3(0.42, 0.66, 0.88), fog), base.a);
}

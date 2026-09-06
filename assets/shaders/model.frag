#version 450 core
in vec3 worldPosition;
in vec3 worldNormal;
in vec2 uv;
uniform vec3 cameraPosition;
uniform vec3 materialDiffuse;
uniform float materialOpacity;
uniform vec4 materialTint;
uniform float materialRoughness;
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
    float roughness = clamp(materialRoughness, 0.0, 1.0);
    float specularAmount = pow(max(dot(viewDirection, reflected), 0.0), mix(24.0, 4.0, roughness));
    vec4 base = vec4(materialDiffuse, materialOpacity);
    if (hasBaseColorTexture) base *= texture(baseColorTexture, uv);
    base *= materialTint;
    if (base.a < 0.05) discard;
    vec3 color = base.rgb * (0.28 + diffuseAmount * 0.82)
               + vec3(mix(0.12, 0.025, roughness)) * specularAmount;
    if (rememberedEntity) { color = mix(color, rememberedTint, 0.72); base.a *= 0.58; }
    float fog = smoothstep(fogRange.x,
                           fogRange.y,
                           distance(cameraPosition.xz, worldPosition.xz));
    outColor = vec4(mix(color, vec3(0.32, 0.36, 0.38), fog * 0.72), base.a);
}

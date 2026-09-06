#version 450 core
in vec3 vertexColor;
in vec3 vertexNormal;
in vec3 worldPosition;
uniform vec3 cameraPosition;
uniform vec2 fogRange;
uniform sampler2D explorationMap;
uniform bool useExploration;
out vec4 outColor;
void main() {
    vec3 lightDirection = normalize(vec3(-0.45, 0.82, 0.35));
    float diffuse = max(dot(normalize(vertexNormal), lightDirection), 0.0);
    vec3 lit = vertexColor * (0.28 + diffuse * 0.82);
    float fog = smoothstep(fogRange.x, fogRange.y, distance(cameraPosition, worldPosition));
    vec3 atmospheric = mix(lit, vec3(0.42, 0.66, 0.88), fog);
    float explored = useExploration ? texture(explorationMap, worldPosition.xz / 192.0 + 0.5).r : 1.0;
    vec3 hidden = vec3(0.008, 0.012, 0.018);
    vec3 remembered = atmospheric * 0.28;
    outColor = vec4(explored < 0.12 ? hidden : (explored < 0.75 ? remembered : atmospheric), 1.0);
}

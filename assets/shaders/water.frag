#version 450 core
in vec3 worldPosition;
in float terrainHeight;

uniform vec3 cameraPosition;
uniform float waterLevel;
uniform float timeSeconds;
uniform vec2 fogRange;
uniform sampler2D explorationMap;
uniform float explorationExtent;
uniform bool useExploration;

out vec4 outColor;

void main() {
    if (terrainHeight >= waterLevel - 0.01)
        discard;
    if (useExploration &&
        any(greaterThan(abs(worldPosition.xz), vec2(explorationExtent * 0.5))))
        discard;

    float waveA = sin(worldPosition.x * 0.19 + worldPosition.z * 0.13 + timeSeconds * 0.65);
    float waveB = sin(worldPosition.x * -0.11 + worldPosition.z * 0.23 + timeSeconds * 0.42);
    float shimmer = 0.5 + 0.5 * (waveA * 0.55 + waveB * 0.45);
    vec3 shallow = vec3(0.10, 0.42, 0.52);
    vec3 deep = vec3(0.025, 0.16, 0.29);
    vec3 water = mix(deep, shallow, 0.38 + shimmer * 0.20);

    float distanceToCamera = distance(cameraPosition.xz, worldPosition.xz);
    float fog = smoothstep(fogRange.x, fogRange.y, distanceToCamera);
    water = mix(water, vec3(0.30, 0.37, 0.42), fog * 0.70);
    float explored = useExploration
        ? texture(explorationMap, worldPosition.xz / explorationExtent + 0.5).r
        : 1.0;
    if (explored < 0.12)
        water = vec3(0.008, 0.012, 0.018);
    else if (explored < 0.75)
        water *= 0.42;
    outColor = vec4(water, explored < 0.12 ? 0.82 : 0.72);
}

#version 450 core
in vec3 worldPosition;
in float terrainHeight;
in float localWaterLevel;
in float waterCoverage;
in vec2 waterFlow;
in float generatedDepth;
in float smoothedDepth;

uniform vec3 cameraPosition;
uniform float waterLevel;
uniform float timeSeconds;
uniform vec2 fogRange;
uniform sampler2D explorationMap;
uniform float explorationExtent;
uniform bool useExploration;
uniform int waterDebugMode;

out vec4 outColor;

void main() {
    float visualDepth = localWaterLevel - terrainHeight;
    if (waterCoverage <= 0.01 || visualDepth <= 0.01)
        discard;
    if (useExploration &&
        any(greaterThan(abs(worldPosition.xz), vec2(explorationExtent * 0.5))))
        discard;

    if (waterDebugMode > 0) {
        vec2 flow = length(waterFlow) > 0.1 ? normalize(waterFlow) : vec2(0.0);
        float angle = atan(flow.y, flow.x) / 6.2831853 + 0.5;
        vec3 directionColor = 0.55 + 0.45 * cos(6.2831853 *
            (angle + vec3(0.0, 0.33, 0.67)));
        float along = dot(worldPosition.xz, flow);
        float across = dot(worldPosition.xz, vec2(-flow.y, flow.x));
        float movingStripe = smoothstep(0.68, 0.92,
            sin(along * 1.35 - timeSeconds * 2.4) * 0.5 + 0.5);
        float centerStroke = 1.0 - smoothstep(0.05, 0.24, abs(fract(across / 3.0) - 0.5));
        float flowMarker = movingStripe * centerStroke * step(0.1, length(flow));
        vec3 debugColor;
        if (waterDebugMode == 1)
            debugColor = mix(vec3(0.42, 0.08, 0.62), directionColor, 0.42);
        else if (waterDebugMode == 2)
            debugColor = mix(vec3(0.02, 0.42, 0.72), directionColor, 0.42);
        else {
            float delta = smoothedDepth - generatedDepth;
            float magnitude = clamp(abs(delta) * 3.0, 0.0, 1.0);
            debugColor = delta >= 0.0 ? mix(vec3(0.05, 0.13, 0.18),
                                            vec3(0.12, 0.95, 0.30), magnitude)
                                      : mix(vec3(0.05, 0.13, 0.18),
                                            vec3(0.98, 0.18, 0.08), magnitude);
        }
        debugColor = mix(debugColor, vec3(1.0), flowMarker * 0.8);
        outColor = vec4(debugColor, 0.94);
        return;
    }

    vec2 visualFlow = length(waterFlow) > 0.1 ? normalize(waterFlow)
                                              : normalize(vec2(0.78, 0.62));
    vec2 visualCross = vec2(-visualFlow.y, visualFlow.x);
    float phaseA = dot(worldPosition.xz, visualFlow) * 0.14 - timeSeconds * 0.72;
    float phaseB = dot(worldPosition.xz, visualCross) * 0.11 - timeSeconds * 0.47;
    float phaseC = worldPosition.x * 0.31 - worldPosition.z * 0.27 + timeSeconds * 1.18;
    float waveA = sin(phaseA);
    float waveB = sin(phaseB);
    float ripple = sin(phaseC);
    float shimmer = clamp(0.5 + waveA * 0.24 + waveB * 0.17 + ripple * 0.09, 0.0, 1.0);

    float depthFactor = smoothstep(0.06, 3.2, visualDepth);
    vec3 shorelineColor = vec3(0.18, 0.56, 0.58);
    vec3 shallowColor = vec3(0.075, 0.34, 0.46);
    vec3 deepColor = vec3(0.018, 0.105, 0.235);
    vec3 water = mix(shallowColor, deepColor, depthFactor);
    water += vec3(0.015, 0.038, 0.048) * shimmer * (1.0 - depthFactor * 0.65);

    float shoreTint = 1.0 - smoothstep(0.05, 0.72, visualDepth);
    water = mix(water, shorelineColor, shoreTint * 0.24);
    float foamBand = smoothstep(0.025, 0.11, visualDepth) *
                     (1.0 - smoothstep(0.11, 0.38, visualDepth));
    float brokenFoam = smoothstep(0.48, 0.84, shimmer + ripple * 0.10);
    float foam = foamBand * mix(0.10, 0.32, brokenFoam);
    water = mix(water, vec3(0.62, 0.78, 0.77), foam);

    float shoreFade = smoothstep(0.08, 0.85, visualDepth);
    float dhdx = (cos(phaseA) * 0.035 * 0.14 * visualFlow.x +
                  cos(phaseB) * 0.020 * 0.11 * visualCross.x) * shoreFade;
    float dhdz = (cos(phaseA) * 0.035 * 0.14 * visualFlow.y +
                  cos(phaseB) * 0.020 * 0.11 * visualCross.y) * shoreFade;
    vec3 waterNormal = normalize(vec3(-dhdx * 5.0, 1.0, -dhdz * 5.0));
    vec3 lightDirection = normalize(vec3(-0.45, 0.82, 0.35));
    vec3 viewDirection = normalize(cameraPosition - worldPosition);
    vec3 halfDirection = normalize(lightDirection + viewDirection);
    float specular = pow(max(dot(waterNormal, halfDirection), 0.0), 72.0);
    float fresnel = pow(1.0 - max(dot(waterNormal, viewDirection), 0.0), 3.0);
    water += vec3(0.72, 0.82, 0.88) * specular * (0.10 + shimmer * 0.14);
    water = mix(water, vec3(0.24, 0.35, 0.43), fresnel * 0.22);

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
    float alpha = mix(0.50, 0.82, smoothstep(0.04, 2.4, visualDepth)) * waterCoverage;
    alpha = max(alpha, foam * 0.55);
    outColor = vec4(water, explored < 0.12 ? 0.82 : alpha);
}

#version 450 core
in vec3 vertexColor;
in vec3 vertexNormal;
in vec3 worldPosition;
in vec4 materialWeights;
in float traversalClass;
uniform vec3 cameraPosition;
uniform vec2 fogRange;
uniform sampler2D explorationMap;
uniform float explorationExtent;
uniform sampler2D grassTexture;
uniform sampler2D dirtTexture;
uniform sampler2D rockTexture;
uniform sampler2D dryGroundTexture;
uniform sampler2D foundationTexture;
uniform bool useExploration;
uniform bool useFoundationTexture;
uniform bool terrainDebug;
uniform int waterDebugMode;
uniform float waterLevel;
out vec4 outColor;

float terrainHash(vec2 point) {
    return fract(sin(dot(point, vec2(127.1, 311.7))) * 43758.5453);
}

float terrainNoise(vec2 point) {
    vec2 cell = floor(point * 0.17);
    vec2 blend = fract(point * 0.17);
    blend = blend * blend * (3.0 - 2.0 * blend);
    return mix(mix(terrainHash(cell), terrainHash(cell + vec2(1.0, 0.0)), blend.x),
               mix(terrainHash(cell + vec2(0.0, 1.0)),
                   terrainHash(cell + vec2(1.0, 1.0)), blend.x),
               blend.y);
}

vec3 tiledSample(sampler2D surface, vec2 worldUv, float variation) {
    vec2 rotated = mat2(0.8, -0.6, 0.6, 0.8) * worldUv * 0.73 + vec2(13.7, 4.2);
    vec3 primary = texture(surface, worldUv).rgb;
    vec3 secondary = texture(surface, rotated).rgb;
    return mix(primary, secondary, 0.28 + variation * 0.18);
}

void main() {
    // The renderer owns terrain for the maximum map size, while a match can use a
    // smaller centered area. Sampling outside that area clamps to the outer fog
    // texel and stretches a revealed edge cell into a long strip. Clip against the
    // authoritative match extent before sampling the exploration texture.
    if (useExploration &&
        any(greaterThan(abs(worldPosition.xz), vec2(explorationExtent * 0.5)))) {
        discard;
    }

    if (terrainDebug) {
        vec2 cell = abs(fract((worldPosition.xz + vec2(explorationExtent * 0.5)) / 2.0) - 0.5);
        float grid = smoothstep(0.455, 0.495, max(cell.x, cell.y));
        vec3 traversalColor = traversalClass > 0.75
                                  ? vec3(0.88, 0.08, 0.05)
                                  : (traversalClass > 0.25
                                         ? vec3(0.95, 0.62, 0.08)
                                         : vertexColor);
        vec3 semanticColor = mix(traversalColor, traversalColor * 0.22, grid * 0.72);
        outColor = vec4(semanticColor, 1.0);
        return;
    }
    if (waterDebugMode > 0) {
        float contours = smoothstep(0.46, 0.50,
            abs(fract(worldPosition.y * 0.35) - 0.5));
        outColor = vec4(mix(vec3(0.035), vec3(0.11), contours), 1.0);
        return;
    }

    vec3 lightDirection = normalize(vec3(-0.45, 0.82, 0.35));
    float diffuse = max(dot(normalize(vertexNormal), lightDirection), 0.0);
    float variation = terrainNoise(worldPosition.xz);
    vec4 weights = max(materialWeights, vec4(0.0));
    float weightTotal = dot(weights, vec4(1.0));
    if (weightTotal > 0.0001)
        weights /= weightTotal;

    vec2 tiledUv = worldPosition.xz * 0.16;
    vec3 grass = tiledSample(grassTexture, tiledUv, variation);
    vec3 dirt = tiledSample(dirtTexture, tiledUv * 0.91, variation);
    vec3 rock = tiledSample(rockTexture, tiledUv * 0.76, variation);
    vec3 dryGround = tiledSample(dryGroundTexture, tiledUv * 0.88, variation);
    vec3 textured = grass * weights.x + dirt * weights.y + rock * weights.z +
                    dryGround * weights.w;
    if (useFoundationTexture) {
        // Foundation geometry uses the same world-space projection as terrain so
        // rotated and circular slabs tile without requiring unique UV meshes.
        textured = tiledSample(foundationTexture, worldPosition.xz * 0.22, variation);
    }

    // Keep material exposure independent of camera altitude. Strategy-camera zoom must
    // not wash the terrain toward its brighter vertex-colour fallback.
    float cameraDistance = distance(cameraPosition.xz, worldPosition.xz);
    float textureStrength = 0.82;
    vec3 surface = useFoundationTexture
                       ? textured
                       : (weightTotal > 0.0001
                              ? mix(vertexColor,
                                    textured * mix(vec3(1.0), vertexColor, 0.18),
                                    textureStrength)
                              : vertexColor);
    vec3 lit = surface * (0.46 + diffuse * 0.64);
    float fog = smoothstep(fogRange.x, fogRange.y, cameraDistance);
    vec3 atmospheric = mix(lit, vec3(0.32, 0.36, 0.38), fog * 0.72);
    float waterDepth = max(waterLevel - worldPosition.y, 0.0);
    if (waterDepth > 0.0) {
        float underwater = smoothstep(0.02, 2.8, waterDepth);
        vec3 underwaterTint = mix(vec3(0.12, 0.29, 0.31),
                                  vec3(0.025, 0.105, 0.16), underwater);
        atmospheric = mix(atmospheric, underwaterTint, 0.40 + underwater * 0.35);
    }
    float explored = useExploration
        ? texture(explorationMap, worldPosition.xz / explorationExtent + 0.5).r
        : 1.0;
    vec3 hidden = vec3(0.008, 0.012, 0.018);
    vec3 remembered = atmospheric * 0.42;
    outColor = vec4(explored < 0.12 ? hidden : (explored < 0.75 ? remembered : atmospheric), 1.0);
}

#version 450 core
in vec3 vertexColor;
in vec3 vertexNormal;
in vec3 worldPosition;
in vec4 materialWeights;
in float traversalClass;
in float localWaterSurfaceHeight;
in float waterCoverage;
uniform vec3 cameraPosition;
uniform vec2 fogRange;
uniform sampler2D explorationMap;
uniform float explorationExtent;
uniform float visibilityExtent;
uniform sampler2D grassTexture;
uniform sampler2D dirtTexture;
uniform sampler2D rockTexture;
uniform sampler2D dryGroundTexture;
uniform sampler2D crackedGroundTexture;
uniform sampler2D foundationTexture;
uniform sampler2D resourceSiteMap;
uniform bool useResourceSites;
uniform bool useExploration;
uniform bool useFoundationTexture;
uniform bool useScaffoldMaterial;
uniform float foundationProgress;
uniform bool terrainDebug;
uniform bool regionalStyle;
uniform bool boundaryPresentation;
uniform int waterDebugMode;
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
    // authoritative match extent independently of fog/debug state. Debug modes may bypass
    // exploration, but they must not expose the unused maximum-size terrain outside the match.
    if (!boundaryPresentation && any(greaterThan(abs(worldPosition.xz), vec2(explorationExtent * 0.5 + 0.001)))) {
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

    float explored = useExploration
        ? texture(explorationMap, worldPosition.xz / visibilityExtent + 0.5).r : 1.0;
    if (useScaffoldMaterial) {
        // Static member stages: progress reveals steel without moving the floor or uploading meshes.
        float reveal = smoothstep(traversalClass, traversalClass + 0.06, foundationProgress);
        float grain = fract(sin(dot(floor(worldPosition * 80.0), vec3(12.9898, 78.233, 31.417))) * 43758.5453);
        if (foundationProgress <= 0.0 || grain > reveal) discard;
        vec3 n = normalize(vertexNormal);
        vec2 uv = abs(n.y) > 0.7 ? worldPosition.xz
                  : abs(n.x) > abs(n.z) ? worldPosition.zy : worldPosition.xy;
        float wear = terrainNoise(uv * 18.0);
        vec3 metal = vertexColor * mix(0.88, 1.12, wear);
        float rust = smoothstep(0.75, 0.9, terrainNoise(uv * 5.0));
        metal = mix(metal, vec3(0.20, 0.105, 0.045), rust * 0.24);
        if (vertexColor.r > 0.4 && vertexColor.b < 0.1) {
            float stripe = step(0.5, fract((uv.x + uv.y) * 5.0));
            metal = mix(vec3(0.045), vertexColor, stripe);
        }
        vec3 light = normalize(vec3(-0.45, 0.82, 0.35));
        vec3 halfDirection = normalize(light + normalize(cameraPosition - worldPosition));
        vec3 lit = metal * (0.45 + max(dot(n, light), 0.0) * 0.65);
        lit += vec3(0.06) * pow(max(dot(n, halfDirection), 0.0), 40.0);
        float depth = max(localWaterSurfaceHeight - worldPosition.y, 0.0);
        lit = mix(lit, lit * vec3(0.38, 0.65, 0.70), waterCoverage * smoothstep(0.0, 2.8, depth) * 0.65);
        lit = pow(max(lit, vec3(0.0)), vec3(1.0 / 2.2));
        float fog = smoothstep(fogRange.x, fogRange.y, distance(cameraPosition.xz, worldPosition.xz));
        lit = mix(lit, vec3(0.32, 0.36, 0.38), fog * 0.72);
        outColor = vec4(explored < 0.12 ? vec3(0.008, 0.012, 0.018) : explored < 0.75 ? lit * 0.42 : lit, 1.0);
        return;
    }
    vec4 site = vec4(0.0);
    if (useResourceSites && regionalStyle && !useFoundationTexture && !boundaryPresentation) {
        site = texture(resourceSiteMap, worldPosition.xz / explorationExtent + 0.5);
        // Current entity changes must never reveal information through remembered terrain.
        site *= smoothstep(0.75, 0.95, explored) * (1.0 - smoothstep(0.0, 0.1, waterCoverage))
                * smoothstep(0.65, 0.95, abs(normalize(vertexNormal).y));
    }
    vec3 lightDirection = normalize(vec3(-0.45, 0.82, 0.35));
    float diffuse = max(dot(normalize(vertexNormal), lightDirection), 0.0);
    float variation = terrainNoise(worldPosition.xz);
    vec4 weights = max(materialWeights, vec4(0.0));
    float weightTotal = dot(weights, vec4(1.0));
    if (weightTotal > 0.0001)
        weights /= weightTotal;

    if (regionalStyle && !useFoundationTexture) {
        // Exposed faces show stone rather than a vertically stretched grass carpet.
        float exposed = 1.0 - smoothstep(0.15, 0.55, abs(normalize(vertexNormal).y));
        weights = mix(weights, vec4(0.0, 0.12, 0.88, 0.0), exposed);
        float soilPatch = smoothstep(0.53, 0.78, terrainNoise(worldPosition.xz * 0.42));
        float soilExposure = soilPatch * weights.x * 0.22;
        weights.x -= soilExposure;
        weights.y += soilExposure;
    }

    // Steep ramp faces also need a vertical projection: a top projection stretches
    // rock into thin grey streaks on cliffs. Walkable slopes keep the ground projection.
    vec2 surfacePosition = worldPosition.xz;
    if (!useFoundationTexture && abs(vertexNormal.y) < 0.5)
        surfacePosition = abs(vertexNormal.x) > abs(vertexNormal.z) ? worldPosition.zy : worldPosition.xy;
    vec2 tiledUv = surfacePosition * (regionalStyle ? 0.18 : 0.16);
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
    float oilWetness = 0.0;
    if (regionalStyle && !useFoundationTexture && weightTotal > 0.0001) {
        // Preserve the photographed material detail and soften the yellow-green cast.
        vec3 meadow = mix(grass, vec3(dot(grass, vec3(0.299, 0.587, 0.114))), 0.20)
                      * vec3(0.91, 1.04, 1.05);
        vec3 soil = dirt * vec3(0.94, 0.97, 1.03);
        surface = meadow * weights.x + soil * weights.y +
                  rock * vec3(0.91, 0.94, 0.96) * weights.z + dryGround * weights.w;
        float broadVariation = terrainNoise(worldPosition.xz * 0.16);
        surface *= mix(0.88, 1.12, broadVariation) * mix(0.96, 1.04, variation);
        if (max(site.r, max(site.g, site.b)) > 0.001) {
            float fragments = smoothstep(0.64, 0.86, terrainNoise(worldPosition.xz * 9.0));
            vec3 salvageSoil = soil * vec3(0.82, 0.79, 0.75) * mix(0.94, 1.03, fragments);
            surface = mix(surface, salvageSoil, site.r * 0.78);
            if (site.g > 0.001) {
                float seepPattern = terrainNoise(worldPosition.xz * 2.8);
                float oilCoverage = smoothstep(0.16, 0.77,
                    site.g + (seepPattern - 0.5) * 0.32);
                vec3 cracked = texture(crackedGroundTexture, worldPosition.xz * 0.16).rgb;
                // Transfer crack relief into the local biome's palette, rather than painting
                // a beige mud island or replacing the terrain with an opaque black stain.
                float crackDetail = clamp(dot(cracked, vec3(0.299, 0.587, 0.114)) / 0.34, 0.38, 1.45);
                vec3 localEarth = mix(surface, soil, 0.70);
                vec3 crackedEarth = mix(cracked * vec3(0.65, 0.66, 0.62),
                    localEarth * crackDetail, 0.65);
                vec3 stainedEarth = crackedEarth * vec3(0.82, 0.78, 0.69);
                surface = mix(surface, stainedEarth, oilCoverage * 0.85);
                // Wet cores are terrain material, never coplanar spill geometry. Broad,
                // world-anchored variation keeps feathered edges stable during movement.
                float wetPattern = terrainNoise(worldPosition.xz * 1.4);
                oilWetness = smoothstep(0.08, 0.72,
                    site.a + (wetPattern - 0.5) * 0.18) * oilCoverage;
                vec3 wetEarth = surface * vec3(0.76, 0.67, 0.54);
                surface = mix(surface, wetEarth, oilWetness * 0.62);
            }
            surface = mix(surface, mix(rock * 0.78, soil * 0.72, 0.35), site.b * 0.78);
        }
    }
    vec3 lit = surface * (regionalStyle ? (0.48 + diffuse * 0.66) : (0.46 + diffuse * 0.64));
    if (oilWetness > 0.001) {
        // Restrained surface sheen, without another water pass or animated geometry.
        vec3 viewDirection = normalize(cameraPosition - worldPosition);
        vec3 halfDirection = normalize(lightDirection + viewDirection);
        float sheen = pow(max(dot(normalize(vertexNormal), halfDirection), 0.0), 64.0);
        lit += vec3(0.025, 0.028, 0.025) * sheen * oilWetness;
    }
    // Material textures decode from sRGB on sampling. The world framebuffer is
    // display-encoded (UI and older passes share it), so encode V2 lighting here.
    if (regionalStyle && !useFoundationTexture)
        lit = pow(max(lit, vec3(0.0)), vec3(1.0 / 2.2));
    float fog = smoothstep(fogRange.x, fogRange.y, cameraDistance);
    vec3 atmospheric = mix(lit, vec3(0.32, 0.36, 0.38), fog * 0.72);
    float waterDepth = max(localWaterSurfaceHeight - worldPosition.y, 0.0);
    if (waterCoverage > 0.01 && waterDepth > 0.0) {
        float underwater = smoothstep(0.02, 2.8, waterDepth);
        vec3 underwaterTint = mix(vec3(0.12, 0.29, 0.31),
                                  vec3(0.025, 0.105, 0.16), underwater);
        atmospheric = mix(atmospheric, underwaterTint,
                          waterCoverage * (0.40 + underwater * 0.35));
    }
    vec3 hidden = vec3(0.008, 0.012, 0.018);
    vec3 remembered = atmospheric * 0.42;
    vec3 result = explored < 0.12 ? hidden : (explored < 0.75 ? remembered : atmospheric);
    if (boundaryPresentation) {
        float outside = max(max(abs(worldPosition.x), abs(worldPosition.z)) - explorationExtent * 0.5, 0.0);
        result = mix(result, vec3(0.32, 0.36, 0.38), smoothstep(8.0, 72.0, outside));
    }
    outColor = vec4(result, 1.0);
}

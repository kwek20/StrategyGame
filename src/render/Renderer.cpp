#include "render/Renderer.hpp"

#include "game/RtsCamera.hpp"
#include "world/World.hpp"
#include "localization/Text.hpp"

#include <glad/glad.h>
#include <SDL3/SDL.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <cmath>
#include <cstddef>
#include <array>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <chrono>
#include <cctype>

namespace strategy {
namespace {

struct TerrainVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
};

std::uint32_t compileShader(GLenum type, const char* source) {
    const std::uint32_t shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) {
        return shader;
    }

    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(length), '\0');
    glGetShaderInfoLog(shader, length, nullptr, log.data());
    glDeleteShader(shader);
    throw std::runtime_error("Shader compilation failed: " + log);
}

std::uint32_t createTerrainProgram() {
    constexpr const char* vertexSource = R"glsl(
        #version 450 core
        layout(location = 0) in vec3 inPosition;
        layout(location = 1) in vec3 inNormal;
        layout(location = 2) in vec3 inColor;
        uniform mat4 viewProjection;
        out vec3 vertexColor;
        out vec3 vertexNormal;
        out vec3 worldPosition;

        void main() {
            gl_Position = viewProjection * vec4(inPosition, 1.0);
            vertexColor = inColor;
            vertexNormal = inNormal;
            worldPosition = inPosition;
        }
    )glsl";

    constexpr const char* fragmentSource = R"glsl(
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
            float lighting = 0.28 + diffuse * 0.82;
            vec3 lit = vertexColor * lighting;
            float fog = smoothstep(fogRange.x, fogRange.y, distance(cameraPosition, worldPosition));
            vec3 atmospheric = mix(lit, vec3(0.42, 0.66, 0.88), fog);
            float explored = useExploration ? texture(explorationMap, worldPosition.xz / 192.0 + 0.5).r : 1.0;
            vec3 hidden = vec3(0.008,0.012,0.018);
            vec3 remembered = atmospheric * 0.28;
            outColor = vec4(explored < 0.12 ? hidden : (explored < 0.75 ? remembered : atmospheric), 1.0);
        }
    )glsl";

    const std::uint32_t vertex = compileShader(GL_VERTEX_SHADER, vertexSource);
    const std::uint32_t fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    const std::uint32_t program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_TRUE) {
        return program;
    }

    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(length), '\0');
    glGetProgramInfoLog(program, length, nullptr, log.data());
    glDeleteProgram(program);
    throw std::runtime_error("Shader linking failed: " + log);
}

std::uint32_t createModelProgram() {
    constexpr const char* vertexSource = R"glsl(
        #version 450 core
        layout(location = 0) in vec3 inPosition;
        layout(location = 1) in vec3 inNormal;
        layout(location = 2) in vec2 inUv;
        layout(location = 3) in ivec4 inBones;
        layout(location = 4) in vec4 inWeights;
        uniform mat4 viewProjection;
        uniform mat4 model;
        uniform bool useSkinning;
        uniform mat4 bones[100];
        out vec3 worldPosition;
        out vec3 worldNormal;
        out vec2 uv;
        void main() {
            mat4 skin = mat4(1.0);
            if (useSkinning) {
                skin = mat4(0.0);
                for (int i = 0; i < 4; ++i)
                    if (inBones[i] >= 0) skin += bones[inBones[i]] * inWeights[i];
            }
            vec4 world = model * skin * vec4(inPosition, 1.0);
            worldPosition = world.xyz;
            worldNormal = normalize(transpose(inverse(mat3(model * skin))) * inNormal);
            uv = inUv;
            gl_Position = viewProjection * world;
        }
    )glsl";
    constexpr const char* fragmentSource = R"glsl(
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
            vec3 color = base.rgb * (0.28 + diffuseAmount * 0.82)
                       + vec3(0.12) * specularAmount;
            if(rememberedEntity){color=mix(color,rememberedTint,0.72);base.a*=0.58;}
            float fog = smoothstep(fogRange.x, fogRange.y, distance(cameraPosition, worldPosition));
            outColor = vec4(mix(color, vec3(0.42, 0.66, 0.88), fog), base.a);
        }
    )glsl";
    const std::uint32_t vertex = compileShader(GL_VERTEX_SHADER, vertexSource);
    const std::uint32_t fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    const std::uint32_t result = glCreateProgram();
    glAttachShader(result, vertex);
    glAttachShader(result, fragment);
    glLinkProgram(result);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(result, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
        glDeleteProgram(result);
        throw std::runtime_error("Model shader linking failed");
    }
    return result;
}

std::uint32_t createOutlineProgram() {
    constexpr const char* vertexSource=R"glsl(
        #version 450 core
        layout(location=0) in vec3 inPosition;
        layout(location=3) in ivec4 inBones;
        layout(location=4) in vec4 inWeights;
        uniform mat4 viewProjection;
        uniform mat4 model;
        uniform bool useSkinning;
        uniform mat4 bones[100];
        void main(){
            mat4 skin=mat4(1.0);
            if(useSkinning){skin=mat4(0.0);for(int i=0;i<4;++i)if(inBones[i]>=0)skin+=bones[inBones[i]]*inWeights[i];}
            gl_Position=viewProjection*model*skin*vec4(inPosition,1.0);
        }
    )glsl";
    constexpr const char* fragmentSource=R"glsl(
        #version 450 core
        out vec4 outColor;
        void main(){outColor=vec4(1.0,0.82,0.05,1.0);}
    )glsl";
    const auto vertex=compileShader(GL_VERTEX_SHADER,vertexSource),fragment=compileShader(GL_FRAGMENT_SHADER,fragmentSource);
    const auto program=glCreateProgram();glAttachShader(program,vertex);glAttachShader(program,fragment);glLinkProgram(program);glDeleteShader(vertex);glDeleteShader(fragment);
    GLint linked=GL_FALSE;glGetProgramiv(program,GL_LINK_STATUS,&linked);if(!linked){glDeleteProgram(program);throw std::runtime_error("Outline shader linking failed");}return program;
}

std::uint32_t createHudProgram() {
    constexpr const char* vertexSource = R"glsl(
        #version 450 core
        layout(location = 0) in vec2 inPosition;
        void main() { gl_Position = vec4(inPosition, 0.0, 1.0); }
    )glsl";
    constexpr const char* fragmentSource = R"glsl(
        #version 450 core
        uniform vec3 hudColor;
        out vec4 outColor;
        void main() { outColor = vec4(hudColor, 1.0); }
    )glsl";
    const std::uint32_t vertex = compileShader(GL_VERTEX_SHADER, vertexSource);
    const std::uint32_t fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    const std::uint32_t result = glCreateProgram();
    glAttachShader(result, vertex);
    glAttachShader(result, fragment);
    glLinkProgram(result);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return result;
}

void appendHudRectangle(std::vector<glm::vec2>& vertices, float left, float top,
                        float right, float bottom, int width, int height) {
    const auto ndc = [width, height](float x, float y) {
        return glm::vec2{x / static_cast<float>(width) * 2.0F - 1.0F,
                         1.0F - y / static_cast<float>(height) * 2.0F};
    };
    const glm::vec2 topLeft = ndc(left, top);
    const glm::vec2 topRight = ndc(right, top);
    const glm::vec2 bottomLeft = ndc(left, bottom);
    const glm::vec2 bottomRight = ndc(right, bottom);
    vertices.insert(vertices.end(), {topLeft, bottomLeft, topRight,
                                     topRight, bottomLeft, bottomRight});
}

void appendHudText(std::vector<glm::vec2>& vertices, const std::string& text,
                   float pixelX, float pixelY, float scale, int width, int height) {
    (void)vertices;(void)text;(void)pixelX;(void)pixelY;(void)scale;(void)width;(void)height;
}

} // namespace

Renderer::Renderer() {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glCullFace(GL_BACK);

    program_ = createTerrainProgram();
    modelProgram_ = createModelProgram();
    outlineProgram_ = createOutlineProgram();
    hudProgram_ = createHudProgram();
    glGenVertexArrays(1, &hudVao_);
    glGenBuffers(1, &hudVbo_);
    glGenTextures(1,&explorationTexture_);glBindTexture(GL_TEXTURE_2D,explorationTexture_);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);

    constexpr int chunkSide = Terrain::chunkCellCount + 1;
    constexpr float halfExtent = static_cast<float>(Terrain::cellCount)
                               * Terrain::spacing * 0.5F;
    terrainChunks_.reserve(Terrain::chunksPerSide * Terrain::chunksPerSide);

    for (int chunkZ = 0; chunkZ < Terrain::chunksPerSide; ++chunkZ) {
        for (int chunkX = 0; chunkX < Terrain::chunksPerSide; ++chunkX) {
            std::vector<TerrainVertex> vertices;
            std::array<std::vector<std::uint32_t>, 3> lodIndices;
            vertices.reserve(chunkSide * chunkSide);
            lodIndices[0].reserve(Terrain::chunkCellCount * Terrain::chunkCellCount * 6);
            const int startX = chunkX * Terrain::chunkCellCount;
            const int startZ = chunkZ * Terrain::chunkCellCount;

            for (int localZ = 0; localZ < chunkSide; ++localZ) {
                for (int localX = 0; localX < chunkSide; ++localX) {
                    const int gridX = startX + localX;
                    const int gridZ = startZ + localZ;
                    vertices.push_back({
                        {static_cast<float>(gridX) * Terrain::spacing - halfExtent,
                         terrain_.vertexHeight(gridX, gridZ),
                         static_cast<float>(gridZ) * Terrain::spacing - halfExtent},
                        terrain_.normalAt(gridX, gridZ),
                        terrain_.colorAt(terrain_.normalizedHeight(gridX, gridZ))
                    });
                }
            }

            constexpr std::array<int, 3> lodSteps{1, 2, 4};
            for (std::size_t level = 0; level < lodSteps.size(); ++level) {
                const int step = lodSteps[level];
                for (int z = 0; z < Terrain::chunkCellCount; z += step) {
                    for (int x = 0; x < Terrain::chunkCellCount; x += step) {
                        const auto topLeft = static_cast<std::uint32_t>(z * chunkSide + x);
                        const auto bottomLeft = topLeft
                            + static_cast<std::uint32_t>(step * chunkSide);
                        lodIndices[level].insert(lodIndices[level].end(), {
                            topLeft, bottomLeft, topLeft + static_cast<std::uint32_t>(step),
                            topLeft + static_cast<std::uint32_t>(step), bottomLeft,
                            bottomLeft + static_cast<std::uint32_t>(step)
                        });
                    }
                }
            }

            TerrainChunk chunk;
            for (std::size_t level = 0; level < lodIndices.size(); ++level) {
                chunk.indexCounts[level] =
                    static_cast<std::uint32_t>(lodIndices[level].size());
            }
            const float chunkWorldSize = static_cast<float>(Terrain::chunkCellCount)
                                       * Terrain::spacing;
            chunk.center = {
                static_cast<float>(startX) * Terrain::spacing - halfExtent
                    + chunkWorldSize * 0.5F,
                Terrain::heightScale * 0.5F,
                static_cast<float>(startZ) * Terrain::spacing - halfExtent
                    + chunkWorldSize * 0.5F
            };
            chunk.radius = std::sqrt(2.0F * std::pow(chunkWorldSize * 0.5F, 2.0F)
                                     + std::pow(Terrain::heightScale * 0.5F, 2.0F));

            glGenVertexArrays(1, &chunk.vao);
            glGenBuffers(1, &chunk.vbo);
            glGenBuffers(static_cast<GLsizei>(chunk.ebos.size()), chunk.ebos.data());
            glBindVertexArray(chunk.vao);
            glBindBuffer(GL_ARRAY_BUFFER, chunk.vbo);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(vertices.size() * sizeof(TerrainVertex)),
                         vertices.data(), GL_STATIC_DRAW);
            for (std::size_t level = 0; level < lodIndices.size(); ++level) {
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunk.ebos[level]);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(lodIndices[level].size()
                                            * sizeof(std::uint32_t)),
                    lodIndices[level].data(), GL_STATIC_DRAW);
            }
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex),
                                  reinterpret_cast<void*>(offsetof(TerrainVertex, position)));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex),
                                  reinterpret_cast<void*>(offsetof(TerrainVertex, normal)));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex),
                                  reinterpret_cast<void*>(offsetof(TerrainVertex, color)));
            glBindVertexArray(0);
            terrainChunks_.push_back(chunk);
        }
    }
}

Renderer::~Renderer() {
    for (const TerrainChunk& chunk : terrainChunks_) {
        glDeleteBuffers(static_cast<GLsizei>(chunk.ebos.size()), chunk.ebos.data());
        glDeleteBuffers(1, &chunk.vbo);
        glDeleteVertexArrays(1, &chunk.vao);
    }
    glDeleteBuffers(1, &hudVbo_);
    glDeleteVertexArrays(1, &hudVao_);
    glDeleteProgram(hudProgram_);
    glDeleteProgram(modelProgram_);
    glDeleteProgram(outlineProgram_);
    glDeleteProgram(program_);
    glDeleteTextures(1,&explorationTexture_);
}

void Renderer::beginFrame(int width, int height) {
    viewportWidth_ = width;
    viewportHeight_ = height;
    glViewport(0, 0, width, height);
    glClearColor(0.42F, 0.66F, 0.88F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::drawLoadingScreen(float progress,const std::string& status) const {
    glClearColor(0.0F,0.0F,0.0F,1.0F);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    const float centerX=viewportWidth_*0.5F,centerY=viewportHeight_*0.5F;
    std::vector<glm::vec2> track,fill;
    appendHudRectangle(track,centerX-210.0F,centerY+38.0F,centerX+210.0F,centerY+58.0F,viewportWidth_,viewportHeight_);
    appendHudRectangle(fill,centerX-207.0F,centerY+41.0F,centerX-207.0F+414.0F*std::clamp(progress,0.0F,1.0F),centerY+55.0F,viewportWidth_,viewportHeight_);
    glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glUseProgram(hudProgram_);glBindVertexArray(hudVao_);glBindBuffer(GL_ARRAY_BUFFER,hudVbo_);glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(glm::vec2),nullptr);
    const auto draw=[this](const std::vector<glm::vec2>& vertices,const glm::vec3& color){glUniform3fv(glGetUniformLocation(hudProgram_,"hudColor"),1,glm::value_ptr(color));glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(vertices.size()*sizeof(glm::vec2)),vertices.data(),GL_DYNAMIC_DRAW);glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(vertices.size()));};draw(track,{0.10F,0.13F,0.16F});draw(fill,{0.18F,0.76F,0.88F});
    const std::string title=Text::get("loading.title");drawText(title,centerX-title.size()*9.0F,centerY-35.0F,3.0F);drawText(status,centerX-status.size()*4.8F,centerY+5.0F,1.4F,{0.65F,0.74F,0.78F});
    glBindVertexArray(0);glEnable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);
}

void Renderer::drawText(const std::string& text,float x,float y,float scale,
                        const glm::vec3& color) const {
    font_.draw(text,x,y,std::max(scale*8.0F,13.0F),color,viewportWidth_,viewportHeight_);
}

void Renderer::drawTerrain(const CameraView& camera,const Player* player) const {
    const glm::vec3 focus = camera.target;
    const glm::mat4 viewProjection = camera.viewProjection();

    glUseProgram(program_);
    const GLint location = glGetUniformLocation(program_, "viewProjection");
    glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(viewProjection));
    glUniform3fv(glGetUniformLocation(program_, "cameraPosition"),1,glm::value_ptr(camera.position));
    const bool closeView=camera.detailDistance<20.0F;
    glUniform2f(glGetUniformLocation(program_,"fogRange"),closeView?75.0F:125.0F,closeView?175.0F:260.0F);
    glUniform1i(glGetUniformLocation(program_,"useExploration"),player?1:0);
    if(player) {
        std::vector<std::uint8_t> map(player->discovered.size());
        for(std::size_t i=0;i<map.size();++i)map[i]=player->visible[i]?255:(player->discovered[i]?90:0);
        glActiveTexture(GL_TEXTURE7);glBindTexture(GL_TEXTURE_2D,explorationTexture_);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        glTexImage2D(GL_TEXTURE_2D,0,GL_R8,Player::explorationCells,Player::explorationCells,0,GL_RED,GL_UNSIGNED_BYTE,map.data());
        glUniform1i(glGetUniformLocation(program_,"explorationMap"),7);glActiveTexture(GL_TEXTURE0);
    }
    constexpr float renderDistance = 190.0F;
    for (const TerrainChunk& chunk : terrainChunks_) {
        const float dx = chunk.center.x - focus.x;
        const float dz = chunk.center.z - focus.z;
        const float maximumDistance = renderDistance + chunk.radius;
        if (dx * dx + dz * dz > maximumDistance * maximumDistance) {
            continue;
        }
        std::size_t lodLevel;
        if(closeView) {
            const float cameraDx=chunk.center.x-camera.position.x;
            const float cameraDz=chunk.center.z-camera.position.z;
            const float distanceSquared=cameraDx*cameraDx+cameraDz*cameraDz;
            lodLevel=distanceSquared<55.0F*55.0F?0U:distanceSquared<105.0F*105.0F?1U:2U;
        } else {
            lodLevel=camera.detailDistance<=36.0F?0U:camera.detailDistance<=76.0F?1U:2U;
        }
        glBindVertexArray(chunk.vao);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunk.ebos[lodLevel]);
        glDrawElements(GL_TRIANGLES,
                       static_cast<GLsizei>(chunk.indexCounts[lodLevel]),
                       GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);
}

void Renderer::drawWorld(const World& world, const CameraView& camera,const Player* player) const {
    const glm::mat4 viewProjection = camera.viewProjection();
    glUseProgram(modelProgram_);
    glUniform3fv(glGetUniformLocation(modelProgram_, "cameraPosition"), 1,
                 glm::value_ptr(camera.position));
    const bool closeView=camera.detailDistance<20.0F;
    glUniform2f(glGetUniformLocation(modelProgram_,"fogRange"),closeView?75.0F:125.0F,closeView?175.0F:260.0F);
    glUniform1i(glGetUniformLocation(modelProgram_,"rememberedEntity"),0);

    for (const Entity& entity : world.entities()) {
        if(entity.resource&&entity.resource.remaining<=0.0F)continue;
        if(player&&entity.authority.owner!=player->id) {
            constexpr float extent=Terrain::cellCount*Terrain::spacing;
            const int x=std::clamp(static_cast<int>((entity.transform.position.x/extent+0.5F)*Player::explorationCells),0,Player::explorationCells-1);
            const int z=std::clamp(static_cast<int>((entity.transform.position.z/extent+0.5F)*Player::explorationCells),0,Player::explorationCells-1);
            if(!player->visible[static_cast<std::size_t>(z*Player::explorationCells+x)])continue;
        }
        const Model* model = resources_.model(entity.modelKey);
        if (model == nullptr) {
            continue;
        }
        const float terrainHeight = terrain_.heightAt(entity.transform.position.x,
                                                       entity.transform.position.z);
        glm::mat4 transform{1.0F};
        transform = glm::translate(transform,
            glm::vec3{entity.transform.position.x,
                      terrainHeight + entity.transform.position.y,
                      entity.transform.position.z});
        transform = glm::rotate(transform, glm::radians(entity.transform.rotationDegrees.x),
                                {1.0F, 0.0F, 0.0F});
        transform = glm::rotate(transform, glm::radians(entity.transform.rotationDegrees.y),
                                {0.0F, 1.0F, 0.0F});
        transform = glm::rotate(transform, glm::radians(entity.transform.rotationDegrees.z),
                                {0.0F, 0.0F, 1.0F});
        float catalogueScale = 1.0F;
        std::string animation;
        if (const EntityDefinition* definition = resources_.entityDefinition(entity.modelKey)) {
            catalogueScale = definition->scale;
            const bool moving = entity.authority.directController != 0
                ? glm::length(entity.unitControl.directInput) > 0.01F
                : entity.unitControl.hasStrategicDestination;
            const std::string animationState = moving
                ? (entity.unitControl.running ? "run" : "walk") : "idle";
            const auto selected = definition->animations.find(animationState);
            if (selected != definition->animations.end()) animation = selected->second;
        }
        transform = glm::scale(transform, entity.transform.scale * catalogueScale);
        const double animationSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        model->draw(modelProgram_, viewProjection, transform, animation, animationSeconds);
    }
    if(player)for(const LastKnownEntity& known:player->intelligence) {
        constexpr float extent=Terrain::cellCount*Terrain::spacing;
        const int x=std::clamp(static_cast<int>((known.position.x/extent+0.5F)*Player::explorationCells),0,Player::explorationCells-1),z=std::clamp(static_cast<int>((known.position.z/extent+0.5F)*Player::explorationCells),0,Player::explorationCells-1);
        if(player->visible[static_cast<std::size_t>(z*Player::explorationCells+x)])continue;
        const Model* model=resources_.model(known.modelKey);if(!model)continue;
        glm::mat4 transform{1.0F};transform=glm::translate(transform,{known.position.x,terrain_.heightAt(known.position.x,known.position.z)+known.position.y,known.position.z});transform=glm::rotate(transform,glm::radians(known.rotationDegrees.x),{1,0,0});transform=glm::rotate(transform,glm::radians(known.rotationDegrees.y),{0,1,0});transform=glm::rotate(transform,glm::radians(known.rotationDegrees.z),{0,0,1});float catalogueScale=1.0F;if(const EntityDefinition* definition=resources_.entityDefinition(known.modelKey))catalogueScale=definition->scale;transform=glm::scale(transform,known.scale*catalogueScale);
        glUseProgram(modelProgram_);glUniform1i(glGetUniformLocation(modelProgram_,"rememberedEntity"),1);const glm::vec3 tint=known.building?glm::vec3{0.22F,0.34F,0.40F}:glm::vec3{0.27F,0.29F,0.31F};glUniform3fv(glGetUniformLocation(modelProgram_,"rememberedTint"),1,glm::value_ptr(tint));model->draw(modelProgram_,viewProjection,transform,"",0.0);glUniform1i(glGetUniformLocation(modelProgram_,"rememberedEntity"),0);
    }
    std::vector<glm::vec2> healthBack,healthDamage,healthRemaining;
    for(const Entity& entity:world.entities()) {
        if(!entity.health||entity.health.current<=0.0F||entity.health.maximum<=0.0F
            ||entity.health.current>=entity.health.maximum-0.001F)continue;
        if(player&&entity.authority.owner!=player->id) {
            constexpr float extent=Terrain::cellCount*Terrain::spacing;
            const int x=std::clamp(static_cast<int>((entity.transform.position.x/extent+0.5F)*Player::explorationCells),0,Player::explorationCells-1),z=std::clamp(static_cast<int>((entity.transform.position.z/extent+0.5F)*Player::explorationCells),0,Player::explorationCells-1);
            if(!player->visible[static_cast<std::size_t>(z*Player::explorationCells+x)])continue;
        }
        const EntityDefinition* definition=resources_.entityDefinition(entity.modelKey);
        const float height=definition?definition->selectionHeight:2.0F;
        const float ground=terrain_.heightAt(entity.transform.position.x,entity.transform.position.z);
        const glm::vec4 clip=viewProjection*glm::vec4{entity.transform.position.x,ground+entity.transform.position.y+height+0.65F,entity.transform.position.z,1.0F};
        if(clip.w<=0.0F)continue;const glm::vec3 ndc=glm::vec3(clip)/clip.w;if(ndc.z<-1.0F||ndc.z>1.0F)continue;
        const float screenX=(ndc.x+1.0F)*0.5F*viewportWidth_,screenY=(1.0F-ndc.y)*0.5F*viewportHeight_;
        if(screenX<0.0F||screenX>viewportWidth_||screenY<0.0F||screenY>viewportHeight_)continue;
        const float width=entity.unitControl?48.0F:72.0F,left=screenX-width*0.5F,top=screenY-4.0F;
        const float ratio=std::clamp(entity.health.current/entity.health.maximum,0.0F,1.0F);
        appendHudRectangle(healthBack,left-2.0F,top-2.0F,left+width+2.0F,top+8.0F,viewportWidth_,viewportHeight_);
        appendHudRectangle(healthDamage,left,top,left+width,top+6.0F,viewportWidth_,viewportHeight_);
        appendHudRectangle(healthRemaining,left,top,left+width*ratio,top+6.0F,viewportWidth_,viewportHeight_);
    }
    if(!healthBack.empty()) {
        glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glUseProgram(hudProgram_);glBindVertexArray(hudVao_);glBindBuffer(GL_ARRAY_BUFFER,hudVbo_);glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(glm::vec2),nullptr);
        const auto draw=[this](const std::vector<glm::vec2>& vertices,const glm::vec3& color){glUniform3fv(glGetUniformLocation(hudProgram_,"hudColor"),1,glm::value_ptr(color));glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(vertices.size()*sizeof(glm::vec2)),vertices.data(),GL_DYNAMIC_DRAW);glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(vertices.size()));};
        draw(healthBack,{0.01F,0.015F,0.02F});draw(healthDamage,{0.55F,0.07F,0.05F});draw(healthRemaining,{0.16F,0.78F,0.20F});glBindVertexArray(0);glEnable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);
    }
}

void Renderer::drawDebugHud(const RtsCamera& camera, std::size_t entityCount) const {
    std::vector<glm::vec2> vertices;
    std::ostringstream fps;
    fps << std::fixed << std::setprecision(1) << framesPerSecond_;
    std::ostringstream cameraLine;
    cameraLine << std::fixed << std::setprecision(1) << camera.focus().x;
    std::ostringstream cameraZ; cameraZ << std::fixed << std::setprecision(1) << camera.focus().z;
    std::ostringstream counts;
    counts << entityCount;
    appendHudText(vertices, Text::format("hud.fps", {fps.str()}), 14.0F, 14.0F, 2.0F,
                  viewportWidth_, viewportHeight_);
    appendHudText(vertices, Text::format("hud.camera", {cameraLine.str(),cameraZ.str()}), 14.0F, 34.0F, 2.0F,
                  viewportWidth_, viewportHeight_);
    appendHudText(vertices, Text::format("hud.counts", {counts.str(),std::to_string(resources_.modelCount())}), 14.0F, 54.0F, 2.0F,
                  viewportWidth_, viewportHeight_);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(hudProgram_);
    glUniform3f(glGetUniformLocation(hudProgram_, "hudColor"), 0.95F, 0.98F, 0.82F);
    glBindVertexArray(hudVao_);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(glm::vec2)),
                 vertices.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    glBindVertexArray(0);
    drawText(Text::format("hud.fps",{fps.str()}),14.0F,14.0F,2.0F);
    drawText(Text::format("hud.camera",{cameraLine.str(),cameraZ.str()}),14.0F,34.0F,2.0F);
    drawText(Text::format("hud.counts",{counts.str(),std::to_string(resources_.modelCount())}),14.0F,54.0F,2.0F);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawVisionRanges(const CameraView& camera,const Entity* entity) const {
    if(!entity||!entity->unitControl||!entity->vision)return;
    const float elevation=terrain_.heightAt(entity->transform.position.x,entity->transform.position.z);
    const float base=entity->vision.sightRange;
    const float effective=effectiveSightRange(base,elevation);
    constexpr int segments=128;
    const auto drawCircle=[&](float radius,glm::vec3 color,float heightOffset){
        std::vector<TerrainVertex> vertices;vertices.reserve(segments);
        for(int index=0;index<segments;++index){const float angle=glm::two_pi<float>()*static_cast<float>(index)/segments;const float x=entity->transform.position.x+std::cos(angle)*radius,z=entity->transform.position.z+std::sin(angle)*radius;vertices.push_back({{x,terrain_.heightAt(x,z)+heightOffset,z},{0,1,0},color});}
        glUseProgram(program_);const glm::mat4 viewProjection=camera.viewProjection();glUniformMatrix4fv(glGetUniformLocation(program_,"viewProjection"),1,GL_FALSE,glm::value_ptr(viewProjection));glUniform3fv(glGetUniformLocation(program_,"cameraPosition"),1,glm::value_ptr(camera.position));glUniform2f(glGetUniformLocation(program_,"fogRange"),10000.0F,10001.0F);glUniform1i(glGetUniformLocation(program_,"useExploration"),0);
        glBindVertexArray(hudVao_);glBindBuffer(GL_ARRAY_BUFFER,hudVbo_);glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(vertices.size()*sizeof(TerrainVertex)),vertices.data(),GL_DYNAMIC_DRAW);glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);glEnableVertexAttribArray(2);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(TerrainVertex),reinterpret_cast<void*>(offsetof(TerrainVertex,position)));glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(TerrainVertex),reinterpret_cast<void*>(offsetof(TerrainVertex,normal)));glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,sizeof(TerrainVertex),reinterpret_cast<void*>(offsetof(TerrainVertex,color)));glLineWidth(3.0F);glDrawArrays(GL_LINE_LOOP,0,segments);
    };
    drawCircle(base,{0.10F,0.78F,1.0F},0.18F);
    drawCircle(effective,{1.0F,0.72F,0.08F},0.24F);
    glLineWidth(1.0F);glDisableVertexAttribArray(1);glDisableVertexAttribArray(2);glBindVertexArray(0);
}

void Renderer::drawDetailedDebugHud(const CameraView& camera,const Entity* entity,
                                    const Player* player,std::uint32_t seed,
                                    std::uint64_t tick,std::size_t entityCount) const {
    (void)player;const auto number=[](float value){std::ostringstream stream;stream<<std::fixed<<std::setprecision(3)<<value;return stream.str();};const auto boolean=[](bool value){return value?"true":"false";};const auto vector=[&](glm::vec3 value){return "["+number(value.x)+", "+number(value.y)+", "+number(value.z)+"]";};
    std::vector<std::string> lines{Text::get("debug.title"),Text::format("debug.camera",{number(camera.position.x),number(camera.position.y),number(camera.position.z)}),Text::format("debug.target",{number(camera.target.x),number(camera.target.y),number(camera.target.z)}),Text::format("debug.world",{std::to_string(seed),std::to_string(tick)}),Text::format("debug.performance",{number(framesPerSecond_),std::to_string(entityCount),std::to_string(resources_.modelCount())})};
    if(entity){const char* kinds[]={"decoration","unit","building","resource"};lines.push_back("ENTITY (saved component state)");lines.push_back("Identity");lines.push_back("  id: "+std::to_string(entity->id));lines.push_back("  archetype: "+entity->modelKey);lines.push_back("  kind: "+std::string(kinds[static_cast<unsigned>(entity->kind)]));lines.push_back("  owner: "+std::to_string(entity->authority.owner));lines.push_back("Transform");lines.push_back("  position: "+vector(entity->transform.position));lines.push_back("  rotation: "+vector(entity->transform.rotationDegrees));lines.push_back("  scale: "+vector(entity->transform.scale));if(entity->health){lines.push_back("Health");lines.push_back("  current [saved]: "+number(entity->health.current));lines.push_back("  maximum [saved]: "+number(entity->health.maximum));}if(entity->vision){lines.push_back("Vision");lines.push_back("  range [resolved]: "+number(entity->vision.sightRange));}if(entity->unitControl){lines.push_back("Unit");lines.push_back("  controller [saved]: "+std::to_string(entity->authority.directController));lines.push_back("  order [saved]: "+std::to_string(static_cast<unsigned>(entity->unitControl.order)));lines.push_back("  orderTarget [saved]: "+std::to_string(entity->unitControl.orderTarget));lines.push_back("  movementSpeed [resolved]: "+number(entity->unitControl.movementSpeed));}if(entity->gatherer){lines.push_back("Gatherer");lines.push_back("  carriedKind [saved]: "+std::to_string(static_cast<unsigned>(entity->unitControl.carriedKind)));lines.push_back("  carriedAmount [saved]: "+number(entity->unitControl.carriedAmount));lines.push_back("  capacity [resolved]: "+number(entity->gatherer.carryCapacity));lines.push_back("  rate [resolved]: "+number(entity->gatherer.gatherPerSecond));}if(entity->combat){lines.push_back("Combat");lines.push_back("  damage [resolved]: "+number(entity->combat.damage));lines.push_back("  range [resolved]: "+number(entity->combat.range));}if(entity->resource){lines.push_back("Resource");lines.push_back("  kind [saved]: "+std::to_string(static_cast<unsigned>(entity->resource.kind)));lines.push_back("  remaining [saved]: "+number(entity->resource.remaining));}if(entity->production){lines.push_back("Production");lines.push_back("  speedMultiplier [saved]: "+number(entity->production.productionSpeedMultiplier));lines.push_back("  speedUpgrades [saved]: "+std::to_string(entity->production.productionSpeedUpgrades));lines.push_back("  queue [saved]: "+std::to_string(entity->production.queue.size()));}if(entity->buildingUpgrades){lines.push_back("Building upgrades");lines.push_back("  level [saved]: "+std::to_string(entity->buildingUpgrades.level));}if(entity->upgrades){lines.push_back("Selectable upgrades");if(entity->upgrades.levels.empty())lines.push_back("  none configured");for(const auto& [id,level]:entity->upgrades.levels)lines.push_back("  "+id+": "+std::to_string(level));}goto component_debug_ready;}
    if(entity){lines.push_back("ENTITY SAVE RECORD");lines.push_back("id: "+std::to_string(entity->id));lines.push_back("name: \""+entity->name+"\"");lines.push_back("model: \""+entity->modelKey+"\"");lines.push_back("owner: "+std::to_string(entity->authority.owner));lines.push_back("directController: "+std::to_string(entity->authority.directController));lines.push_back("position: "+vector(entity->transform.position));lines.push_back("rotation: "+vector(entity->transform.rotationDegrees));lines.push_back("scale: "+vector(entity->transform.scale));lines.push_back("directlyControllable: "+std::string(boolean(entity->unitControl.directlyControllable)));lines.push_back("directInput: ["+number(entity->unitControl.directInput.x)+", "+number(entity->unitControl.directInput.y)+"]");lines.push_back("running: "+std::string(boolean(entity->unitControl.running)));lines.push_back("strategicDestination: "+vector(entity->unitControl.strategicDestination));lines.push_back("hasStrategicDestination: "+std::string(boolean(entity->unitControl.hasStrategicDestination)));lines.push_back("movementSpeed: "+number(entity->unitControl.movementSpeed));lines.push_back("sightRange: "+number(entity->unitControl.sightRange));lines.push_back("unitOrder: "+std::to_string(static_cast<unsigned>(entity->unitControl.order)));lines.push_back("orderTarget: "+std::to_string(entity->unitControl.orderTarget));lines.push_back("carriedKind: "+std::to_string(static_cast<unsigned>(entity->unitControl.carriedKind)));lines.push_back("carriedAmount: "+number(entity->unitControl.carriedAmount));lines.push_back("carryCapacity: "+number(entity->unitControl.carryCapacity));lines.push_back("gatherPerSecond: "+number(entity->unitControl.gatherPerSecond));lines.push_back("health: "+number(entity->health.current));lines.push_back("maximumHealth: "+number(entity->health.maximum));lines.push_back("resourceKind: "+std::to_string(static_cast<unsigned>(entity->resource.kind)));lines.push_back("resourceRemaining: "+number(entity->resource.remaining));lines.push_back("townHallLevel: "+std::to_string(entity->production.level));lines.push_back("characterBuildSeconds: "+number(entity->production.characterBuildSeconds));lines.push_back("productionQueue: ["+std::to_string(entity->production.queue.size())+"]");std::size_t queueIndex=0;for(const ProductionOrder& order:entity->production.queue)lines.push_back("  ["+std::to_string(queueIndex++)+"] kind="+std::to_string(static_cast<unsigned>(order.kind))+" durationSeconds="+number(order.durationSeconds)+" remainingSeconds="+number(order.remainingSeconds));}else lines.push_back(Text::get("debug.no_entity"));
component_debug_ready:
    if(entity&&entity->vision){const float elevation=terrain_.heightAt(entity->transform.position.x,entity->transform.position.z);lines.push_back("  terrainHeight: "+number(elevation));lines.push_back("  effectiveRange [height modified]: "+number(effectiveSightRange(entity->vision.sightRange,elevation)));lines.push_back("  circles: cyan=unit range, yellow=height modified");}
    const float left=10.0F,top=50.0F,right=std::min(static_cast<float>(viewportWidth_)-12.0F,790.0F),bottom=std::min(static_cast<float>(viewportHeight_)-12.0F,top+18.0F+static_cast<float>(lines.size())*15.0F);
    std::vector<glm::vec2> panel;
    appendHudRectangle(panel,left,top,right,bottom,viewportWidth_,viewportHeight_);
    glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glUseProgram(hudProgram_);glUniform3f(glGetUniformLocation(hudProgram_,"hudColor"),0.018F,0.028F,0.038F);
    glBindVertexArray(hudVao_);glBindBuffer(GL_ARRAY_BUFFER,hudVbo_);glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(panel.size()*sizeof(glm::vec2)),panel.data(),GL_DYNAMIC_DRAW);glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(glm::vec2),nullptr);glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(panel.size()));
    for(std::size_t index=0;index<lines.size();++index)if(top+8.0F+index*15.0F<bottom-10.0F)drawText(lines[index],left+10.0F,top+6.0F+index*15.0F,1.15F,index==0?glm::vec3{0.20F,0.82F,0.94F}:glm::vec3{0.88F,0.94F,0.86F});
    glBindVertexArray(0);glEnable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);
}

void Renderer::drawResourceHud(const Player& player) const {std::vector<glm::vec2> panel;appendHudRectangle(panel,10.0F,10.0F,430.0F,44.0F,viewportWidth_,viewportHeight_);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glUseProgram(hudProgram_);glUniform3f(glGetUniformLocation(hudProgram_,"hudColor"),0.018F,0.028F,0.038F);glBindVertexArray(hudVao_);glBindBuffer(GL_ARRAY_BUFFER,hudVbo_);glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(panel.size()*sizeof(glm::vec2)),panel.data(),GL_DYNAMIC_DRAW);glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(glm::vec2),nullptr);glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(panel.size()));drawText(Text::format("strategy.resources",{std::to_string(static_cast<unsigned>(player.wood)),std::to_string(static_cast<unsigned>(player.stone)),std::to_string(static_cast<unsigned>(player.gold))}),20.0F,17.0F,1.45F);glBindVertexArray(0);glEnable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);}

void Renderer::drawStartMenu(bool startHovered, bool buildHovered, bool loadHovered,
                             bool settingsHovered,bool exitHovered, bool seedFocused,
                             const std::string& seedText,const std::string& playerOneCountry,
                             const std::string& playerTwoCountry) const {
    const auto drawVertices = [this](const std::vector<glm::vec2>& vertices,
                                     const glm::vec3& color) {
        glUniform3fv(glGetUniformLocation(hudProgram_, "hudColor"), 1,
                     glm::value_ptr(color));
        glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(vertices.size() * sizeof(glm::vec2)),
                     vertices.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    };

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(hudProgram_);
    glBindVertexArray(hudVao_);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);

    std::vector<glm::vec2> panel;
    appendHudRectangle(panel, 30.0F, 70.0F, 800.0F, 625.0F,
                       viewportWidth_, viewportHeight_);
    drawVertices(panel, {0.035F, 0.055F, 0.075F});

    std::vector<glm::vec2> seedField;
    appendHudRectangle(seedField, 60.0F, 185.0F, 300.0F, 230.0F,
                       viewportWidth_, viewportHeight_);
    drawVertices(seedField, seedFocused ? glm::vec3{0.22F, 0.34F, 0.46F}
                                        : glm::vec3{0.10F, 0.16F, 0.22F});

    std::vector<glm::vec2> startButton;
    appendHudRectangle(startButton, 60.0F, 250.0F, 300.0F, 310.0F,
                       viewportWidth_, viewportHeight_);
    drawVertices(startButton, startHovered ? glm::vec3{0.28F, 0.62F, 0.24F}
                                           : glm::vec3{0.16F, 0.36F, 0.18F});

    std::vector<glm::vec2> buildButton;
    appendHudRectangle(buildButton, 60.0F, 320.0F, 300.0F, 380.0F,
                       viewportWidth_, viewportHeight_);
    drawVertices(buildButton, buildHovered ? glm::vec3{0.25F, 0.48F, 0.70F}
                                           : glm::vec3{0.14F, 0.27F, 0.40F});

    std::vector<glm::vec2> loadButton;
    appendHudRectangle(loadButton, 60.0F, 390.0F, 300.0F, 450.0F,
                       viewportWidth_, viewportHeight_);
    drawVertices(loadButton, loadHovered ? glm::vec3{0.54F, 0.46F, 0.20F}
                                         : glm::vec3{0.31F, 0.27F, 0.13F});

    std::vector<glm::vec2> settingsButton;appendHudRectangle(settingsButton,60,460,300,520,viewportWidth_,viewportHeight_);drawVertices(settingsButton,settingsHovered?glm::vec3{0.30F,0.48F,0.65F}:glm::vec3{0.14F,0.25F,0.36F});
    std::vector<glm::vec2> exitButton;
    appendHudRectangle(exitButton, 60.0F, 530.0F, 300.0F, 590.0F,
                       viewportWidth_, viewportHeight_);
    drawVertices(exitButton, exitHovered ? glm::vec3{0.72F, 0.25F, 0.20F}
                                         : glm::vec3{0.40F, 0.16F, 0.14F});
    std::vector<glm::vec2> countryFields;
    appendHudRectangle(countryFields,380.0F,185.0F,760.0F,230.0F,viewportWidth_,viewportHeight_);
    appendHudRectangle(countryFields,380.0F,250.0F,760.0F,295.0F,viewportWidth_,viewportHeight_);
    drawVertices(countryFields,{0.10F,0.16F,0.22F});

    std::vector<glm::vec2> text;
    appendHudText(text, Text::get("menu.title"), 60.0F, 115.0F, 3.0F,
                  viewportWidth_, viewportHeight_);
    appendHudText(text, Text::get("menu.seed"), 60.0F, 163.0F, 2.0F,
                  viewportWidth_, viewportHeight_);
    appendHudText(text, seedText.empty() ? "0" : seedText, 72.0F, 198.0F, 2.0F,
                  viewportWidth_, viewportHeight_);
    appendHudText(text, Text::get("menu.start"), 125.0F, 269.0F, 3.0F,
                  viewportWidth_, viewportHeight_);
    appendHudText(text, Text::get("menu.build"), 125.0F, 339.0F, 3.0F,
                  viewportWidth_, viewportHeight_);
    appendHudText(text, Text::get("menu.load"), 134.0F, 409.0F, 3.0F,
                  viewportWidth_, viewportHeight_);
    appendHudText(text, Text::get("menu.settings"),100.0F,479.0F,3.0F,viewportWidth_,viewportHeight_);
    appendHudText(text, Text::get("menu.exit"), 134.0F, 549.0F, 3.0F,
                  viewportWidth_, viewportHeight_);
    drawVertices(text, {0.95F, 0.98F, 0.82F});
    drawText(Text::get("menu.title"),60.0F,108.0F,3.0F);
    drawText(Text::get("menu.seed"),60.0F,157.0F,2.0F);
    drawText(seedText.empty()?"0":seedText,72.0F,192.0F,2.0F);
    drawText(Text::get("menu.start"),125.0F,261.0F,3.0F);
    drawText(Text::get("menu.build"),125.0F,331.0F,3.0F);
    drawText(Text::get("menu.load"),134.0F,401.0F,3.0F);
    drawText(Text::get("menu.settings"),100.0F,471.0F,3.0F);
    drawText(Text::get("menu.exit"),134.0F,541.0F,3.0F);
    drawText(Text::get("menu.team_a_country"),380.0F,157.0F,2.0F);
    drawText("<  "+playerOneCountry+"  >",400.0F,192.0F,2.0F);
    drawText(Text::get("menu.team_b_country"),380.0F,222.0F,2.0F);
    drawText("<  "+playerTwoCountry+"  >",400.0F,257.0F,2.0F);
    drawText(Text::get("menu.country_controls"),380.0F,312.0F,1.5F);

    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawSettings(const GameConfig& config,std::size_t resolution,int hovered,int binding)const{
    static constexpr std::array<std::pair<int,int>,4> sizes{{{1280,720},{1600,900},{1920,1080},{2560,1440}}};static constexpr const char* actions[]={"FORWARD","BACKWARD","LEFT","RIGHT","DEBUG","PAUSE"};static constexpr const char* ids[]={"forward","backward","left","right","debug","pause"};
    glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glUseProgram(hudProgram_);glBindVertexArray(hudVao_);glBindBuffer(GL_ARRAY_BUFFER,hudVbo_);glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(glm::vec2),nullptr);const auto box=[&](int row,glm::vec3 color){std::vector<glm::vec2> vertices;appendHudRectangle(vertices,60,120+row*45,700,158+row*45,viewportWidth_,viewportHeight_);glUniform3fv(glGetUniformLocation(hudProgram_,"hudColor"),1,glm::value_ptr(color));glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(glm::vec2),vertices.data(),GL_DYNAMIC_DRAW);glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(vertices.size()));};
    std::vector<glm::vec2> panel;appendHudRectangle(panel,30,45,740,680,viewportWidth_,viewportHeight_);glUniform3f(glGetUniformLocation(hudProgram_,"hudColor"),0.035F,0.055F,0.075F);glBufferData(GL_ARRAY_BUFFER,panel.size()*sizeof(glm::vec2),panel.data(),GL_DYNAMIC_DRAW);glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(panel.size()));for(int row=0;row<12;++row)box(row,row==hovered?glm::vec3{0.22F,0.38F,0.52F}:glm::vec3{0.10F,0.16F,0.22F});
    drawText(Text::get("settings.title"),60,70,3.0F);drawText("RESOLUTION     "+std::to_string(sizes[resolution].first)+" x "+std::to_string(sizes[resolution].second),80,130,1.5F);drawText(std::string("FULLSCREEN     ")+(config.fullscreen?"ON":"OFF"),80,175,1.5F);drawText("VOLUME -       "+std::to_string(static_cast<int>(config.masterVolume*100))+"%",80,220,1.5F);drawText("VOLUME +       "+std::to_string(static_cast<int>(config.masterVolume*100))+"%",80,265,1.5F);for(int i=0;i<6;++i){const auto found=config.keybinds.find(ids[i]);const SDL_Keycode key=found==config.keybinds.end()?0:found->second;drawText(std::string(actions[i])+"     "+(binding==i?"PRESS A KEY":SDL_GetKeyName(key)),80,310+i*45,1.5F);}drawText("APPLY AND BACK",80,580,1.5F);drawText("CANCEL",80,625,1.5F);glBindVertexArray(0);glEnable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);
}

void Renderer::regenerateTerrain(std::uint32_t seed) {
    terrain_ = Terrain(seed);
    constexpr int chunkSide = Terrain::chunkCellCount + 1;
    constexpr float halfExtent = static_cast<float>(Terrain::cellCount)
                               * Terrain::spacing * 0.5F;

    for (int chunkZ = 0; chunkZ < Terrain::chunksPerSide; ++chunkZ) {
        for (int chunkX = 0; chunkX < Terrain::chunksPerSide; ++chunkX) {
            std::vector<TerrainVertex> vertices;
            vertices.reserve(chunkSide * chunkSide);
            const int startX = chunkX * Terrain::chunkCellCount;
            const int startZ = chunkZ * Terrain::chunkCellCount;
            for (int localZ = 0; localZ < chunkSide; ++localZ) {
                for (int localX = 0; localX < chunkSide; ++localX) {
                    const int gridX = startX + localX;
                    const int gridZ = startZ + localZ;
                    vertices.push_back({
                        {static_cast<float>(gridX) * Terrain::spacing - halfExtent,
                         terrain_.vertexHeight(gridX, gridZ),
                         static_cast<float>(gridZ) * Terrain::spacing - halfExtent},
                        terrain_.normalAt(gridX, gridZ),
                        terrain_.colorAt(terrain_.normalizedHeight(gridX, gridZ))
                    });
                }
            }
            const std::size_t chunkIndex = static_cast<std::size_t>(
                chunkZ * Terrain::chunksPerSide + chunkX);
            glBindBuffer(GL_ARRAY_BUFFER, terrainChunks_[chunkIndex].vbo);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(vertices.size() * sizeof(TerrainVertex)),
                         vertices.data(), GL_STATIC_DRAW);
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Renderer::drawPauseMenu(bool resumeHovered, bool exitHovered) const {
    const auto drawVertices = [this](const std::vector<glm::vec2>& vertices,
                                     const glm::vec3& color) {
        glUniform3fv(glGetUniformLocation(hudProgram_, "hudColor"), 1,
                     glm::value_ptr(color));
        glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(vertices.size() * sizeof(glm::vec2)),
                     vertices.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    };

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(hudProgram_);
    glBindVertexArray(hudVao_);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);

    std::vector<glm::vec2> panel;
    appendHudRectangle(panel, 30.0F, 70.0F, 335.0F, 425.0F,
                       viewportWidth_, viewportHeight_);
    drawVertices(panel, {0.035F, 0.055F, 0.075F});

    std::vector<glm::vec2> resumeButton;
    appendHudRectangle(resumeButton, 60.0F, 250.0F, 300.0F, 310.0F,
                       viewportWidth_, viewportHeight_);
    drawVertices(resumeButton, resumeHovered ? glm::vec3{0.28F, 0.62F, 0.24F}
                                             : glm::vec3{0.16F, 0.36F, 0.18F});

    std::vector<glm::vec2> exitButton;
    appendHudRectangle(exitButton, 60.0F, 330.0F, 300.0F, 390.0F,
                       viewportWidth_, viewportHeight_);
    drawVertices(exitButton, exitHovered ? glm::vec3{0.72F, 0.25F, 0.20F}
                                         : glm::vec3{0.40F, 0.16F, 0.14F});

    std::vector<glm::vec2> text;
    appendHudText(text, Text::get("pause.title"), 91.0F, 115.0F, 4.0F,
                  viewportWidth_, viewportHeight_);
    appendHudText(text, Text::get("pause.resume"), 107.0F, 269.0F, 3.0F,
                  viewportWidth_, viewportHeight_);
    appendHudText(text, Text::get("menu.exit"), 134.0F, 349.0F, 3.0F,
                  viewportWidth_, viewportHeight_);
    drawVertices(text, {0.95F, 0.98F, 0.82F});
    drawText(Text::get("pause.title"),91.0F,106.0F,4.0F);
    drawText(Text::get("pause.resume"),107.0F,261.0F,3.0F);
    drawText(Text::get("menu.exit"),134.0F,341.0F,3.0F);

    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawBuildHud(PlayerId team, const std::string& entityType,
                            std::size_t entityCount, const std::string& status) const {
    std::vector<glm::vec2> panel;
    appendHudRectangle(panel, 10.0F, 10.0F, 520.0F, 112.0F,
                       viewportWidth_, viewportHeight_);
    std::vector<glm::vec2> text;
    const std::string line = Text::format("build.header", {
        Text::get(team == 1 ? "build.team.a" : "build.team.b"),
        Text::get(entityType == "worker" ? "entity.worker" : "entity.town_center"),
        std::to_string(entityCount)});
    appendHudText(text, line, 22.0F, 22.0F, 2.0F, viewportWidth_, viewportHeight_);
    appendHudText(text, Text::get("build.controls.select"),
                  22.0F, 48.0F, 1.5F, viewportWidth_, viewportHeight_);
    appendHudText(text, Text::format("build.controls.camera", {status}),
                  22.0F, 74.0F, 1.5F, viewportWidth_, viewportHeight_);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glUseProgram(hudProgram_);
    glBindVertexArray(hudVao_); glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(glm::vec2),nullptr);
    auto draw=[this](const std::vector<glm::vec2>& vertices,const glm::vec3& color) {
        glUniform3fv(glGetUniformLocation(hudProgram_,"hudColor"),1,glm::value_ptr(color));
        glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(vertices.size()*sizeof(glm::vec2)),vertices.data(),GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(vertices.size()));
    };
    draw(panel,{0.025F,0.04F,0.06F}); draw(text,{0.92F,0.96F,0.82F});
    drawText(line,22.0F,18.0F,2.0F);
    drawText(Text::get("build.controls.select"),22.0F,44.0F,1.5F);
    drawText(Text::format("build.controls.camera",{status}),22.0F,70.0F,1.5F);
    glBindVertexArray(0); glEnable(GL_CULL_FACE); glEnable(GL_DEPTH_TEST);
}

void Renderer::drawStrategyHud(const World& world,EntityId selected,const Player* player) const {
    (void)selected;std::vector<glm::vec2> map,dots,rememberedFog,visibleFog,rememberedDots;
    const float mapLeft=static_cast<float>(viewportWidth_)-210.0F,mapTop=20.0F,mapRight=static_cast<float>(viewportWidth_)-20.0F,mapBottom=210.0F;
    appendHudRectangle(map,mapLeft,mapTop,mapRight,mapBottom,viewportWidth_,viewportHeight_);
    const float extent=terrain_.worldExtent(),half=extent*0.5F;
    if(player)for(int z=0;z<16;++z)for(int x=0;x<16;++x){const int sourceX=x*Player::explorationCells/16+Player::explorationCells/32,sourceZ=z*Player::explorationCells/16+Player::explorationCells/32;const auto index=static_cast<std::size_t>(sourceZ*Player::explorationCells+sourceX);auto& layer=player->visible[index]?visibleFog:rememberedFog;if(player->visible[index]||player->discovered[index]){const float l=mapLeft+8.0F+x*(mapRight-mapLeft-16.0F)/16.0F,t=mapTop+28.0F+z*(mapBottom-mapTop-36.0F)/16.0F;appendHudRectangle(layer,l,t,l+(mapRight-mapLeft-16.0F)/16.0F+0.5F,t+(mapBottom-mapTop-36.0F)/16.0F+0.5F,viewportWidth_,viewportHeight_);}}
    for(const Entity& entity:world.entities()) {
        if(player&&entity.authority.owner!=player->id) {const int gx=std::clamp(static_cast<int>((entity.transform.position.x/extent+0.5F)*Player::explorationCells),0,Player::explorationCells-1),gz=std::clamp(static_cast<int>((entity.transform.position.z/extent+0.5F)*Player::explorationCells),0,Player::explorationCells-1);if(!player->visible[static_cast<std::size_t>(gz*Player::explorationCells+gx)])continue;}
        const float x=mapLeft+8.0F+(entity.transform.position.x+half)/extent*(mapRight-mapLeft-16.0F);
        const float y=mapTop+28.0F+(entity.transform.position.z+half)/extent*(mapBottom-mapTop-36.0F);
        appendHudRectangle(dots,x-2.5F,y-2.5F,x+2.5F,y+2.5F,viewportWidth_,viewportHeight_);
    }
    if(player)for(const LastKnownEntity& known:player->intelligence){const int gx=std::clamp(static_cast<int>((known.position.x/extent+0.5F)*Player::explorationCells),0,Player::explorationCells-1),gz=std::clamp(static_cast<int>((known.position.z/extent+0.5F)*Player::explorationCells),0,Player::explorationCells-1);if(player->visible[static_cast<std::size_t>(gz*Player::explorationCells+gx)])continue;const float x=mapLeft+8.0F+(known.position.x+half)/extent*(mapRight-mapLeft-16.0F),y=mapTop+28.0F+(known.position.z+half)/extent*(mapBottom-mapTop-36.0F);appendHudRectangle(rememberedDots,x-2.0F,y-2.0F,x+2.0F,y+2.0F,viewportWidth_,viewportHeight_);}
    glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glUseProgram(hudProgram_);glBindVertexArray(hudVao_);glBindBuffer(GL_ARRAY_BUFFER,hudVbo_);glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(glm::vec2),nullptr);
    auto draw=[this](const std::vector<glm::vec2>&v,const glm::vec3&c){glUniform3fv(glGetUniformLocation(hudProgram_,"hudColor"),1,glm::value_ptr(c));glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(v.size()*sizeof(glm::vec2)),v.data(),GL_DYNAMIC_DRAW);glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(v.size()));};
    draw(map,{0.008F,0.012F,0.016F});draw(rememberedFog,{0.055F,0.085F,0.085F});draw(visibleFog,{0.10F,0.22F,0.18F});draw(rememberedDots,{0.32F,0.38F,0.40F});draw(dots,{0.92F,0.82F,0.25F});
    drawText(Text::get("strategy.minimap"),mapLeft+8.0F,mapTop+4.0F,1.5F);
    // Overlay team markers in their faction colours.
    for(PlayerId team:{PlayerId{1},PlayerId{2}}){std::vector<glm::vec2>teamDots;for(const Entity&e:world.entities())if(e.authority.owner==team){if(player&&team!=player->id){const int gx=std::clamp(static_cast<int>((e.transform.position.x/extent+0.5F)*Player::explorationCells),0,Player::explorationCells-1),gz=std::clamp(static_cast<int>((e.transform.position.z/extent+0.5F)*Player::explorationCells),0,Player::explorationCells-1);if(!player->visible[static_cast<std::size_t>(gz*Player::explorationCells+gx)])continue;}float x=mapLeft+8.0F+(e.transform.position.x+half)/extent*(mapRight-mapLeft-16.0F);float y=mapTop+28.0F+(e.transform.position.z+half)/extent*(mapBottom-mapTop-36.0F);appendHudRectangle(teamDots,x-3.5F,y-3.5F,x+3.5F,y+3.5F,viewportWidth_,viewportHeight_);}draw(teamDots,team==1?glm::vec3{0.2F,0.55F,1.0F}:glm::vec3{0.95F,0.22F,0.18F});}
    glBindVertexArray(0);glEnable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);
}

void Renderer::drawUnitHud(const Entity& controlled) const {
    std::vector<glm::vec2> panel,text,healthBack,healthFill,crosshair;
    const float top=static_cast<float>(viewportHeight_)-125.0F;
    appendHudRectangle(panel,18.0F,top,390.0F,static_cast<float>(viewportHeight_)-18.0F,viewportWidth_,viewportHeight_);
    appendHudText(text,Text::format("unit.name",{controlled.name}),30.0F,top+12.0F,2.0F,viewportWidth_,viewportHeight_);
    appendHudText(text,Text::format("unit.team",{Text::get(controlled.authority.owner==1?"build.team.a":"build.team.b")}),30.0F,top+38.0F,1.5F,viewportWidth_,viewportHeight_);
    const int current=static_cast<int>(std::max(0.0F,controlled.health.current)),maximum=static_cast<int>(std::max(1.0F,controlled.health.maximum));
    appendHudText(text,Text::format("unit.health",{std::to_string(current),std::to_string(maximum)}),30.0F,top+58.0F,1.5F,viewportWidth_,viewportHeight_);
    appendHudText(text,Text::get("unit.escape"),30.0F,top+84.0F,1.25F,viewportWidth_,viewportHeight_);
    appendHudRectangle(healthBack,205.0F,top+55.0F,365.0F,top+70.0F,viewportWidth_,viewportHeight_);
    const float ratio=glm::clamp(controlled.health.current/controlled.health.maximum,0.0F,1.0F);
    appendHudRectangle(healthFill,205.0F,top+55.0F,205.0F+160.0F*ratio,top+70.0F,viewportWidth_,viewportHeight_);
    const float cx=viewportWidth_*0.5F,cy=viewportHeight_*0.5F;
    appendHudRectangle(crosshair,cx-10.0F,cy-1.0F,cx+10.0F,cy+1.0F,viewportWidth_,viewportHeight_);
    appendHudRectangle(crosshair,cx-1.0F,cy-10.0F,cx+1.0F,cy+10.0F,viewportWidth_,viewportHeight_);
    glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glUseProgram(hudProgram_);glBindVertexArray(hudVao_);glBindBuffer(GL_ARRAY_BUFFER,hudVbo_);glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(glm::vec2),nullptr);
    auto draw=[this](const std::vector<glm::vec2>&v,const glm::vec3&c){glUniform3fv(glGetUniformLocation(hudProgram_,"hudColor"),1,glm::value_ptr(c));glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(v.size()*sizeof(glm::vec2)),v.data(),GL_DYNAMIC_DRAW);glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(v.size()));};
    draw(panel,{0.025F,0.04F,0.06F});draw(healthBack,{0.18F,0.06F,0.05F});draw(healthFill,{0.18F,0.72F,0.22F});draw(crosshair,{0.95F,0.95F,0.84F});draw(text,{0.93F,0.97F,0.84F});
    drawText(Text::format("unit.name",{controlled.name}),30.0F,top+8.0F,2.0F);
    drawText(Text::format("unit.team",{Text::get(controlled.authority.owner==1?"build.team.a":"build.team.b")}),30.0F,top+34.0F,1.5F);
    drawText(Text::format("unit.health",{std::to_string(current),std::to_string(maximum)}),30.0F,top+54.0F,1.5F);
    drawText(Text::get("unit.escape"),30.0F,top+80.0F,1.25F);
    glBindVertexArray(0);glEnable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);
}

void Renderer::drawTownHallHud(const Entity& hall,const std::array<bool,3>& hovered) const {
    const float height=static_cast<float>(viewportHeight_),top=height-235.0F;
    std::vector<glm::vec2> panel,buttons[3],queueSlots,queueIcons,progressBack,progressFill;
    appendHudRectangle(panel,18.0F,top,640.0F,height-18.0F,viewportWidth_,viewportHeight_);
    constexpr std::array<float,3> lefts{30.0F,230.0F,430.0F};
    constexpr std::array<float,3> rights{220.0F,420.0F,620.0F};
    for(std::size_t i=0;i<3;++i)appendHudRectangle(buttons[i],lefts[i],height-75.0F,rights[i],height-30.0F,viewportWidth_,viewportHeight_);
    const std::size_t visible=std::min<std::size_t>(hall.production.queue.size(),9);
    for(std::size_t i=0;i<visible;++i) {
        const float left=30.0F+static_cast<float>(i)*54.0F;
        appendHudRectangle(queueSlots,left,top+82.0F,left+44.0F,top+126.0F,viewportWidth_,viewportHeight_);
        // Temporary modern pictogram: a compact equipment tile with a bright core.
        appendHudRectangle(queueIcons,left+10.0F,top+92.0F,left+34.0F,top+116.0F,viewportWidth_,viewportHeight_);
        appendHudRectangle(queueIcons,left+16.0F,top+86.0F,left+28.0F,top+122.0F,viewportWidth_,viewportHeight_);
    }
    if(!hall.production.queue.empty()) {
        const ProductionOrder& active=hall.production.queue.front();
        const float ratio=active.durationSeconds>0.0F?std::clamp(1.0F-active.remainingSeconds/active.durationSeconds,0.0F,1.0F):1.0F;
        appendHudRectangle(progressBack,30.0F,top+137.0F,620.0F,top+147.0F,viewportWidth_,viewportHeight_);
        appendHudRectangle(progressFill,30.0F,top+137.0F,30.0F+590.0F*ratio,top+147.0F,viewportWidth_,viewportHeight_);
    }
    glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glUseProgram(hudProgram_);glBindVertexArray(hudVao_);glBindBuffer(GL_ARRAY_BUFFER,hudVbo_);glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(glm::vec2),nullptr);
    auto draw=[this](const std::vector<glm::vec2>&v,const glm::vec3&c){glUniform3fv(glGetUniformLocation(hudProgram_,"hudColor"),1,glm::value_ptr(c));glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(v.size()*sizeof(glm::vec2)),v.data(),GL_DYNAMIC_DRAW);glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(v.size()));};
    draw(panel,{0.025F,0.04F,0.06F});for(std::size_t i=0;i<3;++i)draw(buttons[i],hovered[i]?glm::vec3{0.32F,0.56F,0.22F}:glm::vec3{0.14F,0.27F,0.20F});
    draw(queueSlots,{0.08F,0.12F,0.15F});draw(queueIcons,{0.18F,0.76F,0.88F});draw(progressBack,{0.06F,0.08F,0.10F});draw(progressFill,{0.18F,0.76F,0.88F});
    drawText(Text::format("town_hall.title",{std::to_string(hall.production.level)}),30.0F,top+14.0F,2.4F);
    std::string current=Text::get("town_hall.queue.empty");std::ostringstream remaining;remaining<<std::fixed<<std::setprecision(1)<<0.0F;
    if(!hall.production.queue.empty()) {const ProductionOrder& active=hall.production.queue.front();current=Text::get(active.kind==ProductionKind::trainCharacter?"town_hall.queue.train":active.kind==ProductionKind::upgradeBuilding?"town_hall.queue.upgrade":"town_hall.queue.research");remaining.str("");remaining<<std::fixed<<std::setprecision(1)<<std::max(0.0F,active.remainingSeconds);}
    if(hall.production.queue.empty())drawText(Text::get("town_hall.production.empty"),30.0F,top+48.0F,1.7F);
    else drawText(Text::format("town_hall.production",{std::to_string(hall.production.queue.size()),current,remaining.str()}),30.0F,top+48.0F,1.7F);
    if(hall.production.queue.size()>visible)drawText("+ "+std::to_string(hall.production.queue.size()-visible),526.0F,top+96.0F,1.35F);
    drawText(hall.production.level>=3?Text::get("town_hall.max_level"):Text::get("town_hall.upgrade"),42.0F,height-64.0F,1.35F);
    drawText(hall.production.characterBuildSeconds<=2.0F?Text::get("town_hall.minimum_time"):Text::get("town_hall.faster"),242.0F,height-64.0F,1.35F);
    drawText(Text::get("town_hall.train"),442.0F,height-64.0F,1.35F);
    glBindVertexArray(0);glEnable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);
}

void Renderer::drawSelectionBox(const glm::vec2& start,const glm::vec2& end) const {
    const float left=std::min(start.x,end.x),right=std::max(start.x,end.x);
    const float top=std::min(start.y,end.y),bottom=std::max(start.y,end.y);
    std::vector<glm::vec2> border;
    appendHudRectangle(border,left,top,right,top+2.0F,viewportWidth_,viewportHeight_);
    appendHudRectangle(border,left,bottom-2.0F,right,bottom,viewportWidth_,viewportHeight_);
    appendHudRectangle(border,left,top,left+2.0F,bottom,viewportWidth_,viewportHeight_);
    appendHudRectangle(border,right-2.0F,top,right,bottom,viewportWidth_,viewportHeight_);
    glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glUseProgram(hudProgram_);
    glUniform3f(glGetUniformLocation(hudProgram_,"hudColor"),1.0F,0.82F,0.05F);
    glBindVertexArray(hudVao_);glBindBuffer(GL_ARRAY_BUFFER,hudVbo_);
    glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(border.size()*sizeof(glm::vec2)),border.data(),GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(glm::vec2),nullptr);
    glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(border.size()));glBindVertexArray(0);
    glEnable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);
}

std::vector<EntityId> Renderer::unitsInScreenRectangle(const glm::vec2& start,
    const glm::vec2& end,const World& world,const CameraView& camera,PlayerId owner) const {
    const float left=std::min(start.x,end.x),right=std::max(start.x,end.x);
    const float top=std::min(start.y,end.y),bottom=std::max(start.y,end.y);
    const glm::mat4 viewProjection=camera.viewProjection();std::vector<EntityId> result;
    for(const Entity& entity:world.entities()) {
        if(entity.authority.owner!=owner||entity.resource)continue;
        const EntityDefinition* definition=resources_.entityDefinition(entity.modelKey);
        const float height=definition?definition->selectionHeight:1.8F;
        const float radius=definition?definition->selectionRadius:0.6F;
        const float ground=terrain_.heightAt(entity.transform.position.x,entity.transform.position.z);
        const glm::vec3 center{entity.transform.position.x,ground+height*0.5F,entity.transform.position.z};
        const std::array<glm::vec3,6> points{center+glm::vec3{-radius,0,0},center+glm::vec3{radius,0,0},center+glm::vec3{0,-height*0.5F,0},center+glm::vec3{0,height*0.5F,0},center+glm::vec3{0,0,-radius},center+glm::vec3{0,0,radius}};
        float entityLeft=std::numeric_limits<float>::max(),entityRight=-entityLeft,entityTop=entityLeft,entityBottom=-entityLeft;bool projected=false;
        for(const glm::vec3& point:points){const glm::vec4 clip=viewProjection*glm::vec4{point,1.0F};if(clip.w<=0.0F)continue;const glm::vec3 ndc=glm::vec3(clip)/clip.w;const float screenX=(ndc.x+1.0F)*0.5F*viewportWidth_,screenY=(1.0F-ndc.y)*0.5F*viewportHeight_;entityLeft=std::min(entityLeft,screenX);entityRight=std::max(entityRight,screenX);entityTop=std::min(entityTop,screenY);entityBottom=std::max(entityBottom,screenY);projected=true;}
        if(projected&&entityRight>=left&&entityLeft<=right&&entityBottom>=top&&entityTop<=bottom)result.push_back(entity.id);
    }
    return result;
}

void Renderer::drawUnitSelectionHud(const World& world,const std::vector<EntityId>& selected) const {
    struct Group {std::string model,name;std::size_t count;};std::vector<Group> groups;
    for(EntityId id:selected)if(const Entity* entity=world.findEntity(id)) {auto found=std::find_if(groups.begin(),groups.end(),[&](const Group& group){return group.model==entity->modelKey;});if(found==groups.end())groups.push_back({entity->modelKey,entity->name,1});else ++found->count;}
    const std::size_t visible=std::min<std::size_t>(groups.size(),6);
    const float panelHeight=58.0F+static_cast<float>(visible)*24.0F;
    const float top=static_cast<float>(viewportHeight_)-panelHeight-18.0F;
    std::vector<glm::vec2> panel;
    appendHudRectangle(panel,18.0F,top,410.0F,static_cast<float>(viewportHeight_)-18.0F,viewportWidth_,viewportHeight_);
    glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glUseProgram(hudProgram_);
    glUniform3f(glGetUniformLocation(hudProgram_,"hudColor"),0.025F,0.04F,0.06F);
    glBindVertexArray(hudVao_);glBindBuffer(GL_ARRAY_BUFFER,hudVbo_);glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(panel.size()*sizeof(glm::vec2)),panel.data(),GL_DYNAMIC_DRAW);glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(glm::vec2),nullptr);glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(panel.size()));glBindVertexArray(0);
    drawText(Text::format("selection.title",{std::to_string(selected.size())}),30.0F,top+12.0F,2.0F);
    for(std::size_t index=0;index<visible;++index)drawText(Text::format("selection.group",{groups[index].name,std::to_string(groups[index].count)}),30.0F,top+40.0F+index*24.0F,1.35F);
    if(groups.size()>visible)drawText(Text::format("selection.more",{std::to_string(groups.size()-visible)}),280.0F,top+12.0F,1.35F);
    glEnable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);
}

void Renderer::drawOrderMarkers(const World& world,EntityId selected,
                                const std::vector<EntityId>& selection,
                                const CameraView& camera) const {
    std::vector<glm::vec2> destinations;
    const auto add=[&](EntityId id){const Entity* entity=world.findEntity(id);if(!entity||!entity->unitControl||!entity->unitControl.hasStrategicDestination)return;const glm::vec2 destination{entity->unitControl.strategicDestination.x,entity->unitControl.strategicDestination.z};for(const glm::vec2& existing:destinations)if(glm::dot(existing-destination,existing-destination)<0.25F)return;destinations.push_back(destination);};
    if(selection.empty())add(selected);else for(EntityId id:selection)add(id);
    if(destinations.empty())return;
    std::vector<glm::vec2> poles,flags;
    const auto screen=[this](glm::vec4 clip){const glm::vec3 ndc=glm::vec3(clip)/clip.w;return glm::vec2{(ndc.x+1.0F)*0.5F*viewportWidth_,(1.0F-ndc.y)*0.5F*viewportHeight_};};
    const auto ndc=[this](glm::vec2 pixel){return glm::vec2{pixel.x/static_cast<float>(viewportWidth_)*2.0F-1.0F,1.0F-pixel.y/static_cast<float>(viewportHeight_)*2.0F};};
    for(const glm::vec2& destination:destinations){const float ground=terrain_.heightAt(destination.x,destination.y);const glm::vec4 clip=camera.viewProjection()*glm::vec4{destination.x,ground+0.15F,destination.y,1.0F};if(clip.w<=0.0F)continue;const glm::vec2 base=screen(clip);if(base.x<0||base.x>viewportWidth_||base.y<0||base.y>viewportHeight_)continue;appendHudRectangle(poles,base.x-1.5F,base.y-30.0F,base.x+1.5F,base.y+2.0F,viewportWidth_,viewportHeight_);flags.insert(flags.end(),{ndc({base.x+1.5F,base.y-30.0F}),ndc({base.x+20.0F,base.y-24.0F}),ndc({base.x+1.5F,base.y-18.0F})});}
    if(poles.empty())return;glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glUseProgram(hudProgram_);glBindVertexArray(hudVao_);glBindBuffer(GL_ARRAY_BUFFER,hudVbo_);glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(glm::vec2),nullptr);const auto draw=[this](const std::vector<glm::vec2>& vertices,const glm::vec3& color){glUniform3fv(glGetUniformLocation(hudProgram_,"hudColor"),1,glm::value_ptr(color));glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(vertices.size()*sizeof(glm::vec2)),vertices.data(),GL_DYNAMIC_DRAW);glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(vertices.size()));};draw(poles,{0.92F,0.92F,0.82F});draw(flags,{1.0F,0.72F,0.05F});glBindVertexArray(0);glEnable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);
}

glm::vec3 Renderer::screenToTerrain(float pixelX, float pixelY,
                                    const CameraView& camera) const {
    const glm::vec3 focus=camera.target;
    const float ground=terrain_.heightAt(focus.x,focus.z);
    const glm::mat4 inverseViewProjection=glm::inverse(camera.viewProjection());
    const float x=2.0F*pixelX/static_cast<float>(viewportWidth_)-1.0F;
    const float y=1.0F-2.0F*pixelY/static_cast<float>(viewportHeight_);
    glm::vec4 nearPoint=inverseViewProjection*glm::vec4{x,y,-1.0F,1.0F};
    glm::vec4 farPoint=inverseViewProjection*glm::vec4{x,y,1.0F,1.0F};
    nearPoint/=nearPoint.w; farPoint/=farPoint.w;
    const glm::vec3 origin{nearPoint}; const glm::vec3 direction=glm::normalize(glm::vec3(farPoint-nearPoint));
    glm::vec3 previous=origin; float previousDelta=previous.y-terrain_.heightAt(previous.x,previous.z);
    for(float distance=1.0F;distance<=350.0F;distance+=1.0F) {
        const glm::vec3 point=origin+direction*distance;
        const float delta=point.y-terrain_.heightAt(point.x,point.z);
        if(previousDelta>=0.0F&&delta<=0.0F) {
            glm::vec3 low=previous,high=point;
            for(int i=0;i<8;++i) { const glm::vec3 middle=(low+high)*0.5F;
                if(middle.y>terrain_.heightAt(middle.x,middle.z)) low=middle; else high=middle; }
            glm::vec3 result=(low+high)*0.5F; result.y=terrain_.heightAt(result.x,result.z); return result;
        }
        previous=point; previousDelta=delta;
    }
    return {focus.x,ground,focus.z};
}

EntityId Renderer::pickEntity(float pixelX,float pixelY,const World& world,
                              const CameraView& camera,const Player* player,
                              bool currentlyVisibleOnly) const {
    const float x=2.0F*pixelX/static_cast<float>(viewportWidth_)-1.0F;
    const float y=1.0F-2.0F*pixelY/static_cast<float>(viewportHeight_);
    const glm::mat4 inverseViewProjection=glm::inverse(camera.viewProjection());
    glm::vec4 nearPoint=inverseViewProjection*glm::vec4{x,y,-1.0F,1.0F};
    glm::vec4 farPoint=inverseViewProjection*glm::vec4{x,y,1.0F,1.0F};
    nearPoint/=nearPoint.w; farPoint/=farPoint.w;
    const glm::vec3 origin{nearPoint};
    const glm::vec3 direction=glm::normalize(glm::vec3(farPoint-nearPoint));
    EntityId best=0; float bestDistance=std::numeric_limits<float>::max();
    for(const Entity& entity:world.entities()) {
        if(entity.resource&&entity.resource.remaining<=0.0F)continue;
        glm::vec3 pickPosition=entity.transform.position;
        if(player&&entity.authority.owner!=player->id) {
            constexpr float extent=Terrain::cellCount*Terrain::spacing;
            const int gridX=std::clamp(static_cast<int>((entity.transform.position.x/extent+0.5F)*Player::explorationCells),0,Player::explorationCells-1);
            const int gridZ=std::clamp(static_cast<int>((entity.transform.position.z/extent+0.5F)*Player::explorationCells),0,Player::explorationCells-1);
            const std::size_t index=static_cast<std::size_t>(gridZ*Player::explorationCells+gridX);
            if(!player->discovered[index]||(currentlyVisibleOnly&&!player->visible[index]))continue;
            if(!player->visible[index]){const auto known=std::find_if(player->intelligence.begin(),player->intelligence.end(),[&](const LastKnownEntity& item){return item.id==entity.id;});if(known==player->intelligence.end())continue;pickPosition=known->position;}
        }
        const EntityDefinition* definition=resources_.entityDefinition(entity.modelKey);
        if(!definition) continue;
        const float groundHeight=terrain_.heightAt(pickPosition.x,pickPosition.z);
        const glm::vec3 center{pickPosition.x,groundHeight+definition->selectionHeight*0.5F,pickPosition.z};
        // Ray/ellipsoid intersection keeps tall buildings from acquiring a huge
        // spherical click area over nearby empty terrain.
        const float horizontalRadius=std::max(0.15F,definition->selectionRadius);
        const float verticalRadius=std::max(0.15F,definition->selectionHeight*0.5F);
        const glm::vec3 inverseRadii{1.0F/horizontalRadius,1.0F/verticalRadius,1.0F/horizontalRadius};
        const glm::vec3 offset=(origin-center)*inverseRadii;
        const glm::vec3 scaledDirection=direction*inverseRadii;
        const float a=glm::dot(scaledDirection,scaledDirection);
        const float b=glm::dot(offset,scaledDirection);
        const float c=glm::dot(offset,offset)-1.0F; const float discriminant=b*b-a*c;
        if(discriminant<0.0F) continue;
        const float distance=(-b-std::sqrt(discriminant))/a;
        if(distance>=0.0F&&distance<bestDistance){bestDistance=distance;best=entity.id;}
    }
    return best;
}

void Renderer::drawEntityOutline(const World& world,EntityId id,const CameraView& camera,const Player* player) const {
    const Entity* entity=world.findEntity(id);if(!entity)return;std::string modelKey=entity->modelKey;Transform shown=entity->transform;bool remembered=false;
    if(player&&entity->authority.owner!=player->id){constexpr float extent=Terrain::cellCount*Terrain::spacing;const int x=std::clamp(static_cast<int>((shown.position.x/extent+0.5F)*Player::explorationCells),0,Player::explorationCells-1),z=std::clamp(static_cast<int>((shown.position.z/extent+0.5F)*Player::explorationCells),0,Player::explorationCells-1);if(!player->visible[static_cast<std::size_t>(z*Player::explorationCells+x)]){const auto known=std::find_if(player->intelligence.begin(),player->intelligence.end(),[&](const LastKnownEntity& item){return item.id==id;});if(known==player->intelligence.end())return;modelKey=known->modelKey;shown.position=known->position;shown.rotationDegrees=known->rotationDegrees;shown.scale=known->scale;remembered=true;}}
    const Model* model=resources_.model(modelKey);if(!model)return;const EntityDefinition* definition=resources_.entityDefinition(modelKey);const float scale=definition?definition->scale:1.0F;
    const float ground=terrain_.heightAt(shown.position.x,shown.position.z);glm::mat4 transform{1.0F};transform=glm::translate(transform,{shown.position.x,ground+shown.position.y,shown.position.z});transform=glm::rotate(transform,glm::radians(shown.rotationDegrees.x),{1,0,0});transform=glm::rotate(transform,glm::radians(shown.rotationDegrees.y),{0,1,0});transform=glm::rotate(transform,glm::radians(shown.rotationDegrees.z),{0,0,1});transform=glm::scale(transform,shown.scale*scale*1.035F);
    std::string animation;
    if(definition){const bool moving=!remembered&&(glm::length(entity->unitControl.directInput)>0.01F||entity->unitControl.hasStrategicDestination);const auto found=definition->animations.find(moving?(entity->unitControl.running?"run":"walk"):"idle");if(found!=definition->animations.end())animation=found->second;}
    const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    glCullFace(GL_FRONT);model->draw(outlineProgram_,camera.viewProjection(),transform,animation,seconds);glCullFace(GL_BACK);
}

CameraView Renderer::constrainThirdPersonCamera(const CameraView& desired,
                                                const World& world,EntityId followed) const {
    const glm::vec3 segment=desired.position-desired.target;
    const float desiredDistance=glm::length(segment);
    if(desiredDistance<0.001F) return desired;
    const glm::vec3 direction=segment/desiredDistance;
    float allowed=desiredDistance;
    const float half=terrain_.worldExtent()*0.5F-0.5F;
    for(float distance=0.5F;distance<=desiredDistance;distance+=0.25F) {
        const glm::vec3 point=desired.target+direction*distance;
        if(std::abs(point.x)>half||std::abs(point.z)>half
            ||point.y<terrain_.heightAt(point.x,point.z)+0.45F) {
            allowed=std::max(0.5F,distance-0.35F); break;
        }
    }
    for(const Entity& entity:world.entities()) {
        if(entity.id==followed) continue;
        const EntityDefinition* definition=resources_.entityDefinition(entity.modelKey);
        if(!definition||definition->selectionRadius<1.0F) continue;
        const float ground=terrain_.heightAt(entity.transform.position.x,entity.transform.position.z);
        const glm::vec3 center{entity.transform.position.x,
            ground+definition->selectionHeight*0.5F,entity.transform.position.z};
        const glm::vec3 offset=desired.target-center;
        const float radius=definition->selectionRadius+0.3F;
        const float b=glm::dot(offset,direction);
        const float c=glm::dot(offset,offset)-radius*radius;
        const float discriminant=b*b-c;
        if(discriminant<0.0F) continue;
        const float hit=-b-std::sqrt(discriminant);
        if(hit>0.5F&&hit<allowed) allowed=std::max(0.5F,hit-0.3F);
    }
    CameraView result=desired;
    result.position=desired.target+direction*allowed;
    result.view=glm::lookAt(result.position,result.target,{0.0F,1.0F,0.0F});
    return result;
}

float Renderer::terrainHeightAt(float worldX, float worldZ) const {
    return terrain_.heightAt(worldX, worldZ);
}

} // namespace strategy

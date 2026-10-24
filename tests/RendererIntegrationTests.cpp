#include "TerrainLevelingFixture.hpp"
#include "TerrainTiltFixture.hpp"
#include "audio/AudioSystem.hpp"
#include "core/EventBus.hpp"
#include "diagnostics/Logger.hpp"
#include "game/PlayState.hpp"
#include "game/RtsCamera.hpp"
#include "game/SettingsState.hpp"
#include "gameplay/DefinitionRegistry.hpp"
#include "localization/Text.hpp"
#include "render/Renderer.hpp"
#include "render/ShaderManager.hpp"
#include "simulation/GameSession.hpp"
#include "simulation/StateChecksum.hpp"
#include "terrain/Terrain.hpp"
#include "terrain/TerrainGenerationPipeline.hpp"
#include "terrain/TerrainV2Recipe.hpp"
#include "world/Collision.hpp"
#include "world/GenerationProgress.hpp"
#include "world/PowerPlacement.hpp"
#include "world/Vegetation.hpp"
#include "world/World.hpp"
#include "world/WorldGeneration.hpp"

#include <RmlUi/Core.h>
#include <SDL3/SDL.h>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <string_view>
#include <thread>

namespace {
bool noGlErrors(const char* stage) {
    bool valid = true;
    for (GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError()) {
        std::cerr << stage << " generated OpenGL error " << error << '\n';
        valid = false;
    }
    return valid;
}

bool checkFormPanel(strategy::Renderer& renderer, const char* selectorId, const char* actionId) {
    renderer.focusUi(0);
    auto* context = Rml::GetContext("strategy_ui");
    auto* focused = context ? context->GetFocusElement() : nullptr;
    if (!focused || focused->GetId() != selectorId) {
        std::cerr << "Form selector did not receive keyboard focus: " << selectorId << '\n';
        return false;
    }
    for (const char* id : {"shell", "content", selectorId, actionId}) {
        const auto bounds = renderer.uiBounds(id);
        if (!bounds || bounds->right <= bounds->left || bounds->bottom <= bounds->top) {
            std::cerr << "Templated form element has no layout: " << id << '\n';
            return false;
        }
    }
    auto* button = focused->GetElementById(std::string(selectorId) + ".left");
    if (!button)
        return false;
    // Compact artwork must not mask the shared interaction tints.
    for (const auto& [state, color] : {
             std::pair{"hover", Rml::Colourb(223, 255, 255)},
             std::pair{"active", Rml::Colourb(174, 191, 196)},
             std::pair{"disabled", Rml::Colourb(104, 114, 119)}}) {
        button->SetPseudoClass(state, true);
        context->Update();
        const bool matches = button->GetProperty<Rml::Colourb>("image-color") == color;
        button->SetPseudoClass(state, false);
        if (!matches) {
            std::cerr << "Selector tint overridden for state: " << state << '\n';
            return false;
        }
    }
    return true;
}

bool captureTerrain(const char* name) {
    const char* directory = SDL_getenv("STRATEGY_TERRAIN_SCREENSHOTS");
    if (!directory && std::string_view{name}.starts_with("satellite-"))
        directory = SDL_getenv("STRATEGY_SATELLITE_SCREENSHOTS");
    if (!directory)
        return true;
    std::filesystem::create_directories(directory);
    std::vector<unsigned char> pixels(1280 * 720 * 4), flipped(pixels.size());
    glFinish();
    glReadPixels(0, 0, 1280, 720, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    for (int row = 0; row < 720; ++row)
        std::copy_n(
            pixels.data() + row * 1280 * 4, 1280 * 4, flipped.data() + (719 - row) * 1280 * 4);
    SDL_Surface* surface =
        SDL_CreateSurfaceFrom(1280, 720, SDL_PIXELFORMAT_RGBA32, flipped.data(), 1280 * 4);
    const bool saved =
        surface && SDL_SaveBMP(surface, (std::filesystem::path(directory) / name).string().c_str());
    SDL_DestroySurface(surface);
    return saved;
}

bool checkPlacementPreview(strategy::Renderer& renderer, const strategy::Terrain& original) {
    using namespace strategy;
    const auto terrainDefinitions = TerrainGenerationDefinitions::load();
    auto recipe =
        TerrainV2Recipes::load().resolve(terrainDefinitions.layout(TerrainLayoutId{"open_plains"}));
    recipe.cells = TerrainCellSettings{};
    std::vector<TerrainCellSource> source(1600);
    for (int z = 0; z < 40; ++z)
        for (int x = 0; x < 40; ++x) {
            const float h = x < 20 ? 16.0F : 12.0F;
            source[z * 40 + x] = {h, h, h, {0, 0}, TerrainCellRole::opening};
        }
    Terrain terrain{Terrain::DeferredInitialization{}};
    TerrainGenerationPipeline::assembleV2(
        terrain,
        7,
        terrainDefinitions.layout(recipe.layout),
        terrainDefinitions,
        {TerrainV2Plan::create(7, recipe, 240, 2),
         TerrainTerraces::resolve(source, 40, 240, recipe.cells)});
    renderer.stageGeneratedTerrain(terrain, 7, 10);
    const DefinitionRegistry definitions;
    const auto* type = definitions.archetype("alloy_processor");
    CameraView camera;
    camera.target = {-3, 16, 0};
    camera.position = camera.target + glm::vec3{17, 20, 22};
    camera.view = glm::lookAt(camera.position, camera.target, glm::vec3{0, 1, 0});
    camera.projection = glm::perspective(glm::radians(55.0F), 1280.0F / 720, .1F, 600.0F);
    renderer.preloadAssetGroup("match");
    for (int frame = 0; frame < 400; ++frame) {
        renderer.beginFrame(1280, 720);
        renderer.drawTerrain(camera);
        renderer.endFrame();
        if (renderer.terrainUploadFinished() && renderer.assetProgress("match").finished())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    bool valid = true;
    for (int view = 0; view < 4; ++view) {
        if (view == 3) {
            recipe.cells.elevationStep = .1F;
            for (int z = 0; z < 40; ++z)
                for (int x = 0; x < 40; ++x) {
                    const float h = x < 20 ? 16.0F : 16.1F;
                    source[z * 40 + x] = {h, h, h, {.02F, 0}, TerrainCellRole::route};
                }
            TerrainGenerationPipeline::assembleV2(
                terrain,
                7,
                terrainDefinitions.layout(recipe.layout),
                terrainDefinitions,
                {TerrainV2Plan::create(7, recipe, 240, 2),
                 TerrainTerraces::resolve(source, 40, 240, recipe.cells)});
            renderer.stageGeneratedTerrain(terrain, 7, 10);
            for (int i = 0; i < 200 && !renderer.terrainUploadFinished(); ++i) {
                renderer.beginFrame(1280, 720);
                renderer.drawTerrain(camera);
                renderer.endFrame();
            }
        }
        const glm::vec2 center{view == 3 ? 0.0F : view == 1 ? .25F : -3.5F, 0};
        auto profile = type->placement;
        if (view == 2)
            profile.maximumFoundationDepth = 2;
        const auto result =
            terrain.evaluatePlacement(center.x, center.y, *type->footprint, profile);
        valid &= view == 0 || view == 3 ? result.valid() : !result.valid();
        World ghost;
        auto& building = ghost.createEntity("Preview", "alloy_processor", 1);
        definitions.initializeEntity(building);
        building.transform.position = {
            center.x,
            result.fit.height - terrain.entityBaseHeight(building.id, center.x, center.y),
            center.y};
        building.construction.emplace();
        building.construction.placementValid = result.valid();
        renderer.beginFrame(1280, 720);
        renderer.drawTerrain(camera);
        renderer.drawWorld(ghost, camera);
        renderer.drawBuildingPlacementPreview(
            center, *type->footprint, profile, result, result.valid(), camera);
        renderer.endFrame();
        glFinish();
        valid &= captureTerrain(view == 0   ? "placement-valid.bmp"
                                : view == 1 ? "placement-overhang.bmp"
                                : view == 2 ? "placement-depth.bmp"
                                            : "placement-gentle-ground.bmp");
        valid &= noGlErrors("building footprint and support preview");
        if (view == 0) {
            auto foundation = terrain.evaluateFoundation(
                building.id, center.x, center.y, *type->footprint, profile);
            foundation.influence = 0;
            const auto initialUploads = renderer.foundationMeshUploadCount();
            renderer.setTerrainFoundations({foundation});
            const auto uploads = renderer.foundationMeshUploadCount();
            valid &= uploads == initialUploads + 1 && renderer.foundationMeshCount() == 1;
            auto closeCamera = camera;
            closeCamera.target = {center.x, 16, center.y};
            closeCamera.position = closeCamera.target + glm::vec3{11, 10, 14};
            closeCamera.view =
                glm::lookAt(closeCamera.position, closeCamera.target, glm::vec3{0, 1, 0});
            building.transform.position.y = 0;
            for (int phase = 0; phase < 4; ++phase) {
                foundation.influence = phase == 0 ? 0 : phase == 1 ? .22F : phase == 2 ? .6F : 1;
                renderer.setTerrainFoundations({foundation});
                building.construction.state = phase == 3
                                                  ? BuildingLifecycleState::operational
                                                  : BuildingLifecycleState::underConstruction;
                renderer.beginFrame(1280, 720);
                renderer.drawTerrain(closeCamera);
                renderer.drawWorld(ghost, closeCamera);
                renderer.endFrame();
                glFinish();
                valid &= captureTerrain(
                    ("scaffold-construction-" + std::to_string(phase) + ".bmp").c_str());
                valid &= renderer.foundationMeshUploadCount() == uploads;
            }
            auto second = foundation;
            second.sourceEntity = 999;
            second.center.z += 12;
            renderer.setTerrainFoundations({foundation, second});
            valid &= renderer.foundationMeshUploadCount() == uploads + 1 &&
                     renderer.foundationMeshCount() == 2;
            renderer.setTerrainFoundations({foundation});
            valid &= renderer.foundationMeshUploadCount() == uploads + 1 &&
                     renderer.foundationMeshCount() == 1;
            auto moved = foundation;
            moved.center.z += .5F;
            renderer.setTerrainFoundations({moved});
            valid &= renderer.foundationMeshUploadCount() == uploads + 2;
            renderer.stageGeneratedTerrain(terrain, 7, 10, {foundation});
            for (int frame = 0; frame < 200 && !renderer.terrainUploadFinished(); ++frame) {
                renderer.beginFrame(1280, 720);
                renderer.drawTerrain(closeCamera);
                renderer.endFrame();
            }
            renderer.beginFrame(1280, 720);
            renderer.drawTerrain(closeCamera);
            renderer.drawWorld(ghost, closeCamera);
            renderer.endFrame();
            glFinish();
            valid &= captureTerrain("scaffold-restored.bmp") && renderer.foundationMeshCount() == 1;
            TerrainFootprint round{FootprintShape::circle, 4, {4, 4}, 37};
            auto circular = terrain.evaluateFoundation(998, -2, 0, round, profile);
            circular.influence = 1;
            renderer.setTerrainFoundations({circular});
            renderer.beginFrame(1280, 720);
            renderer.drawTerrain(closeCamera);
            renderer.endFrame();
            glFinish();
            valid &= captureTerrain("scaffold-circle-rotated.bmp");
            renderer.setTerrainFoundations({});
            valid &= renderer.foundationMeshCount() == 0 &&
                     noGlErrors("scaffold cache and reconstruction");
        }
    }
    // The same immutable bed fixture under shallow water exercises submerged footings.
    auto wetRecipe = TerrainV2Recipes::load().resolve(
        terrainDefinitions.layout(TerrainLayoutId{"central_water"}));
    wetRecipe.cells = TerrainCellSettings{};
    for (int z = 0; z < 40; ++z)
        for (int x = 0; x < 40; ++x) {
            const float h = x < 20 ? 16.0F : 12.0F;
            source[z * 40 + x] = {h, h, h, {0, 0}, TerrainCellRole::opening};
        }
    Terrain wet{Terrain::DeferredInitialization{}};
    TerrainGenerationPipeline::assembleV2(
        wet,
        7,
        terrainDefinitions.layout(wetRecipe.layout),
        terrainDefinitions,
        {TerrainV2Plan::create(7, wetRecipe, 240, 2),
         TerrainTerraces::resolve(source, 40, 240, wetRecipe.cells)});
    TerrainFootprint shoreFootprint{FootprintShape::rectangle, 3, {3, 3}};
    valid &= wet.evaluatePlacement(-3.1F, 0, shoreFootprint, {}).valid();
    TerrainPlacementProfile shoreProfile;
    shoreProfile.support = TerrainBuildingSupport::shore;
    shoreProfile.shore = TerrainShoreSupport{0.2F, 0.8F, 0.4F};
    valid &= wet.evaluatePlacement(0, 0, shoreFootprint, shoreProfile).valid();
    auto shoreFoundation = wet.evaluateFoundation(997, 0, 0, shoreFootprint, shoreProfile);
    shoreFoundation.influence = 1;
    renderer.stageGeneratedTerrain(wet, 7, 10, {shoreFoundation});
    camera.target = {0, 16, 0};
    camera.position = camera.target + glm::vec3{11, 10, 14};
    camera.view = glm::lookAt(camera.position, camera.target, glm::vec3{0, 1, 0});
    for (int frame = 0; frame < 200 && !renderer.terrainUploadFinished(); ++frame) {
        renderer.beginFrame(1280, 720);
        renderer.drawTerrain(camera);
        renderer.endFrame();
    }
    renderer.beginFrame(1280, 720);
    renderer.drawTerrain(camera);
    renderer.endFrame();
    glFinish();
    valid &= captureTerrain("scaffold-shore.bmp") && noGlErrors("submerged scaffold supports");
    renderer.stageGeneratedTerrain(original, 12345, 10);
    return valid;
}

bool checkLevelingPreview(strategy::Renderer& renderer, const strategy::Terrain& original) {
    using namespace strategy;
    auto terrain = test::levelingFixture(.5F, false, false, 3);
    DefinitionRegistry definitions;
    const auto* type = definitions.archetype("alloy_processor");
    auto profile = type->placement;
    profile.levelingMaximumFill = 0;
    profile.levelingMaximumAverageEarthwork = 0;
    auto strict = profile;
    strict.levelingMaximumRelief = 0;
    const auto before = terrain.evaluatePlacement(0, 0, *type->footprint, strict);
    const auto after = terrain.evaluatePlacement(0, 0, *type->footprint, profile);
    if (!after.valid() || !after.leveling) {
        std::cerr << "Leveling fixture missing repair: valid=" << after.valid()
                  << " repair=" << after.leveling.has_value() << '\n';
        return false;
    }
    CameraView camera;
    camera.target = {0, 16, 0};
    camera.position = camera.target + glm::vec3{12, 12, 15};
    camera.view = glm::lookAt(camera.position, camera.target, glm::vec3{0, 1, 0});
    camera.projection = glm::perspective(glm::radians(55.F), 1280.F / 720, .1F, 600.F);
    renderer.stageGeneratedTerrain(terrain, 7, 10);
    while (!renderer.terrainUploadFinished()) {
        renderer.beginFrame(1280, 720);
        renderer.drawTerrain(camera);
        renderer.endFrame();
    }
    World world;
    auto& building = world.createEntity("Preview", "alloy_processor", 1);
    definitions.initializeEntity(building);
    auto foundation = terrain.evaluateFoundation(building.id, 0, 0, *type->footprint, profile);
    // This probe isolates the cut raster. Scaffold geometry has separate tests; including
    // an accepted shallow support deck would obscure the terrain-only depth comparison.
    foundation.requiresSlab = false;
    bool valid = true;
    std::vector<float> originalDepth, previewDepth;
    for (int phase = 0; phase < 4; ++phase) {
        renderer.setTerrainFoundations(phase == 2 ? std::vector{foundation}
                                                  : std::vector<TerrainFoundation>{});
        renderer.setPlacementTerrainPreview(phase == 1 ? &foundation : nullptr);
        const auto uploads = renderer.placementSurfaceUploads();
        renderer.setPlacementTerrainPreview(phase == 1 ? &foundation : nullptr);
        valid &= uploads == renderer.placementSurfaceUploads();
        renderer.flushTerrainUpdates();
        const auto& placement = phase == 0 || phase == 3 ? before : after;
        building.transform.position.y = 0;
        const TerrainFoundation plane{
            building.id, {0, placement.fit.height, 0}, *type->footprint, placement.fit.gradient};
        building.construction.emplace();
        building.construction.placementValid = phase != 0 && phase != 3;
        building.construction.state =
            phase == 2 ? BuildingLifecycleState::operational : BuildingLifecycleState::planned;
        renderer.beginFrame(1280, 720);
        renderer.drawTerrain(camera);
        std::vector<float> depth(1280 * 720);
        glReadPixels(0, 0, 1280, 720, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data());
        if (phase == 0)
            originalDepth = depth;
        if (phase == 1) {
            previewDepth = depth;
            std::size_t changed = 0;
            for (std::size_t i = 0; i < depth.size(); ++i)
                changed += std::abs(depth[i] - originalDepth[i]) > 1e-6F;
            valid &= changed > 100; // The temporary layer really changes rendered geometry.
        }
        if (phase == 2)
            for (std::size_t i = 0; i < depth.size(); ++i)
                valid &= std::abs(depth[i] - previewDepth[i]) < 2e-6F;
        renderer.drawWorld(world, camera, nullptr, false, &plane);
        if (phase != 2)
            renderer.drawBuildingPlacementPreview(
                {0, 0}, *type->footprint, profile, placement, placement.valid(), camera);
        renderer.endFrame();
        glFinish();
        valid &= captureTerrain(phase == 0   ? "leveling-before.bmp"
                                : phase == 1 ? "leveling-preview.bmp"
                                : phase == 2 ? "leveling-complete.bmp"
                                             : "leveling-cancelled.bmp");
        if (!valid)
            std::cerr << "Leveling depth/mesh failure phase=" << phase << '\n';
        valid &= renderer.foundationMeshCount() == 0;
        valid &= phase == 2
                     ? std::abs(renderer.terrainHeightAt(1.5F, 1.5F) -
                                terrain.foundationTerrainHeightAt(foundation, {1.5F, 1.5F})) < 1e-4F
                     : renderer.terrainHeightAt(1.5F, 1.5F) > 16.4F;
    }
    if (!valid)
        std::cerr << "Leveling preview mismatch: plane=" << after.fit.height
                  << " angle=" << after.fit.slopeDegrees << '\n';
    renderer.stageGeneratedTerrain(original, 12345, 10);
    return valid && noGlErrors("bounded leveling preview and commit");
}

bool checkGradingPreview(strategy::Renderer& renderer, const strategy::Terrain& original) {
    using namespace strategy;
    DefinitionRegistry definitions;
    const auto* type = definitions.archetype("storage_silo");
    auto terrain = test::levelingFixture(-.5F, false, false, 3);
    const auto placement = terrain.evaluatePlacement(0, 0, *type->footprint, type->placement);
    if (!placement.valid() || !placement.leveling || placement.fillVolume <= 0 ||
        placement.requiresScaffold())
        return false;
    World world;
    auto& building = world.createEntity("Graded pad", "storage_silo", 1);
    definitions.initializeEntity(building);
    building.construction.emplace();
    building.construction.placementValid = true;
    auto foundation = terrain.foundationFromPlacement(
        building.id, {0, 0}, *type->footprint, type->placement, placement);
    CameraView camera;
    camera.target = {0, foundation.center.y + 3, 0};
    camera.position = camera.target + glm::vec3{14, 10, 18};
    camera.view = glm::lookAt(camera.position, camera.target, glm::vec3{0, 1, 0});
    camera.projection = glm::perspective(glm::radians(55.F), 1280.F / 720, .1F, 600.F);
    renderer.stageGeneratedTerrain(terrain, 7, 10);
    while (!renderer.terrainUploadFinished()) {
        renderer.beginFrame(1280, 720);
        renderer.drawTerrain(camera);
        renderer.endFrame();
    }
    bool valid = true;
    std::vector<float> before, preview;
    const float oldHeight = terrain.heightAt(1.5F, 1.5F);
    for (int phase = 0; phase < 4; ++phase) {
        renderer.setTerrainFoundations(phase == 2 ? std::vector{foundation}
                                                  : std::vector<TerrainFoundation>{});
        renderer.setPlacementTerrainPreview(phase == 1 ? &foundation : nullptr);
        renderer.flushTerrainUpdates();
        renderer.beginFrame(1280, 720);
        renderer.drawTerrain(camera);
        std::vector<float> depth(1280 * 720);
        glReadPixels(0, 0, 1280, 720, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data());
        if (phase == 0)
            before = depth;
        if (phase == 1)
            preview = depth;
        if (phase == 2)
            for (std::size_t i = 0; i < depth.size(); ++i)
                valid &= std::abs(depth[i] - preview[i]) < 2e-6F;
        if (phase == 3)
            for (std::size_t i = 0; i < depth.size(); ++i)
                valid &= std::abs(depth[i] - before[i]) < 2e-6F;
        valid &= phase == 2 ? renderer.terrainHeightAt(1.5F, 1.5F) > oldHeight + .1F
                            : renderer.terrainHeightAt(1.5F, 1.5F) == oldHeight;
        if (phase == 1 || phase == 2)
            renderer.drawPlacementGhost(building, camera, &foundation);
        renderer.endFrame();
        valid &= renderer.foundationMeshCount() == 0;
        valid &= captureTerrain(phase == 0   ? "grading-before.bmp"
                                : phase == 1 ? "grading-preview.bmp"
                                : phase == 2 ? "grading-complete.bmp"
                                             : "grading-cancelled.bmp");
    }
    std::size_t changed = 0;
    for (std::size_t i = 0; i < preview.size(); ++i)
        changed += std::abs(preview[i] - before[i]) > 1e-6F;
    valid &= changed > 100;
    renderer.stageGeneratedTerrain(original, 12345, 10);
    return valid && noGlErrors("graded pad preview/commit/cancel");
}

bool checkTiltedPlacement(strategy::Renderer& renderer, const strategy::Terrain& original) {
    using namespace strategy;
    DefinitionRegistry definitions;
    bool valid = true;
    for (int tier = 0; tier < 4; ++tier) {
        auto terrain = tier == 3 ? Terrain{1, TerrainLayoutId{"continental"}, TerrainMode::v2, 15}
                                 : test::tiltFixture(tier == 0   ? .5F
                                                     : tier == 1 ? 1.F
                                                                 : .25F,
                                                     tier == 2 ? .25F : 0.F,
                                                     tier == 2);
        TerrainFootprint footprint{FootprintShape::rectangle,
                                   tier == 2 ? 8.F : 5.F,
                                   tier == 2 ? glm::vec2{8, 8} : glm::vec2{5, 4}};
        footprint.maximumTiltDegrees = 10;
        const glm::vec2 center =
            tier == 3 ? glm::vec2{-64, -160} : glm::vec2{tier == 2 ? -6.F : 0.F, 0};
        auto profile =
            tier == 3 ? definitions.archetype("alloy_processor")->placement : test::tiltProfile();
        if (tier == 1)
            profile.maximumBuildingTiltDegrees = 10;
        const auto placement = terrain.evaluatePlacement(center.x, center.y, footprint, profile);
        if (!placement.valid() || (tier == 1 && placement.fit.slopeDegrees < 9.F))
            return false;
        World world;
        auto& building = world.createEntity("Angled placement", "alloy_processor", 1);
        definitions.initializeEntity(building);
        building.transform.position = {center.x, 0, center.y};
        // Match the larger combined-case footprint instead of displaying an oversized empty deck.
        if (tier == 2)
            building.transform.scale = {1.6F, 1, 2};
        auto foundation =
            terrain.evaluateFoundation(building.id, center.x, center.y, footprint, profile);
        CameraView camera;
        camera.target = foundation.center;
        camera.position = camera.target + glm::vec3{16, 10, 18};
        camera.view = glm::lookAt(camera.position, camera.target, glm::vec3{0, 1, 0});
        camera.projection = glm::perspective(glm::radians(55.F), 1280.F / 720, .1F, 600.F);
        renderer.stageGeneratedTerrain(terrain, 7, 10, {foundation});
        while (!renderer.terrainUploadFinished()) {
            renderer.beginFrame(1280, 720);
            renderer.drawTerrain(camera);
            renderer.endFrame();
        }
        std::vector<float> ghostDepth(1280 * 720);
        for (bool preview : {true, false}) {
            building.construction.emplace();
            building.construction.placementValid = true;
            building.construction.state =
                preview ? BuildingLifecycleState::planned : BuildingLifecycleState::operational;
            renderer.beginFrame(1280, 720);
            renderer.drawTerrain(camera);
            if (preview)
                renderer.drawPlacementGhost(building, camera, &foundation);
            else
                renderer.drawWorld(world, camera);
            std::vector<float> depth(1280 * 720);
            glReadPixels(0, 0, 1280, 720, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data());
            if (preview)
                ghostDepth = depth;
            else
                for (std::size_t i = 0; i < depth.size(); ++i)
                    valid &= std::abs(depth[i] - ghostDepth[i]) < 2e-6F;
            if (preview)
                renderer.drawBuildingPlacementPreview(
                    center, footprint, profile, placement, true, camera);
            renderer.endFrame();
            glFinish();
            const std::string name = "tilt-tier-" + std::to_string(tier + 1) +
                                     (preview ? "-preview.bmp" : "-complete.bmp");
            valid &= captureTerrain(name.c_str()) && noGlErrors("tilted placement");
            valid &= renderer.foundationMeshCount() == (foundation.requiresSlab ? 1 : 0);
        }
        // Prove that the actual ghost renderer visibly tilts the mesh, rather than
        // merely reporting a nonzero angle in the support overlay.
        auto horizontal = foundation;
        horizontal.gradient = {0, 0};
        renderer.beginFrame(1280, 720);
        renderer.drawTerrain(camera);
        renderer.drawPlacementGhost(building, camera, &horizontal);
        std::vector<float> flatDepth(1280 * 720);
        glReadPixels(0, 0, 1280, 720, GL_DEPTH_COMPONENT, GL_FLOAT, flatDepth.data());
        std::size_t changed = 0;
        for (std::size_t i = 0; i < flatDepth.size(); ++i)
            changed += std::abs(flatDepth[i] - ghostDepth[i]) > 2e-6F;
        valid &= placement.fit.slopeDegrees > .5F && changed > 100;
        std::cout << "Rendered placement tier=" << tier + 1
                  << " angle=" << placement.fit.slopeDegrees
                  << " pixels changed by tilt=" << changed << '\n';
        renderer.endFrame();
    }
    renderer.stageGeneratedTerrain(original, 12345, 10);
    return valid;
}

bool checkVegetationCacheBuffers() {
    strategy::ResourceManager resources{"assets"};
    const auto seep = resources.requestModel("v2_oil_seep");
    if (resources.state(seep) != strategy::ResourceState::ready ||
        resources.modelOrMarker(seep) != nullptr || !resources.error(seep).empty() ||
        !resources.entityDefinition("v2_oil_seep")->terrainSurface)
        return false;
    strategy::VegetationInstance instance;
    instance.presentation = strategy::PresentationId{"grass_fresh"};
    instance.transform.position = {2, 0, 3};
    strategy::VegetationChunk first, second;
    first.center = {2, 3};
    first.radius = 1;
    first.instances.push_back(instance);
    second.center = {40, 3};
    second.radius = 1;
    instance.transform.position.x = 40;
    second.instances.push_back(instance);
    strategy::VegetationField field{{first, second}};
    strategy::VegetationRenderCache cache;
    const strategy::VegetationRenderCache::PrepareTransform prepare =
        [](const auto& item, const auto*, float baseY) {
            return glm::translate(glm::mat4{1}, item.transform.position + glm::vec3{0, baseY, 0});
        };
    cache.beginFrame(field);
    const auto& markerBatch =
        cache.prepareChunk(0, field.chunks()[0], resources, prepare).at("grass_fresh");
    const auto handle = markerBatch.handle;
    const auto* markerModel = markerBatch.model;
    for (int attempt = 0;
         attempt < 400 && resources.state(handle) != strategy::ResourceState::ready;
         ++attempt) {
        resources.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (resources.state(handle) != strategy::ResourceState::ready)
        return false;
    cache.beginFrame(field);
    const auto& loadedBatch =
        cache.prepareChunk(0, field.chunks()[0], resources, prepare).at("grass_fresh");
    bool valid = loadedBatch.model != markerModel && cache.stats().rebuiltChunks == 1;
    glm::mat4 matrix;
    glBindBuffer(GL_ARRAY_BUFFER, loadedBatch.buffer);
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(matrix), &matrix);
    valid &= matrix[3].x == 2 && matrix[3].z == 3 && matrix[3].y == loadedBatch.model->baseY();
    const auto firstBuffer = loadedBatch.buffer;
    const auto secondBuffer =
        cache.prepareChunk(1, field.chunks()[1], resources, prepare).at("grass_fresh").buffer;
    cache.beginFrame(field);
    cache.prepareChunk(0, field.chunks()[0], resources, prepare);
    cache.prepareChunk(1, field.chunks()[1], resources, prepare);
    valid &= cache.stats().rebuiltChunks == 0 && cache.stats().uploadedBatches == 0 &&
             glIsBuffer(firstBuffer) && glIsBuffer(secondBuffer);
    cache.invalidateTerrainRegion({0, 0}, {4, 5});
    cache.beginFrame(field);
    cache.prepareChunk(0, field.chunks()[0], resources, prepare);
    const auto& untouched = cache.prepareChunk(1, field.chunks()[1], resources, prepare);
    valid &= cache.stats().rebuiltChunks == 1 && untouched.at("grass_fresh").buffer == secondBuffer;
    field.removeInstance(field.chunks()[0].instances[0].id);
    cache.beginFrame(field);
    valid &=
        cache.prepareChunk(0, field.chunks()[0], resources, prepare).at("grass_fresh").count == 0;
    cache.prepareChunk(1, field.chunks()[1], resources, prepare);
    valid &= cache.stats().rebuiltChunks == 1 && cache.stats().uploadedBatches == 0;
    const auto& survivor =
        cache.prepareChunk(1, field.chunks()[1], resources, prepare).at("grass_fresh");
    glBindBuffer(GL_ARRAY_BUFFER, survivor.buffer);
    glGetBufferSubData(GL_ARRAY_BUFFER, survivor.first * sizeof(matrix), sizeof(matrix), &matrix);
    valid &= matrix[3].x == 40 && matrix[3].z == 3;
    cache.clear();
    valid &= !glIsBuffer(secondBuffer);
    if (!valid)
        std::cerr << "Vegetation cache buffer/readiness/invalidation test failed\n";
    return noGlErrors("vegetation cache buffers") && valid;
}

bool checkMapBoundary(strategy::Renderer& renderer, const strategy::Terrain& original) {
    const strategy::Terrain terrain{
        7, strategy::TerrainLayoutId{"central_water"}, strategy::TerrainMode::v2, 10};
    renderer.stageGeneratedTerrain(terrain, 7, 10);
    strategy::CameraView camera;
    camera.projection = glm::ortho(-0.5F, 0.5F, -0.5F, 0.5F, 0.1F, 200.0F);
    const auto pixel = [&](glm::vec2 position, const strategy::Player* player) {
        camera.position = {position.x, 100, position.y};
        camera.target = {position.x, 0, position.y};
        camera.view = glm::lookAt(camera.position, camera.target, glm::vec3{0, 0, -1});
        renderer.beginProfileFrame();
        renderer.beginFrame(640, 360);
        renderer.drawTerrain(camera, player);
        renderer.endFrame();
        std::array<unsigned char, 4> result{};
        glReadPixels(320, 180, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
        return result;
    };
    const strategy::Player hidden;
    bool valid = true;
    // Beyond each playable edge, land must continue, yet respect exploration visibility.
    const float outsideEdge = strategy::MapArea{10}.extent() * 0.5F + 2.0F;
    for (glm::vec2 position : {glm::vec2{-outsideEdge, 0},
                               glm::vec2{outsideEdge, 0},
                               glm::vec2{0, -outsideEdge},
                               glm::vec2{0, outsideEdge}}) {
        const auto revealed = pixel(position, nullptr);
        const auto unexplored = pixel(position, &hidden);
        const bool hasLand = std::abs(static_cast<int>(revealed[0]) - 82) > 12 ||
                             std::abs(static_cast<int>(revealed[1]) - 92) > 12 ||
                             std::abs(static_cast<int>(revealed[2]) - 97) > 12;
        valid &= hasLand && unexplored[0] < 8 && unexplored[1] < 8 && unexplored[2] < 8;
    }
    // A mode switch must discard the V2 backdrop instead of retaining stale outside land.
    renderer.stageGeneratedTerrain(original, 12345, 10);
    const auto outside = pixel({outsideEdge, 0}, nullptr);
    valid &= outside[2] > 200 && outside[0] < 120;
    if (!valid)
        std::cerr << "Map backdrop continuity, fog or mode-switch regression\n";
    return noGlErrors("map boundary") && valid;
}

bool checkRememberedCulling(strategy::Renderer& renderer) {
    using namespace strategy;
    auto terrain = test::levelingFixture(0);
    renderer.stageGeneratedTerrain(terrain, 7, 10);
    while (!renderer.terrainUploadFinished()) {
        renderer.beginFrame(1280, 720);
        renderer.drawLoadingScreen(.5F, "MEMORY CULLING TEST");
        renderer.endFrame();
    }
    renderer.finishLoadingScreen();
    CameraView camera;
    camera.target = {0, terrain.heightAt(0, 0), 0};
    camera.position = camera.target + glm::vec3{0, 100, 0};
    camera.view = glm::lookAt(camera.position, camera.target, glm::vec3{0, 0, -1});
    camera.projection = glm::ortho(-20.F, 20.F, -20.F, 20.F, .1F, 200.F);
    World empty;
    Player player;
    std::fill(player.discovered.begin(), player.discovered.end(), 255);
    player.intelligence.push_back({99999, "basic_generator", {0, 0, 0}, {0, 0, 0}, {1, 1, 1}, true});
    GLuint query = 0;
    glGenQueries(1, &query);
    const auto primitives = [&] {
        renderer.beginFrame(1280, 720);
        renderer.drawTerrain(camera, &player);
        glBeginQuery(GL_PRIMITIVES_GENERATED, query);
        renderer.drawWorld(empty, camera, &player);
        glEndQuery(GL_PRIMITIVES_GENERATED);
        renderer.endFrame();
        GLuint count = 0;
        glGetQueryObjectuiv(query, GL_QUERY_RESULT, &count);
        return count;
    };
    bool valid = primitives() > 0; // Explored but unseen memory remains drawable.
    player.intelligence.front().position.x = 10000;
    valid &= primitives() == 0; // Off-screen memory never reaches the GPU.
    player.intelligence.front().position.x = 0;
    std::fill(player.visible.begin(), player.visible.end(), 255);
    valid &= primitives() == 0; // Full satellite coverage has no remembered-only draws.
    player.visible.back() = 0;
    valid &= primitives() == 0; // Local live coverage also removes redundant memory.
    const auto cell = MapArea{10}.gridCell({0, 0}, Player::explorationCells);
    player.visible[cell.y * Player::explorationCells + cell.x + 1] = 0;
    player.intelligence.front().scale = {10, 10, 10};
    valid &= primitives() > 0; // A large object crossing a fog boundary must remain.
    std::fill(player.visible.begin(), player.visible.end(), 0);
    player.intelligence.front().position.x = 21;
    valid &= primitives() > 0; // Its origin is outside the camera, but its bounds overlap.
    glDeleteQueries(1, &query);
    if (!valid)
        std::cerr << "Remembered-object live/fog/frustum culling regression\n";
    return valid && noGlErrors("remembered entity culling");
}

bool checkPowerBuildingVisuals(strategy::Renderer& renderer) {
    using namespace strategy;
    const DefinitionRegistry definitions;
    auto terrain = test::levelingFixture(0);
    renderer.stageGeneratedTerrain(terrain, 7, 10);
    while (!renderer.terrainUploadFinished()) {
        renderer.beginFrame(1280, 720);
        renderer.drawLoadingScreen(.5F, "POWER BUILDING VISUALS");
        renderer.endFrame();
    }
    renderer.finishLoadingScreen();
    CameraView camera;
    camera.target = {0, terrain.heightAt(0, 0), 0};
    camera.position = camera.target + glm::vec3{35, 42, 48};
    camera.view = glm::lookAt(camera.position, camera.target, glm::vec3{0, 1, 0});
    camera.projection = glm::ortho(-28.F, 28.F, -16.F, 16.F, .1F, 200.F);
    Player player;
    player.id = 1;
    std::fill(player.discovered.begin(), player.discovered.end(), 1);
    std::fill(player.visible.begin(), player.visible.end(), 1);
    const auto frame = [&](const World& world) {
        renderer.beginFrame(1280, 720);
        renderer.drawTerrain(camera, &player);
        renderer.drawWorld(world, camera, &player);
        renderer.endFrame();
        std::vector<unsigned char> pixels(1280 * 720 * 4);
        glReadPixels(0, 0, 1280, 720, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        return pixels;
    };
    const auto difference = [](const auto& a, const auto& b) {
        std::size_t count = 0;
        for (std::size_t i = 0; i < a.size(); i += 4)
            if (std::abs(int(a[i]) - int(b[i])) + std::abs(int(a[i + 1]) - int(b[i + 1])) +
                    std::abs(int(a[i + 2]) - int(b[i + 2])) >
                25)
                ++count;
        return count;
    };
    World world;
    const auto ground = frame(world);
    auto& mine = world.createEntity("Empty mine", "synthetic_mine", 1);
    definitions.initializeEntity(mine);
    mine.resource.remaining = 0;
    mine.transform.position = {0, 0, 6};
    const auto emptyMine = frame(world);
    bool valid = difference(ground, emptyMine) > 500;
    const auto clip = camera.viewProjection() * glm::vec4{0, terrain.heightAt(0, 6) + 2, 6, 1};
    valid &=
        renderer.pickEntity(
            (clip.x / clip.w + 1) * 640, (1 - clip.y / clip.w) * 360, world, camera, &player) ==
        mine.id;
    const auto a = world.createEntity("Pole", "electricity_pole", 1).id;
    definitions.initializeEntity(*world.findEntity(a));
    world.findEntity(a)->transform.position = {-12, 0, -6};
    const auto b = world.createEntity("Pole", "electricity_pole", 1).id;
    definitions.initializeEntity(*world.findEntity(b));
    world.findEntity(b)->transform.position = {12, 0, -6};
    const auto disconnected = frame(world);
    world.findEntity(a)->power.connections = {b};
    world.findEntity(b)->power.connections = {a};
    const auto connected = frame(world);
    valid &= difference(disconnected, connected) > 150;
    valid &= captureTerrain("power-cords-empty-mine.bmp");
    world.findEntity(b)->construction.emplace();
    world.findEntity(b)->construction.state = BuildingLifecycleState::planned;
    frame(world);
    valid &= captureTerrain("power-cords-planned.bmp");
    // Cables belong to the owner's view, never a hidden enemy network.
    world.findEntity(a)->authority.owner = 2;
    world.findEntity(b)->authority.owner = 2;
    const auto enemyLinked = frame(world);
    world.findEntity(a)->power.connections.clear();
    world.findEntity(b)->power.connections.clear();
    valid &= difference(enemyLinked, frame(world)) == 0;
    if (!valid)
        std::cerr << "Power cord rendering or empty mine presentation failed\n";
    return valid && noGlErrors("power building visuals");
}

bool checkExploredStaticVisibility(strategy::Renderer& renderer) {
    using namespace strategy;
    const DefinitionRegistry definitions;
    auto terrain = test::levelingFixture(0);
    renderer.stageGeneratedTerrain(terrain, 7, 10);
    while (!renderer.terrainUploadFinished()) {
        renderer.beginFrame(1280, 720);
        renderer.drawLoadingScreen(.5F, "EXPLORED LANDSCAPE TEST");
        renderer.endFrame();
    }
    renderer.finishLoadingScreen();
    CameraView camera;
    camera.target = {0, terrain.heightAt(0, 0), 0};
    camera.position = camera.target + glm::vec3{0, 100, 0};
    camera.view = glm::lookAt(camera.position, camera.target, glm::vec3{0, 0, -1});
    camera.projection = glm::ortho(-20.F, 20.F, -20.F, 20.F, .1F, 200.F);
    Player player;
    player.id = 1;
    World empty, resources, enemies;
    auto& node = resources.createEntity("Scrap", "scrap_node_small", 0);
    definitions.initializeEntity(node);
    node.transform.scale = {4, 4, 4};
    const auto nodeId = node.id;
    auto& enemy = enemies.createEntity("Enemy", "worker", 2);
    definitions.initializeEntity(enemy);
    enemy.transform.scale = {4, 4, 4};
    VegetationChunk chunk;
    chunk.radius = 20;
    chunk.instances.push_back({EntityArchetypeId{"foliage_conifer_1"},
                               PresentationId{"foliage_conifer_1"}, Transform{}, 90001});
    const VegetationField vegetation{{chunk}};
    struct Image {
        std::vector<unsigned char> color = std::vector<unsigned char>(1280 * 720 * 4);
        std::vector<float> depth = std::vector<float>(1280 * 720);
    };
    const auto frame = [&](const World& world, bool trees = false) {
        renderer.beginFrame(1280, 720);
        renderer.drawTerrain(camera, &player);
        renderer.drawWorld(world, camera, &player);
        if (trees)
            renderer.drawVegetation(vegetation, camera, &player);
        renderer.endFrame();
        Image image;
        glReadPixels(0, 0, 1280, 720, GL_RGBA, GL_UNSIGNED_BYTE, image.color.data());
        glReadPixels(0, 0, 1280, 720, GL_DEPTH_COMPONENT, GL_FLOAT, image.depth.data());
        return image;
    };
    const auto measure = [](const Image& ground, const Image& objects) {
        std::size_t count = 0;
        double brightness = 0;
        for (std::size_t i = 0; i < ground.depth.size(); ++i)
            if (objects.depth[i] < ground.depth[i] - 1e-6F) {
                ++count;
                brightness += objects.color[i * 4] + objects.color[i * 4 + 1] + objects.color[i * 4 + 2];
            }
        return std::pair{count, count ? brightness / count : 0};
    };
    const auto hidden = frame(empty);
    bool valid = measure(hidden, frame(resources)).first == 0 &&
                 measure(hidden, frame(empty, true)).first == 0 &&
                 renderer.pickEntity(640, 360, resources, camera, &player, true) == 0;
    std::fill(player.discovered.begin(), player.discovered.end(), 255);
    const auto explored = frame(empty);
    const auto dimResource = measure(explored, frame(resources));
    valid &= dimResource.first > 20 &&
             renderer.pickEntity(640, 360, resources, camera, &player, true) == nodeId;
    resources.findEntity(nodeId)->resource.fieldId = 17;
    const auto dimField = measure(explored, frame(resources));
    const auto dimTrees = measure(explored, frame(empty, true));
    valid &= dimField.first > 20 && dimTrees.first > 20 &&
             measure(explored, frame(enemies)).first == 0 &&
             renderer.pickEntity(640, 360, enemies, camera, &player, true) == 0;
    std::fill(player.visible.begin(), player.visible.end(), 255);
    const auto live = frame(empty);
    const auto brightField = measure(live, frame(resources));
    frame(resources); // A visibility change must reuse the warmed field allocation.
    valid &= renderer.resourceNodeCacheStats().uploadedBatches == 0;
    const auto brightTrees = measure(live, frame(empty, true));
    valid &= renderer.vegetationCacheStats().uploadedBatches == 0;
    valid &= brightField.first > 20 && brightTrees.first > 20 &&
             dimField.second < brightField.second * .65 && dimField.second > brightField.second * .2 &&
             dimTrees.second < brightTrees.second * .65 && dimTrees.second > brightTrees.second * .2 &&
             measure(live, frame(enemies)).first > 20;
    std::cout << "Explored field pixels=" << dimField.first << " brightness=" << dimField.second
              << "/" << brightField.second << " trees=" << dimTrees.first << " brightness="
              << dimTrees.second << "/" << brightTrees.second << '\n';
    if (!valid)
        std::cerr << "Explored resources/decoration, enemy fog or static cache regression\n";
    return valid && noGlErrors("explored static visibility");
}

bool captureForm(strategy::Renderer& renderer, const char* name) {
    const char* directory = SDL_getenv("STRATEGY_FORM_SCREENSHOTS");
    if (!directory)
        return true;
    renderer.setUiProperty("shell", "animation", "none");
    renderer.beginFrame(1280, 720);
    renderer.drawRmlUi();
    renderer.endFrame();
    glFinish();
    std::filesystem::create_directories(directory);
    std::vector<unsigned char> pixels(1280 * 720 * 4), flipped(pixels.size());
    glReadPixels(0, 0, 1280, 720, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    for (int row = 0; row < 720; ++row)
        std::copy_n(pixels.data() + row * 1280 * 4, 1280 * 4,
                    flipped.data() + (719 - row) * 1280 * 4);
    SDL_Surface* surface = SDL_CreateSurfaceFrom(1280, 720, SDL_PIXELFORMAT_RGBA32, flipped.data(), 1280 * 4);
    const bool saved = surface && SDL_SaveBMP(surface, (std::filesystem::path(directory) / name).string().c_str());
    SDL_DestroySurface(surface);
    return saved;
}

bool checkMatchPreparation(strategy::Renderer& renderer, strategy::Logger& logger) {
    using namespace strategy;
    DefinitionRegistry definitions;
    AudioSystem audio;
    EventBus events;
    StateContext context{audio, events, logger, definitions, "gamedata/config.json", &renderer};
    MatchSetupOptions setup;
    setup.terrainMode = TerrainMode::v2;
    setup.mapChunksPerSide = 10;
    setup.terrainSeed = 17;
    setup.terrainLayout = "open_plains";
    const auto checksum = [](const PlayState& state) {
        const auto& session = state.session();
        return authoritativeStateChecksum(session.world(),
                                          session.players(),
                                          session.terrainSeed(),
                                          session.tick(),
                                          session.mapChunksPerSide(),
                                          session.terrainLayout().value,
                                          session.terrain().mode());
    };
    const auto prepare = [&](PlayState& state) {
        const auto before = checksum(state);
        state.beginPresentationPreparation(renderer);
        bool ready = false;
        float previous = 0;
        int steps = 0;
        const auto started = std::chrono::steady_clock::now();
        while (!ready && std::chrono::steady_clock::now() - started < std::chrono::seconds(90)) {
            renderer.beginFrame(1280, 720);
            ready = state.advancePresentationPreparation(renderer);
            if (state.presentationPreparationProgress() < previous)
                return false;
            previous = state.presentationPreparationProgress();
            renderer.drawLoadingScreen(.8F + .19F * previous, "PREPARING BATTLEFIELD");
            renderer.endFrame();
            ++steps;
            if (previous >= .9F && previous < 1)
                captureTerrain("startup-preparing.bmp");
            // The loading document must remain on top after preparing the gameplay HUD.
            if (!renderer.uiBounds("loading-title"))
                return false;
        }
        if (!ready || steps < 5 || checksum(state) != before)
            return false;
        renderer.drawLoadingScreen(1, "CLICK TO START");
        captureTerrain("startup-ready.bmp");
        renderer.finishLoadingScreen();
        renderer.beginFrame(1280, 720);
        const auto first = std::chrono::steady_clock::now();
        state.render(renderer);
        renderer.endFrame();
        glFinish();
        const auto milliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - first)
                .count();
        std::cout << "Prepared first gameplay frame: " << milliseconds
                  << " ms, loading steps=" << steps << '\n';
        return renderer.resourceSiteUploadedTiles() == 0 &&
               renderer.vegetationCacheStats().uploadedBatches == 0 &&
               renderer.resourceNodeCacheStats().uploadedBatches == 0 &&
               renderer.uiBounds("top-shell").has_value() && checksum(state) == before &&
               noGlErrors("prepared match entry");
    };
    bool valid = true;
    const auto path = std::filesystem::temp_directory_path() / "strategy_prepared_entry_save.json";
    {
        PlayState state{context, setup};
        // Cancel during terrain/tile preparation; the next job must start with a fresh identity.
        state.beginPresentationPreparation(renderer);
        renderer.cancelPresentationPreparation();
        valid &= !renderer.presentationFenceReady();
        valid &= prepare(state);
        const auto& session = state.session();
        SaveGame::write(path,
                        session.terrainSeed(),
                        session.world(),
                        &session.players(),
                        session.mapChunksPerSide(),
                        session.terrainLayout().value,
                        TerrainMode::v2,
                        session.terrain().repairLevel());
    }
    {
        WorldGenerationProgress progress;
        auto restoring = std::async(std::launch::async, [&] {
            return std::make_unique<PlayState>(context, SaveGame::read(path), &progress);
        });
        auto restored = restoring.get();
        valid &= prepare(*restored);
        WorldGenerationProgress cancelled;
        cancelled.requestCancellation();
        bool aborted = false;
        try {
            PlayState abandoned{context, SaveGame::read(path), &cancelled};
        } catch (const WorldGenerationCancelled&) {
            aborted = true;
        }
        valid &= aborted;
    }
    std::filesystem::remove(path);
    setup.terrainMode = TerrainMode::v1;
    {
        PlayState legacy{context, setup};
        valid &= prepare(legacy);
    }
    renderer.finishLoadingScreen();
    return valid;
}

bool checkKeybindSettings(strategy::Renderer& renderer, strategy::Logger& logger) {
    using namespace strategy;
    DefinitionRegistry definitions;
    AudioSystem audio;
    EventBus events;
    const auto directory = std::filesystem::path{"build/debug/audits/keybind-settings"};
    std::filesystem::create_directories(directory);
    const auto configPath = directory / "config.json";
    GameConfig{}.write(configPath);
    StateContext context{audio, events, logger, definitions, configPath, &renderer};
    SettingsState settings{context};
    settings.update(.6F);
    const auto frame = [&] {
        renderer.beginFrame(1280, 720);
        settings.render(renderer);
        renderer.endFrame();
    };
    const auto click = [&](const std::string& id) {
        const auto bounds = renderer.uiBounds(id);
        if (!bounds)
            throw std::runtime_error("Missing settings control: " + id);
        const float x = (bounds->left + bounds->right) * .5F,
                    y = (bounds->top + bounds->bottom) * .5F;
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.x = x;
        event.button.y = y;
        settings.handleEvent(event);
        event.type = SDL_EVENT_MOUSE_BUTTON_UP;
        settings.handleEvent(event);
        frame();
    };
    const auto key =
        [&](SDL_EventType type, SDL_Keycode code, SDL_Keymod modifiers = SDL_KMOD_NONE) {
            SDL_Event event{};
            event.type = type;
            event.key.key = code;
            event.key.mod = modifiers;
            settings.handleEvent(event);
            frame();
        };
    const auto filter = [&](const std::string& text) {
        renderer.setUiValue("keybind-search", text);
        settings.update(.01F);
        frame();
    };
    frame();
    bool valid = captureTerrain("settings-video.bmp");
    click("settings.tab.audio");
    valid &= captureTerrain("settings-audio.bmp");
    click("settings.tab.keybinds");
    frame();
    valid &= captureTerrain("settings-keybinds.bmp");
    click("keybind-search");
    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.text = "power";
    settings.handleEvent(text);
    settings.update(.01F);
    frame();
    valid &= renderer.uiValue("keybind-search") == "power";
    filter("Maintain power");
    click("settings.bind.range_lock.0");
    key(SDL_EVENT_KEY_DOWN, SDLK_LALT, SDL_KMOD_LALT);
    key(SDL_EVENT_KEY_UP, SDLK_LALT);
    click("settings.apply");
    valid &= context.settingsRevision == 1 &&
             GameConfig::load(configPath).keybinds.values.at("range_lock")[0] ==
                 InputBinding{BindingDevice::modifier, 4};
    click("settings.bind.range_lock.1");
    key(SDL_EVENT_KEY_DOWN, SDLK_L);
    key(SDL_EVENT_KEY_UP, SDLK_L);
    click("settings.apply");
    valid &= GameConfig::load(configPath).keybinds.values.at("range_lock")[1].code ==
             static_cast<int>(SDLK_L);
    click("settings.bind.range_lock.0");
    key(SDL_EVENT_KEY_DOWN, SDLK_LSHIFT, SDL_KMOD_LSHIFT);
    key(SDL_EVENT_KEY_UP, SDLK_LSHIFT);
    valid &= captureTerrain("settings-keybind-conflict.bmp");
    const auto before = context.settingsRevision;
    click("settings.capture.cancel");
    valid &= context.settingsRevision == before &&
             GameConfig::load(configPath).keybinds.values.at("range_lock")[0].code == 4;
    click("settings.bind.range_lock.0");
    key(SDL_EVENT_KEY_DOWN, SDLK_LSHIFT, SDL_KMOD_LSHIFT);
    key(SDL_EVENT_KEY_UP, SDLK_LSHIFT);
    click("settings.capture.replace");
    click("settings.apply");
    valid &= GameConfig::load(configPath).keybinds.values.at("repeat_build")[0].code == 0;
    filter("");
    renderer.setUiValue("keybind-category", "building");
    settings.update(.01F);
    frame();
    valid &= captureTerrain("settings-building-bindings.bmp");
    click("settings.reset.category");
    click("settings.apply");
    valid &= GameConfig::load(configPath).keybinds.values.at("range_lock")[0].code == 1;
    valid &= captureTerrain("settings-building-defaults.bmp");
    renderer.setUiValue("keybind-category", "");
    settings.update(.01F);
    frame();
    click("settings.preset.grid");
    click("settings.apply");
    valid &= GameConfig::load(configPath).keybinds.values.at("forward")[0].code ==
             static_cast<int>(SDLK_UP);
    for (float scale : {.75F, 1.5F}) {
        renderer.setUiScale(scale);
        frame();
        valid &= captureTerrain(scale > 1 ? "settings-keybinds-large.bmp"
                                          : "settings-keybinds-small.bmp");
        const auto list = renderer.uiBounds("binding-list"),
                   apply = renderer.uiBounds("settings.apply");
        valid &= list && apply && list->top >= 0 && list->bottom - list->top >= 70 &&
                 list->bottom < apply->top && apply->bottom <= 720;
    }
    renderer.setUiScale(1);
    frame();
    if (!valid)
        std::cerr << "Keybind settings capture/filter/layout failed\n";
    return valid && noGlErrors("keybind settings");
}

bool checkPlacementControls(strategy::Renderer& renderer,
                            strategy::Logger& logger,
                            bool satellite = false,
                            bool powerAssist = false,
                            bool repeatBuild = false,
                            bool navigation = false) {
    using namespace strategy;
    DefinitionRegistry definitions;
    GameSession session{definitions,
                        7,
                        "spain",
                        "japan",
                        "unassigned",
                        "unassigned",
                        10,
                        1,
                        1,
                        TerrainLayoutId{"open_plains"},
                        nullptr,
                        TerrainMode::v2};
    session.players().find(1)->resources["alloy"] = 10000;
    Entity* blocker = nullptr;
    Entity* drone = nullptr;
    glm::vec3 hub{};
    EntityId hubId{};
    for (auto& entity : session.world().entities()) {
        if (entity.authority.owner != 1)
            continue;
        if (entity.archetype.value == "worker")
            blocker = &entity;
        if (entity.archetype.value == "construction_drone")
            drone = &entity;
        if (entity.archetype.value == "command_hub") {
            hub = entity.transform.position;
            hubId = entity.id;
        }
    }
    if (!blocker || !drone)
        return false;
    const auto originalBlocker = blocker->transform;
    const auto* generator = definitions.archetype("basic_generator");
    glm::vec3 target{};
    bool found = false;
    const float searchRadius = satellite ? 48.F : 24.F;
    for (float z = hub.z - searchRadius; z <= hub.z + searchRadius && !found; z += 2)
        for (float x = hub.x - searchRadius; x <= hub.x + searchRadius && !found; x += 2) {
            const auto cell = MapArea{10}.gridCell({x, z}, Player::explorationCells);
            const auto index = cell.y * Player::explorationCells + cell.x;
            const auto* player = session.players().find(1);
            bool eligible = satellite ? !player->discovered[index] : player->visible[index] != 0;
            if (powerAssist) {
                const glm::vec2 direction{x - hub.x, z - hub.z};
                const float distance = glm::length(direction);
                eligible = eligible && distance >= 16 && distance <= 22;
                if (eligible) {
                    const glm::vec2 far = glm::vec2{hub.x, hub.z} + direction / distance * 40.0F;
                    const auto edge = clampPowerPlacementPosition(
                        {far.x, 0, far.y}, *session.world().findEntity(hubId), 24);
                    eligible =
                        session
                            .planBuildingPlacement(
                                1, {drone->id, "construct.basic_generator", edge, {drone->id}, 0})
                            .valid();
                }
            }
            if (eligible &&
                !overlapsObject(
                    session.world(),
                    definitions,
                    spatialShape(definitions, EntityArchetypeId{"basic_generator"}, {x, z}, 0)) &&
                session.terrain()
                    .evaluatePlacement(x, z, *generator->footprint, generator->placement)
                    .valid()) {
                target = {x, session.terrain().heightAt(x, z), z};
                found = true;
            }
        }
    if (!found)
        return false;
    if (satellite) {
        const auto* player = session.players().find(1);
        session.restorePlayerProgress(1, player->resources, player->discovered, player->intelligence,
                                      {{"satellite.low_resolution", 1}});
    }
    RtsCamera projectionCamera;
    projectionCamera.focusAt(hub);
    const auto camera =
        projectionCamera.view(1280.0F / 720, session.terrain().heightAt(hub.x, hub.z));
    const auto clip = camera.viewProjection() * glm::vec4{target, 1};
    const glm::vec2 targetScreen{(clip.x / clip.w + 1) * 640, (1 - clip.y / clip.w) * 360};
    renderer.stageGeneratedTerrain(session.terrain(), 7, 10, session.world().foundations());
    AudioSystem audio;
    EventBus events;
    const auto testDirectory = std::filesystem::path{"build/debug/audits"} /
        ("strategy-satellite-ui-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(testDirectory);
    auto testConfig = GameConfig::load("gamedata/config.json");
    testConfig.saveDirectory = testDirectory;
    testConfig.saveFile = "quickload.json";
    const auto configPath = testDirectory / "config.json";
    testConfig.write(configPath);
    StateContext context{audio, events, logger, definitions, configPath, &renderer};
    PlayState state{context, MatchSetupOptions{}, std::move(session)};
    state.update(.5F); // Finish the entry fade before inspecting placement feedback.
    const auto frame = [&] {
        renderer.beginFrame(1280, 720);
        state.render(renderer);
        renderer.endFrame();
    };
    for (int i = 0; i < 200; ++i) {
        frame();
        if (renderer.terrainUploadFinished())
            break;
    }
    const auto panel = [&] {
        auto* context = Rml::GetContext("strategy_ui");
        for (int i = 0; i < context->GetNumDocuments(); ++i)
            if (auto* element = context->GetDocument(i)->GetElementById("placement-reason"))
                return element;
        return static_cast<Rml::Element*>(nullptr);
    };
    const auto motion = [&](float x, float y) {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.x = x;
        event.motion.y = y;
        state.handleEvent(event);
    };
    const auto button = [&](SDL_EventType type, Uint8 button, float x, float y) {
        SDL_Event event{};
        event.type = type;
        event.button.button = button;
        event.button.x = x;
        event.button.y = y;
        event.button.clicks = 1;
        state.handleEvent(event);
    };
    const auto activate = [&] {
        const auto bounds = renderer.uiBounds("action.construct.basic_generator");
        if (!bounds)
            return false;
        const float x = (bounds->left + bounds->right) * .5F,
                    y = (bounds->top + bounds->bottom) * .5F;
        motion(x, y);
        button(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, x, y);
        button(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, x, y);
        motion(1265, 700);
        frame();
        return panel() && panel()->IsVisible();
    };
    bool valid = activate();
    if (navigation) {
        const auto check = [&](bool okay, const char* stage) {
            if (!okay)
                std::cerr << "Navigation stage failed: " << stage << "\n";
            valid &= okay;
        };
        const auto key = [&](SDL_Keycode code, SDL_Keymod mods = SDL_KMOD_NONE) {
            SDL_Event e{};
            e.type = SDL_EVENT_KEY_DOWN;
            e.key.key = code;
            e.key.mod = mods;
            state.handleEvent(e);
            e.type = SDL_EVENT_KEY_UP;
            state.handleEvent(e);
            frame();
        };
        key(SDLK_ESCAPE);
        key(SDLK_H);
        check(renderer.uiBounds("action.command_hub.train_construction_drone").has_value(),
              "hub focus");
        key(SDLK_PERIOD);
        check(renderer.uiBounds("action.construct.basic_generator").has_value(), "idle drone");
        key(SDLK_1, SDL_KMOD_LCTRL);
        key(SDLK_H);
        key(SDLK_1);
        check(renderer.uiBounds("action.construct.basic_generator").has_value(), "group recall");
        key(SDLK_P);
        state.update(1.F / 30);
        const Entity* controlled = nullptr;
        for (const auto& e : state.session().world().entities())
            if (e.authority.directController == 1)
                controlled = &e;
        if (!controlled)
            return false;
        const auto yaw = controlled->transform.rotationDegrees.y;
        SDL_Event motionEvent{};
        motionEvent.type = SDL_EVENT_MOUSE_MOTION;
        motionEvent.motion.xrel = 40;
        motionEvent.motion.yrel = 5;
        state.handleEvent(motionEvent);
        state.update(1.F / 30);
        check(std::abs(controlled->transform.rotationDegrees.y - yaw) > .1F, "direct camera orbit");
        SDL_Event movement{};
        movement.type = SDL_EVENT_KEY_DOWN;
        movement.key.key = SDLK_W;
        state.handleEvent(movement);
        state.update(1.F / 30);
        check(glm::length(controlled->unitControl.directInput) > .9F, "direct movement");
        movement.type = SDL_EVENT_KEY_UP;
        state.handleEvent(movement);
        state.update(1.F / 30);
        check(glm::length(controlled->unitControl.directInput) == 0, "movement release");
        key(SDLK_P);
        state.update(1.F / 30);
        check(controlled->authority.directController == 0, "leave direct control");
        if (!valid)
            std::cerr << "Navigation, groups or direct-control input failed\n";
        return valid && noGlErrors("navigation hotkeys");
    }
    if (repeatBuild) {
        const auto project = [&](glm::vec3 p) {
            p.y = state.session().terrain().heightAt(p.x, p.z);
            const auto clip = camera.viewProjection() * glm::vec4{p, 1};
            return glm::vec2{(clip.x / clip.w + 1) * 640, (1 - clip.y / clip.w) * 360};
        };
        EntityId first{};
        std::vector<EntityId> placed;
        for (int i = 0; i < 3; ++i) {
            glm::vec3 point = target;
            if (i) {
                bool available = false;
                for (float z = hub.z - 36; z <= hub.z + 36 && !available; z += 3)
                    for (float x = hub.x - 36; x <= hub.x + 36 && !available; x += 3) {
                        PlaceBuildingCommand intent{drone->id,
                                                    "construct.basic_generator",
                                                    {x, 0, z},
                                                    {drone->id},
                                                    0,
                                                    true};
                        const auto cell = MapArea{10}.gridCell({x, z}, Player::explorationCells);
                        const auto index = cell.y * Player::explorationCells + cell.x;
                        const auto screen = project(intent.position);
                        if (state.session().players().find(1)->discovered[index] && screen.x > 20 &&
                            screen.x < 1100 && screen.y > 80 && screen.y < 350 &&
                            state.session().planBuildingPlacement(1, intent).valid()) {
                            point = intent.position;
                            available = true;
                        }
                    }
                if (!available)
                    return false;
            }
            const auto screen = project(point);
            SDL_SetModState(i < 2 ? SDL_KMOD_LSHIFT : SDL_KMOD_NONE);
            motion(screen.x, screen.y);
            frame();
            const auto before = state.session().world().size();
            button(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, screen.x, screen.y);
            button(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, screen.x, screen.y);
            frame();
            state.update(1.F / 30);
            frame();
            if (state.session().world().size() != before + 1) {
                std::cerr << "Repeat placement rejected step " << i << " at " << point.x << ","
                          << point.z << " panel=" << (panel() ? panel()->GetInnerRML() : "missing")
                          << "\n";
                return false;
            }
            const auto id = state.session().world().entities().back().id;
            placed.push_back(id);
            if (i == 0)
                first = id;
            valid &=
                state.session().world().findEntity(drone->id)->unitControl.orderTarget == first;
            valid &= panel() && panel()->IsVisible() == (i < 2);
        }
        const auto* builder = state.session().world().findEntity(drone->id);
        valid &=
            builder->unitControl.constructionQueue == std::deque<EntityId>{placed[1], placed[2]};
        valid &= captureTerrain("repeat-placement-queue.bmp");
        if (!valid)
            std::cerr << "Shift repeat-placement did not retain FIFO construction\n";
        return valid && noGlErrors("repeat placement controls");
    }
    if (powerAssist) {
        motion(targetScreen.x, targetScreen.y);
        frame();
        frame();
        valid &= panel() && panel()->GetInnerRML().find("CONNECTS TO") != std::string::npos;
        SDL_SetModState(SDL_KMOD_LCTRL);
        frame();
        const glm::vec2 direction = glm::normalize(glm::vec2{target.x - hub.x, target.z - hub.z});
        const glm::vec2 far = glm::vec2{hub.x, hub.z} + direction * 40.0F;
        const glm::vec3 farGround{far.x, state.session().terrain().heightAt(far.x, far.y), far.y};
        const auto farClip = camera.viewProjection() * glm::vec4{farGround, 1};
        const glm::vec2 farScreen{(farClip.x / farClip.w + 1) * 640,
                                  (1 - farClip.y / farClip.w) * 360};
        motion(farScreen.x, farScreen.y);
        frame();
        frame();
        valid &= panel() &&
                 panel()->GetInnerRML().find(Text::format("placement.power.locked", {"Ctrl"})) !=
                     std::string::npos;
        valid &= captureTerrain("power-placement-locked.bmp");
        if (const auto bounds = renderer.uiBounds("placement-reason"))
            valid &= bounds->left >= 0 && bounds->top >= 0 && bounds->right <= 1280 &&
                     bounds->bottom <= 720;
        const auto before = state.session().world().size();
        button(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, farScreen.x, farScreen.y);
        button(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, farScreen.x, farScreen.y);
        SDL_SetModState(
            SDL_KMOD_NONE); // Click must retain its own modifier, even before the next render.
        frame();
        state.update(1.0F / 30);
        frame();
        valid &= state.session().world().size() == before + 1;
        if (state.session().world().size() == before + 1) {
            const auto& placed = state.session().world().entities().back();
            const float distance = glm::length(glm::vec2{placed.transform.position.x - hub.x,
                                                         placed.transform.position.z - hub.z});
            valid &= distance > 23.9F && distance <= 24 && placed.power.connections.size() == 1 &&
                     placed.power.connections.front() == hubId;
        }
        {
            SettingsState settings{context};
            settings.update(.6F);
            const auto settingsFrame = [&] {
                renderer.beginFrame(1280, 720);
                settings.render(renderer);
                renderer.endFrame();
            };
            const auto settingsClick = [&](const std::string& id) {
                const auto bounds = renderer.uiBounds(id);
                if (!bounds)
                    throw std::runtime_error("Missing live settings action: " + id);
                SDL_Event e{};
                e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                e.button.button = SDL_BUTTON_LEFT;
                e.button.x = (bounds->left + bounds->right) * .5F;
                e.button.y = (bounds->top + bounds->bottom) * .5F;
                settings.handleEvent(e);
                e.type = SDL_EVENT_MOUSE_BUTTON_UP;
                settings.handleEvent(e);
                settingsFrame();
            };
            settingsFrame();
            settingsClick("settings.tab.keybinds");
            renderer.setUiValue("keybind-search", "Maintain power");
            settings.update(.01F);
            settingsFrame();
            settingsClick("settings.bind.range_lock.0");
            SDL_Event e{};
            e.type = SDL_EVENT_KEY_DOWN;
            e.key.key = SDLK_LALT;
            e.key.mod = SDL_KMOD_LALT;
            settings.handleEvent(e);
            e.type = SDL_EVENT_KEY_UP;
            e.key.mod = SDL_KMOD_NONE;
            settings.handleEvent(e);
            settingsFrame();
            settingsClick("settings.apply");
        }
        state.update(0);
        frame();
        valid &= activate();
        motion(targetScreen.x, targetScreen.y);
        frame();
        SDL_SetModState(SDL_KMOD_LALT);
        frame();
        frame();
        valid &= panel() && panel()->GetInnerRML().find(Text::format("placement.power.locked",
                                                                     {"Alt"})) != std::string::npos;
        valid &= captureTerrain("power-placement-live-rebind.bmp");
        SDL_SetModState(SDL_KMOD_NONE);
        SDL_Event key{};
        key.type = SDL_EVENT_KEY_DOWN;
        key.key.key = SDLK_ESCAPE;
        state.handleEvent(key);
        key.type = SDL_EVENT_KEY_UP;
        state.handleEvent(key);
        key.type = SDL_EVENT_KEY_DOWN;
        key.key.key = SDLK_H;
        state.handleEvent(key);
        key.type = SDL_EVENT_KEY_UP;
        state.handleEvent(key);
        frame();
        valid &= renderer.uiBounds("action.power.connection").has_value() &&
                 !renderer.uiBounds("action.power.connect").has_value() &&
                 !renderer.uiBounds("action.power.disconnect").has_value() &&
                 !renderer.uiBounds("action.power.discharge").has_value();
        valid &= noGlErrors("assisted power placement");
        if (!valid)
            std::cerr << "Power placement controls or click/preview agreement failed\n";
        return valid;
    }

    valid &= captureTerrain("placement-controls.bmp");
    if (auto bounds = renderer.uiBounds("placement-reason"))
        valid &=
            bounds->left >= 0 && bounds->top >= 0 && bounds->right <= 1280 && bounds->bottom <= 720;
    button(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT, 800, 300);
    button(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_RIGHT, 800, 300);
    state.update(1.0F / 30);
    frame();
    valid &= panel() && panel()->IsVisible();
    valid &= drone->unitControl.order == UnitOrderKind::idle;
    SDL_Event escape{};
    escape.type = SDL_EVENT_KEY_DOWN;
    escape.key.key = SDLK_ESCAPE;
    state.handleEvent(escape);
    frame();
    valid &= panel() && !panel()->IsVisible();
    valid &= activate();
    motion(targetScreen.x, targetScreen.y);
    frame();
    frame();
    valid &= renderer.resourceNodeCacheStats().rebuiltChunks == 0 &&
             renderer.resourceNodeCacheStats().uploadedBatches == 0;
    button(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, targetScreen.x, targetScreen.y);
    button(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, targetScreen.x, targetScreen.y);
    frame();
    // The preview was valid; a ground unit moves into it before the simulation commits.
    blocker->transform.position = {target.x, 0, target.z};
    state.update(1.0F / 30);
    frame();
    valid &= panel() && panel()->IsVisible() &&
             panel()->GetInnerRML().find(Text::get("placement.collision")) != std::string::npos;
    valid &= captureTerrain("placement-rejected.bmp");
    blocker->transform = originalBlocker;
    motion(targetScreen.x, targetScreen.y);
    frame();
    button(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, targetScreen.x, targetScreen.y);
    button(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, targetScreen.x, targetScreen.y);
    button(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT, targetScreen.x, targetScreen.y);
    button(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_RIGHT, targetScreen.x, targetScreen.y);
    motion(640, 360); // New hover is blocked by the hub; the clicked intent remains legal.
    button(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 640, 360);
    button(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, 640, 360); // Cannot overwrite the pending click.
    const auto before = state.session().world().size();
    frame();
    state.update(1.0F / 30);
    frame();
    valid &= state.session().world().size() == before + 1;
    valid &= state.session().world().entities().back().transform.rotationDegrees.y == 45;
    valid &= panel() && !panel()->IsVisible();
    valid &= activate();
    button(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT, 800, 300);
    SDL_Event drag{};
    drag.type = SDL_EVENT_MOUSE_MOTION;
    drag.motion.x = 824;
    drag.motion.y = 300;
    drag.motion.xrel = 24;
    state.handleEvent(drag);
    button(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_RIGHT, 824, 300);
    frame();
    valid &= panel() && panel()->IsVisible();
    state.handleEvent(escape);
    frame();
    valid &= panel() && !panel()->IsVisible();
    const auto resourcesBeforeSatellite = state.session().players().find(1)->resources;
    if (auto bounds = renderer.uiBounds("hud.satellite")) {
        const float x = (bounds->left + bounds->right) * .5F;
        const float y = (bounds->top + bounds->bottom) * .5F;
        for (int click = 0; click < 3; ++click) {
            motion(x, y);
            button(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, x, y);
            button(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, x, y);
            state.update(1.F / 30);
            frame();
            valid &= state.session().satelliteCoverage(1) ==
                     (!satellite && click == 0 ? SatelliteCoverage::lowResolution
                                               : SatelliteCoverage::highResolution);
            valid &= state.session().players().find(1)->resources == resourcesBeforeSatellite;
            valid &= renderer.uiBounds("action.construct.basic_generator").has_value();
        }
    } else {
        valid = false;
    }
    if (satellite) {
        auto savedPlayers = state.session().players();
        auto withoutCoverage = savedPlayers;
        auto* unknown = withoutCoverage.find(1);
        unknown->researchedUpgrades.clear();
        std::fill(unknown->discovered.begin(), unknown->discovered.end(), 0);
        unknown->resources["alloy"] = 777;
        const auto quickload = [&](const PlayerRegistry& players) {
            const auto& current = state.session();
            SaveGame::write(testConfig.savePath(), current.terrainSeed(), current.world(), &players,
                            current.mapChunksPerSide(), current.terrainLayout().value,
                            current.terrain().mode(), current.terrain().repairLevel());
            SDL_Event load{};
            load.type = SDL_EVENT_KEY_DOWN;
            load.key.key = SDLK_F9;
            state.handleEvent(load);
        };
        quickload(withoutCoverage);
        valid &= state.session().satelliteCoverage(1) == SatelliteCoverage::none &&
                 state.session().players().find(1)->resources.at("alloy") == 777;
        quickload(savedPlayers);
        const auto* restored = state.session().players().find(1);
        valid &= state.session().satelliteCoverage(1) == SatelliteCoverage::highResolution &&
                 std::all_of(restored->discovered.begin(), restored->discovered.end(),
                             [](auto cell) { return cell != 0; }) &&
                 restored->resources == savedPlayers.find(1)->resources;
        frame();
    }
    std::filesystem::remove(testConfig.savePath());
    std::filesystem::remove(configPath);
    std::filesystem::remove(testDirectory);
    if (!valid)
        std::cerr << "Placement camera controls, Escape cancellation or commit feedback failed\n";
    return valid && noGlErrors("placement controls");
}

bool checkProgress(strategy::Renderer& renderer, const char* id, Rml::Colourb tint) {
    auto* context = Rml::GetContext("strategy_ui");
    Rml::Element* progress = nullptr;
    for (int i = 0; context && i < context->GetNumDocuments(); ++i)
        if (auto* element = context->GetDocument(i)->GetElementById(id))
            progress = element;
    if (!progress || progress->GetNumChildren(true) != 1)
        return false;
    const auto originalValue = progress->GetAttribute<Rml::String>("value", "0");
    const auto originalMax = progress->GetAttribute<Rml::String>("max", "1");
    const float trackWidth = progress->GetParentNode()->GetBox().GetSize().x;
    progress->SetAttribute("max", 100);
    bool valid = progress->GetProperty<Rml::Colourb>("image-color") == tint;
    for (const auto& [value, ratio] : {std::pair{0.0F, 0.0F}, {0.25F, 0.0025F},
             {25.0F, 0.25F}, {62.5F, 0.625F}, {100.0F, 1.0F}, {150.0F, 1.0F}, {-5.0F, 0.0F}}) {
        progress->SetAttribute("value", value);
        renderer.beginFrame(1280, 720);
        renderer.drawRmlUi();
        renderer.endFrame();
        const float width = progress->GetBox().GetSize().x;
        const float fillWidth = progress->GetChild(0)->GetBox().GetSize().x;
        valid = width > 0.0F && std::abs(fillWidth - width * ratio) < 0.1F &&
                std::abs(progress->GetParentNode()->GetBox().GetSize().x - trackWidth) < 0.1F && valid;
    }
    progress->SetAttribute("max", originalMax);
    progress->SetAttribute("value", originalValue);
    if (!valid)
        std::cerr << "Progress bar fill, tint, or fixed track failed: " << id << '\n';
    return valid;
}
bool checkResourceSiteTexture() {
    {
        strategy::World nodes;
        nodes.createEntity("unrelated entity", "command_hub", 1);
        auto& first = nodes.createEntity("first", "v2_scrap_beams", 0);
        const auto firstId = first.id;
        first.resource.emplace();
        first.resource.fieldId = 1;
        first.resource.remaining = 25;
        auto& second = nodes.createEntity("second", "v2_scrap_plates", 0);
        second.resource.emplace();
        second.resource.fieldId = 2;
        second.resource.remaining = 25;
        strategy::ResourceNodePresentation presentation;
        const auto& initial = presentation.update(nodes);
        const auto identity = initial.identity();
        if (initial.chunks().size() != 2 || initial.instanceCount() != 2 ||
            presentation.update(nodes).identity() != identity)
            return false;
        nodes.findEntity(firstId)->resource.remaining = 0;
        const auto& depleted = presentation.update(nodes);
        if (depleted.identity() != identity || depleted.instanceCount() != 1 ||
            depleted.chunks()[0].revision != 1 || depleted.chunks()[1].revision != 0)
            return false;
        nodes.findEntity(firstId)->resource.remaining = 25;
        if (presentation.update(nodes).identity() == identity)
            return false;
    }
    strategy::ResourceSiteTexture texture;
    strategy::ResourceSiteMap reference;
    std::vector<strategy::ResourceSiteSource> sources{{1, {-8, 0}, 3.8F, 0}, {2, {5, 2}, 5, 0}};
    GLint oldRows = 0, oldAlignment = 0;
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &oldRows);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &oldAlignment);
    const auto compare = [&] {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 7);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
        texture.update(sources, 480);
        GLint rows = 0, alignment = 0;
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rows);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
        reference.clear();
        reference.update(sources, 480);
        std::vector<std::uint8_t> pixels(reference.pixels().size());
        texture.bind();
        glActiveTexture(GL_TEXTURE8);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glActiveTexture(GL_TEXTURE0);
        return pixels == reference.pixels() && rows == 7 && alignment == 2;
    };
    bool valid = compare();
    const auto fullUpload = texture.uploadedTiles();
    texture.beginPreparation(sources, 480);
    while (!texture.advancePreparation()) {
    }
    texture.bind();
    glActiveTexture(GL_TEXTURE8);
    std::vector<std::uint8_t> preparedPixels(reference.pixels().size());
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, preparedPixels.data());
    glActiveTexture(GL_TEXTURE0);
    valid &= preparedPixels == reference.pixels();

    valid = compare() && texture.uploadedTiles() == 0 && valid;
    sources.erase(sources.begin());
    valid =
        compare() && texture.uploadedTiles() > 0 && texture.uploadedTiles() < fullUpload && valid;
    sources.front().position = {130, -120};
    valid = compare() && valid;
    sources.clear();
    valid = compare() && valid;
    glPixelStorei(GL_UNPACK_ROW_LENGTH, oldRows);
    glPixelStorei(GL_UNPACK_ALIGNMENT, oldAlignment);
    if (!valid)
        std::cerr
            << "Resource site texture replacement/removal or pixel-store restoration failed\n";
    return noGlErrors("resource site texture cache") && valid;
}

} // namespace

int strategyTestMain() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cout << "Renderer test skipped: " << SDL_GetError() << '\n';
        return 0;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* window =
        SDL_CreateWindow("renderer-test", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) {
        std::cout << "Renderer test skipped: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 0;
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context || !gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        std::cout << "Renderer test skipped: " << SDL_GetError() << '\n';
        if (context)
            SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }

    if (const char* testCase = SDL_getenv("STRATEGY_RENDER_CASE");
        testCase && (std::string_view{testCase} == "explored_static" ||
                     std::string_view{testCase} == "power_placement" ||
                     std::string_view{testCase} == "power_buildings" ||
                     std::string_view{testCase} == "keybind_settings" ||
                     std::string_view{testCase} == "repeat_placement" ||
                     std::string_view{testCase} == "navigation_keys")) {
        bool result = false;
        {
            strategy::Logger logger{"build/debug/audits/explored-static-render.log"};
            strategy::Renderer renderer{&logger};
            renderer.preloadAssetGroup("match");
            while (!renderer.assetProgress("match").finished()) {
                renderer.beginFrame(1280, 720);
                renderer.drawLoadingScreen(.5F, "EXPLORED LANDSCAPE TEST");
                renderer.endFrame();
            }
            renderer.finishLoadingScreen();
            result = std::string_view{testCase} == "power_buildings"
                         ? checkPowerBuildingVisuals(renderer)
                     : std::string_view{testCase} == "navigation_keys"
                         ? checkPlacementControls(renderer, logger, false, false, false, true)
                     : std::string_view{testCase} == "repeat_placement"
                         ? checkPlacementControls(renderer, logger, false, false, true)
                     : std::string_view{testCase} == "keybind_settings"
                         ? checkKeybindSettings(renderer, logger)
                     : std::string_view{testCase} == "power_placement"
                         ? checkPlacementControls(renderer, logger, false, true)
                         : checkExploredStaticVisibility(renderer);
        }
        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return result ? 0 : 1;
    }

    bool valid = noGlErrors("context creation") && checkVegetationCacheBuffers() &&
                 checkResourceSiteTexture();
    const auto reportCheck = [&valid](bool result, int line) {
        if (valid && !result)
            std::cerr << "Renderer integration check failed at line " << line << "\n";
        return result;
    };
    {
        strategy::ShaderManager shaders;
        constexpr std::string_view vertex = R"(#version 450 core
void main(){gl_Position=vec4(0,0,0,1);})";
        constexpr std::string_view fragment = R"(#version 450 core
out vec4 color;void main(){color=vec4(1);})";
        const strategy::ShaderHandle first = shaders.load("integration-test", vertex, fragment);
        const strategy::ShaderHandle cached = shaders.load("integration-test", vertex, fragment);
        valid = reportCheck(first == cached &&
                                shaders.state(first) == strategy::ResourceState::ready && valid,
                            __LINE__);
        const GLint firstLocation = shaders.uniform(first, "notPresent");
        const GLint cachedLocation = shaders.uniform(first, "notPresent");
        shaders.use(first);
        valid = reportCheck(firstLocation == -1 && cachedLocation == firstLocation &&
                                noGlErrors("shader manager") && valid,
                            __LINE__);

        const std::filesystem::path shaderDirectory =
            std::filesystem::temp_directory_path() / "strategy-shader-reload-test";
        std::filesystem::create_directories(shaderDirectory);
        const std::filesystem::path vertexPath = shaderDirectory / "test.vert";
        const std::filesystem::path fragmentPath = shaderDirectory / "test.frag";
        const auto write = [](const std::filesystem::path& path, std::string_view source) {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream << source;
        };
        write(vertexPath, vertex);
        write(fragmentPath, fragment);
        const strategy::ShaderHandle fileShader =
            shaders.loadFiles("reload-test", vertexPath, fragmentPath);
        const std::uint32_t originalProgram = shaders.program(fileShader);
        write(fragmentPath, "#version 450 core\nout vec4 color;void main(){color=vec4(0,1,0,1);}");
        std::filesystem::last_write_time(
            fragmentPath, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(1));
        std::this_thread::sleep_for(std::chrono::milliseconds(260));
        const auto successfulReload = shaders.reloadChanged();
#ifdef STRATEGY_EXPECT_SHADER_HOT_RELOAD
        valid = reportCheck(successfulReload.size() == 1 && successfulReload.front().succeeded &&
                                shaders.program(fileShader) != originalProgram && valid,
                            __LINE__);
#else
        valid = reportCheck(successfulReload.empty() &&
                                shaders.program(fileShader) == originalProgram && valid,
                            __LINE__);
#endif
        const std::uint32_t validReplacement = shaders.program(fileShader);
        write(fragmentPath, "this is not valid GLSL");
        std::filesystem::last_write_time(
            fragmentPath, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(2));
        std::this_thread::sleep_for(std::chrono::milliseconds(260));
        const auto failedReload = shaders.reloadChanged();
#ifdef STRATEGY_EXPECT_SHADER_HOT_RELOAD
        valid = reportCheck(failedReload.size() == 1 && !failedReload.front().succeeded &&
                                shaders.program(fileShader) == validReplacement && valid,
                            __LINE__);
#else
        valid = reportCheck(failedReload.empty() &&
                                shaders.program(fileShader) == validReplacement && valid,
                            __LINE__);
#endif
        std::filesystem::remove(vertexPath);
        std::filesystem::remove(fragmentPath);
        std::filesystem::remove(shaderDirectory);

        strategy::Logger logger{std::filesystem::temp_directory_path() /
                                "strategy-renderer-test.log"};
        strategy::Renderer renderer{&logger};
        const strategy::Terrain generatedTerrain{12345};
        valid = reportCheck(checkMapBoundary(renderer, generatedTerrain) && valid, __LINE__);
        valid = reportCheck(checkPlacementPreview(renderer, generatedTerrain) && valid, __LINE__);
        valid = reportCheck(checkLevelingPreview(renderer, generatedTerrain) && valid, __LINE__);
        valid = reportCheck(checkTiltedPlacement(renderer, generatedTerrain) && valid, __LINE__);
        valid = reportCheck(checkGradingPreview(renderer, generatedTerrain) && valid, __LINE__);
        renderer.stageGeneratedTerrain(generatedTerrain, 12345, 10);
        valid =
            !renderer.terrainUploadFinished() && renderer.terrainUploadProgress() == 0.0F && valid;

        strategy::CameraView camera;
        camera.position = {24, 30, 35};
        camera.target = {0, 0, 0};
        camera.view = glm::lookAt(camera.position, camera.target, glm::vec3{0, 1, 0});
        camera.projection = glm::perspective(glm::radians(55.0F), 640.0F / 360.0F, 0.1F, 500.0F);

        {
            strategy::World seeps;
            auto& seep = seeps.createEntity("Terrain seep", "v2_oil_seep", 0);
            seep.kind = strategy::EntityKind::resource;
            seep.resource.emplace();
            seep.resource.remaining = 25;
            auto pickCamera = camera;
            pickCamera.target = {0, generatedTerrain.heightAt(0, 0), 0};
            pickCamera.position = pickCamera.target + glm::vec3{0, 20, 0};
            pickCamera.view =
                glm::lookAt(pickCamera.position, pickCamera.target, glm::vec3{0, 0, -1});
            renderer.beginFrame(640, 360);
            valid = reportCheck(
                renderer.pickEntity(320, 180, seeps, pickCamera) == seep.id && valid, __LINE__);
            seep.resource.remaining = 0;
            valid = reportCheck(renderer.pickEntity(320, 180, seeps, pickCamera) == 0 && valid,
                                __LINE__);
            renderer.endFrame();
        }

        strategy::World world;
        strategy::Entity& town = world.createEntity("Town Center", "town_center", 1);
        town.kind = strategy::EntityKind::building;
        town.power.emplace();
        town.power.generation = 10.0F;
        town.power.gridId = town.id;
        town.power.state = strategy::PowerOperationalState::powered;
        const strategy::EntityId townId = town.id;
        strategy::Entity& worker = world.createEntity("Worker", "worker", 1);
        worker.kind = strategy::EntityKind::unit;
        worker.transform.position = {4, 0, 2};
        worker.unitControl.emplace();
        worker.power.emplace();
        worker.power.demand = 2.0F;
        worker.power.supplied = 2.0F;
        worker.power.gridId = townId;
        worker.power.state = strategy::PowerOperationalState::powered;
        worker.power.connections = {townId};
        worker.transient.powerParent = townId;
        worker.transient.powerRoot = townId;
        worker.transient.powerReceivedLastTick = 2.0F;
        worker.transient.powerIncomingLastTick = {{townId, 2.0F}};
        const strategy::EntityId workerId = worker.id;
        strategy::Entity* stableTown = world.findEntity(townId);
        stableTown->power.connections = {workerId};
        stableTown->transient.powerRoot = townId;
        stableTown->transient.powerSentLastTick = 2.0F;
        strategy::Entity& missing = world.createEntity("Missing", "missing_test_asset", 1);
        missing.transform.position = {-4, 0, 2};
        strategy::VegetationField vegetation;
        strategy::VegetationChunk vegetationChunk;
        vegetationChunk.center = {0.0F, 0.0F};
        vegetationChunk.radius = 16.0F;
        strategy::VegetationInstance vegetationInstance;
        vegetationInstance.archetype = strategy::EntityArchetypeId{"grass_fresh"};
        vegetationInstance.presentation = strategy::PresentationId{"grass_fresh"};
        vegetationInstance.transform.position = {2.0F, 0.0F, -2.0F};
        vegetationChunk.instances.push_back(vegetationInstance);
        vegetationInstance.transform.position.x = 8;
        vegetationChunk.instances.push_back(vegetationInstance);
        strategy::VegetationChunk otherChunk;
        otherChunk.center = {40, 0};
        otherChunk.radius = 16;
        vegetationInstance.transform.position.x = 40;
        otherChunk.instances.push_back(vegetationInstance);
        vegetation = strategy::VegetationField{{std::move(vegetationChunk), std::move(otherChunk)}};

        renderer.preloadAssetGroup("match");
        strategy::AssetLoadProgress loadProgress = renderer.assetProgress("match");
        for (int frame = 0;
             frame < 400 && (!loadProgress.finished() || !renderer.terrainUploadFinished());
             ++frame) {
            renderer.beginProfileFrame();
            renderer.beginFrame(640, 360);
            renderer.drawTerrain(camera);
            renderer.drawVegetation(vegetation, camera);
            renderer.drawWorld(world, camera);
            renderer.endFrame();
            glFinish();
            valid = reportCheck(noGlErrors("frame") && valid, __LINE__);
            loadProgress = renderer.assetProgress("match");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const auto vegetationFrame = [&] {
            renderer.beginProfileFrame();
            renderer.beginFrame(1280, 720);
            renderer.drawTerrain(camera);
            renderer.drawVegetation(vegetation, camera);
            renderer.endFrame();
            glFinish();
            return renderer.vegetationCacheStats();
        };
        vegetationFrame(); // Settle the last async asset completion before cache assertions.
        auto cacheStats = vegetationFrame();
        valid = reportCheck(cacheStats.rebuiltChunks == 0 && cacheStats.uploadedBatches == 0 &&
                                cacheStats.submittedInstances == 3 && valid,
                            __LINE__);
        valid = reportCheck(
            vegetation.removeInstance(vegetation.chunks()[0].instances[0].id) && valid, __LINE__);
        cacheStats = vegetationFrame();
        valid = reportCheck(cacheStats.rebuiltChunks == 1 && cacheStats.uploadedBatches == 1 &&
                                cacheStats.submittedInstances == 2 && valid,
                            __LINE__);
        cacheStats = vegetationFrame();
        valid = reportCheck(
            cacheStats.rebuiltChunks == 0 && cacheStats.uploadedBatches == 0 && valid, __LINE__);
        valid =
            vegetation.clearWithin({strategy::FootprintShape::circle, {8, -2}, 1}) == 1 && valid;
        cacheStats = vegetationFrame();
        valid = reportCheck(cacheStats.rebuiltChunks == 1 && cacheStats.uploadedBatches == 0 &&
                                cacheStats.submittedInstances == 1 && valid,
                            __LINE__);
        vegetation = strategy::VegetationField{vegetation};
        cacheStats = vegetationFrame();
        valid = reportCheck(
            cacheStats.rebuiltChunks == 2 && cacheStats.submittedInstances == 1 && valid, __LINE__);
        renderer.stageGeneratedTerrain(generatedTerrain, 12345, 10);
        cacheStats = vegetationFrame();
        valid = reportCheck(cacheStats.rebuiltChunks == 2 && cacheStats.submittedInstances == 1 &&
                                noGlErrors("vegetation cache destruction and replacement") && valid,
                            __LINE__);
        while (!renderer.terrainUploadFinished())
            vegetationFrame();
        if (SDL_getenv("STRATEGY_TERRAIN_SCREENSHOTS")) {
            for (const char* layout :
                 {"open_plains", "rolling_hills", "central_hill", "central_water", "continental"}) {
                strategy::Terrain terrainV2{
                    7, strategy::TerrainLayoutId{layout}, strategy::TerrainMode::v2, 10};
                renderer.stageGeneratedTerrain(terrainV2, 7, 10);
                auto terrainCamera = camera;
                terrainCamera.position = {150, 205, 185};
                terrainCamera.target = {0, 12, 0};
                terrainCamera.view =
                    glm::lookAt(terrainCamera.position, terrainCamera.target, glm::vec3{0, 1, 0});
                terrainCamera.projection =
                    glm::perspective(glm::radians(55.0F), 1280.0F / 720.0F, 0.1F, 1000.0F);
                for (int frame = 0; frame < 200 && !renderer.terrainUploadFinished(); ++frame) {
                    renderer.beginProfileFrame();
                    renderer.beginFrame(1280, 720);
                    renderer.drawTerrain(terrainCamera);
                    renderer.endFrame();
                }
                renderer.beginProfileFrame();
                renderer.beginFrame(1280, 720);
                renderer.drawTerrain(terrainCamera);
                renderer.endFrame();
                valid = reportCheck(captureTerrain((std::string{layout} + ".bmp").c_str()) &&
                                        noGlErrors("V2 terrain") && valid,
                                    __LINE__);
            }
            // Same-scale captures compare composition changes, not merely texture noise.
            for (const char* layout : {"central_water", "rolling_hills"}) {
                for (std::uint32_t seed : {1U, 12345U}) {
                    const strategy::Terrain variant{
                        seed, strategy::TerrainLayoutId{layout}, strategy::TerrainMode::v2, 10};
                    renderer.stageGeneratedTerrain(variant, seed, 10);
                    auto variantCamera = camera;
                    variantCamera.position = {150, 205, 185};
                    variantCamera.target = {0, 12, 0};
                    variantCamera.view = glm::lookAt(
                        variantCamera.position, variantCamera.target, glm::vec3{0, 1, 0});
                    variantCamera.projection =
                        glm::perspective(glm::radians(55.0F), 1280.0F / 720.0F, 0.1F, 1000.0F);
                    for (int frame = 0; frame < 200 && !renderer.terrainUploadFinished(); ++frame) {
                        renderer.beginProfileFrame();
                        renderer.beginFrame(1280, 720);
                        renderer.drawTerrain(variantCamera);
                        renderer.endFrame();
                    }
                    valid = reportCheck(captureTerrain((std::string{layout} + "-seed-" +
                                                        std::to_string(seed) + ".bmp")
                                                           .c_str()) &&
                                            noGlErrors("V2 seeded composition") && valid,
                                        __LINE__);
                }
            }
            const strategy::DefinitionRegistry gameplay;
            strategy::GameSession v2Session{gameplay,
                                            7,
                                            "spain",
                                            "japan",
                                            "unassigned",
                                            "unassigned",
                                            10,
                                            1,
                                            1,
                                            strategy::TerrainLayoutId{"central_water"},
                                            nullptr,
                                            strategy::TerrainMode::v2};
            renderer.stageGeneratedTerrain(
                v2Session.terrain(), 7, 10, v2Session.world().foundations());
            auto reviewCamera = camera;
            reviewCamera.projection =
                glm::perspective(glm::radians(55.0F), 1280.0F / 720.0F, 0.1F, 600.0F);
            for (int view = 0; view < 15; ++view) {
                glm::vec3 focus = view == 0 ? glm::vec3{0, 12, 0}
                                            : glm::vec3{view == 1   ? -85.0F
                                                        : view == 2 ? 85.0F
                                                                    : 0.0F,
                                                        12,
                                                        view == 3   ? -85.0F
                                                        : view == 4 ? 85.0F
                                                                    : 0.0F};
                if (view == 6) {
                    const auto p = v2Session.resourceLayout().fields.front().center;
                    focus = {p.x, v2Session.terrain().heightAt(p.x, p.y), p.y};
                } else if (view == 7 && !v2Session.terrain().v2Landscape()->plan.ridges().empty()) {
                    const auto p = v2Session.terrain().v2Landscape()->plan.ridges().front().center;
                    focus = {p.x, v2Session.terrain().heightAt(p.x, p.y), p.y};
                } else if (view >= 8) {
                    const std::string family = view == 8                   ? "scrap_field"
                                               : (view == 9 || view >= 11) ? "oil_field"
                                                                           : "uranium_field";
                    for (const auto& field : v2Session.resourceLayout().fields)
                        if (field.definition.value == family &&
                            (view == 11 ? field.startingPlayer != 0 : field.startingPlayer == 0)) {
                            const auto p = field.center;
                            focus = {p.x, v2Session.terrain().heightAt(p.x, p.y), p.y};
                            break;
                        }
                }
                reviewCamera.target = focus;
                reviewCamera.position = focus + (view == 10   ? glm::vec3{-28, 38, -32}
                                                 : view == 13 ? glm::vec3{0, 12, 14}
                                                 : view == 14 ? glm::vec3{0, 12, -14}
                                                 : view == 12 ? glm::vec3{0, 45, 55}
                                                 : view == 5  ? glm::vec3{150, 205, 185}
                                                 : view >= 6  ? glm::vec3{0, 26, 32}
                                                              : glm::vec3{0, 80, 75});
                reviewCamera.view =
                    glm::lookAt(reviewCamera.position, reviewCamera.target, glm::vec3{0, 1, 0});
                for (int frame = 0; frame < 200 && !renderer.terrainUploadFinished(); ++frame) {
                    renderer.beginProfileFrame();
                    renderer.beginFrame(1280, 720);
                    renderer.prepareResourceSites(v2Session.world());
                    renderer.drawTerrain(reviewCamera);
                    renderer.endFrame();
                }
                renderer.beginProfileFrame();
                renderer.beginFrame(1280, 720);
                renderer.prepareResourceSites(v2Session.world());
                renderer.drawTerrain(reviewCamera);
                renderer.drawVegetation(v2Session.vegetation(), reviewCamera);
                renderer.drawWorld(v2Session.world(), reviewCamera);
                renderer.endFrame();
                valid = reportCheck(
                    captureTerrain(
                        ("central-water-play-" + std::to_string(view) + ".bmp").c_str()) &&
                        noGlErrors("V2 movement review") && valid,
                    __LINE__);
            }
            // Exercise continuous motion across shores, cliffs and active-map boundaries.
            for (int frame = 0; frame < 80; ++frame) {
                const float angle = glm::two_pi<float>() * frame / 80;
                reviewCamera.target = {85 * std::cos(angle), 12, 85 * std::sin(angle)};
                reviewCamera.position =
                    reviewCamera.target + glm::vec3{0, 65 + 25 * std::sin(angle), 75};
                reviewCamera.view =
                    glm::lookAt(reviewCamera.position, reviewCamera.target, glm::vec3{0, 1, 0});
                renderer.beginProfileFrame();
                renderer.beginFrame(1280, 720);
                renderer.prepareResourceSites(v2Session.world());
                renderer.drawTerrain(reviewCamera);
                renderer.drawVegetation(v2Session.vegetation(), reviewCamera);
                renderer.drawWorld(v2Session.world(), reviewCamera);
                renderer.endFrame();
            }
            valid = reportCheck(noGlErrors("V2 continuous camera motion") && valid, __LINE__);
            renderer.stageGeneratedTerrain(generatedTerrain, 12345, 10);
            for (int frame = 0; frame < 200 && !renderer.terrainUploadFinished(); ++frame) {
                renderer.beginProfileFrame();
                renderer.beginFrame(640, 360);
                renderer.drawTerrain(camera);
                renderer.endFrame();
            }
        }
        if (SDL_getenv("STRATEGY_RENDER_PROFILE")) {
            // Opt-in render-only probe: excludes simulation, UI and swap throttling. Avoid a
            // machine-dependent FPS assertion in CTest; report CPU+GPU completion time instead.
            const strategy::DefinitionRegistry definitions;
            strategy::GameSession scene{definitions,
                                        7,
                                        "spain",
                                        "japan",
                                        "unassigned",
                                        "unassigned",
                                        20,
                                        1,
                                        1,
                                        strategy::TerrainLayoutId{"central_water"},
                                        nullptr,
                                        strategy::TerrainMode::v2};
            renderer.stageGeneratedTerrain(scene.terrain(), 7, 20, scene.world().foundations());
            SDL_SetWindowSize(window, 2554, 1389);
            SDL_PumpEvents();
            auto probeCamera = camera;
            probeCamera.target = {0, 12, 0};
            probeCamera.position = {0, 92, 75};
            probeCamera.view =
                glm::lookAt(probeCamera.position, probeCamera.target, glm::vec3{0, 1, 0});
            probeCamera.projection =
                glm::perspective(glm::radians(55.0F), 2554.0F / 1389, 0.1F, 1000.0F);
            for (int frame = 0; frame < 1000 && !renderer.terrainUploadFinished(); ++frame) {
                renderer.beginProfileFrame();
                renderer.beginFrame(2554, 1389);
                renderer.prepareResourceSites(scene.world());
                renderer.drawTerrain(probeCamera);
                renderer.endFrame();
            }
            double totalMilliseconds = 0;
            double prepareMilliseconds = 0;
            std::array<double, 3> gpuMilliseconds{};
            std::array<GLuint, 3> queries{};
            glGenQueries(3, queries.data());
            std::cout << "V2_RENDER_DEVICE " << glGetString(GL_RENDERER) << std::endl;
            std::size_t rebuilt = 0, uploaded = 0, siteUploads = 0, nodeUploads = 0;
            for (int frame = 0; frame < 120; ++frame) {
                glFinish();
                const auto start = std::chrono::steady_clock::now();
                renderer.beginProfileFrame();
                renderer.beginFrame(2554, 1389);
                const auto prepareStart = std::chrono::steady_clock::now();
                renderer.prepareResourceSites(scene.world());
                const double prepareMs = std::chrono::duration<double, std::milli>(
                                             std::chrono::steady_clock::now() - prepareStart)
                                             .count();
                glBeginQuery(GL_TIME_ELAPSED, queries[0]);
                renderer.drawTerrain(probeCamera);
                glEndQuery(GL_TIME_ELAPSED);
                glBeginQuery(GL_TIME_ELAPSED, queries[1]);
                renderer.drawVegetation(scene.vegetation(), probeCamera);
                glEndQuery(GL_TIME_ELAPSED);
                glBeginQuery(GL_TIME_ELAPSED, queries[2]);
                renderer.drawWorld(scene.world(), probeCamera);
                glEndQuery(GL_TIME_ELAPSED);
                renderer.endFrame();
                glFinish();
                if (frame >= 20) {
                    prepareMilliseconds += prepareMs;
                    for (std::size_t pass = 0; pass < queries.size(); ++pass) {
                        GLuint64 ns = 0;
                        glGetQueryObjectui64v(queries[pass], GL_QUERY_RESULT, &ns);
                        gpuMilliseconds[pass] += static_cast<double>(ns) / 1000000;
                    }
                    totalMilliseconds += std::chrono::duration<double, std::milli>(
                                             std::chrono::steady_clock::now() - start)
                                             .count();
                    rebuilt += renderer.vegetationCacheStats().rebuiltChunks;
                    uploaded += renderer.vegetationCacheStats().uploadedBatches;
                    siteUploads += renderer.resourceSiteUploadedTiles();
                    nodeUploads += renderer.resourceNodeCacheStats().uploadedBatches;
                }
            }
            std::cout << "V2_RENDER_PROFILE chunks=20 pixels=2554x1389 frames=100 mean_ms="
                      << totalMilliseconds / 100 << " repair=" << scene.terrain().repairLevel()
                      << " resource_targets=" << scene.resourceLayout().nodes.size()
                      << " rebuilt_chunks=" << rebuilt << " uploaded_batches=" << uploaded
                      << " site_tile_uploads=" << siteUploads
                      << " node_batch_uploads=" << nodeUploads << std::endl;
            std::cout << "V2_RENDER_PASSES prepare_cpu_ms=" << prepareMilliseconds / 100
                      << " terrain_gpu_ms=" << gpuMilliseconds[0] / 100
                      << " vegetation_gpu_ms=" << gpuMilliseconds[1] / 100
                      << " world_gpu_ms=" << gpuMilliseconds[2] / 100 << std::endl;
            glDeleteQueries(3, queries.data());
            valid = reportCheck(renderer.terrainUploadFinished() && rebuilt == 0 && uploaded == 0 &&
                                    siteUploads == 0 && nodeUploads == 0 &&
                                    noGlErrors("V2 render profile") && valid,
                                __LINE__);
            renderer.stageGeneratedTerrain(generatedTerrain, 12345, 10);
            for (int frame = 0; frame < 200 && !renderer.terrainUploadFinished(); ++frame) {
                renderer.beginProfileFrame();
                renderer.beginFrame(640, 360);
                renderer.drawTerrain(camera);
                renderer.endFrame();
            }
        }
        const strategy::TextureHandle texture = renderer.requestTexture("ui/placeholder");
        renderer.bindTexture(texture, 0);
        // Asset groups can grow without requiring this renderer behavior test to duplicate the
        // manifest's exact inventory. Verify that every declared item completed and that all
        // currently required presentation models were uploaded.
        valid =
            reportCheck(loadProgress.total >= 44 && loadProgress.completed == loadProgress.total &&
                            loadProgress.failed == 0 && renderer.loadedModelCount() >= 29 &&
                            renderer.terrainUploadFinished() &&
                            renderer.terrainUploadProgress() == 1.0F && valid,
                        __LINE__);
        valid = reportCheck(renderer.textureState(texture) == strategy::ResourceState::ready &&
                                noGlErrors("standalone texture") && valid,
                            __LINE__);
        renderer.beginProfileFrame();
        renderer.beginFrame(640, 360);
        renderer.drawTerrain(camera, nullptr, true);
        renderer.drawVegetation(vegetation, camera);
        renderer.drawWorld(world, camera);
        renderer.drawEntityOutline(world, workerId, camera);
        renderer.drawPowerRanges(world, camera, 1);
        strategy::Entity placementPreview;
        placementPreview.transform.position = {8.0F, 0.0F, 0.0F};
        placementPreview.power.emplace();
        placementPreview.power.demand = 4.0F;
        placementPreview.power.connectionRange = 24.0F;
        placementPreview.power.maximumConnections = 2;
        placementPreview.authority.owner = 1;
        if (const auto* endpoint =
                strategy::findPowerPlacementTarget(world, 1, placementPreview, false))
            renderer.drawPowerPlacementConnection(camera, placementPreview, *endpoint, *endpoint);
        placementPreview.transform.position = {1000.0F, 0.0F, 1000.0F};
        placementPreview.authority.owner = 1;
        if (const auto* endpoint =
                strategy::findPowerPlacementTarget(world, 1, placementPreview, false))
            renderer.drawPowerPlacementConnection(camera, placementPreview, *endpoint, *endpoint);
        renderer.drawPowerConnections(world, camera, 1, true);
        renderer.drawTerrainDebugHud({0.0F, 0.0F, 0.0F}, {});
        renderer.drawPowerDebugHud(world, 1, {0.0F, 0.0F, 0.0F});
        renderer.endFrame();
        glFinish();
        valid = reportCheck(noGlErrors("complete render") && valid, __LINE__);
        renderer.beginFrame(1280, 720);
        renderer.drawLoadingScreen(0.42F, "GENERATING TERRAIN FIELDS");
        valid = reportCheck(
            checkProgress(renderer, "loading-fill", Rml::Colourb(84, 215, 242)) && valid, __LINE__);
        renderer.endFrame();
        glFinish();
        valid = reportCheck(noGlErrors("RmlUi loading screen") && valid, __LINE__);
        renderer.beginFrame(1280, 720);
        renderer.drawLoadingScreen(1.0F, "CLICK TO START");
        renderer.endFrame();
        glFinish();
        valid = reportCheck(noGlErrors("RmlUi completed loading screen") && valid, __LINE__);
        if (const char* loadingScreenshotPath = SDL_getenv("STRATEGY_LOADING_SCREENSHOT")) {
            std::vector<unsigned char> pixels(1280 * 720 * 4), flipped(pixels.size());
            glReadPixels(0, 0, 1280, 720, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            for (int row = 0; row < 720; ++row)
                std::copy_n(pixels.data() + row * 1280 * 4,
                            1280 * 4,
                            flipped.data() + (719 - row) * 1280 * 4);
            SDL_Surface* surface = SDL_CreateSurfaceFrom(
                1280, 720, SDL_PIXELFORMAT_RGBA32, flipped.data(), 1280 * 4);
            valid = reportCheck(surface && SDL_SaveBMP(surface, loadingScreenshotPath) && valid,
                                __LINE__);
            SDL_DestroySurface(surface);
        }
        renderer.finishLoadingScreen();
        const strategy::RmlUiScreenHandle pauseScreen =
            renderer.pushUiScreen({"assets/ui/pause_menu.rml",
                                   {},
                                   {"pause.resume", "pause.settings", "pause.exit"},
                                   {"pause.resume", "pause.settings", "pause.exit"},
                                   1.0F});
        renderer.beginFrame(1280, 720);
        renderer.drawRmlUi();
        renderer.endFrame();
        glFinish();
        valid = reportCheck(noGlErrors("RmlUi pause menu") && valid, __LINE__);
        renderer.removeUiScreen(pauseScreen);
        const strategy::RmlUiScreenHandle settingsScreen =
            renderer.pushUiScreen({"assets/ui/settings.rml",
                                   {},
                                   {"settings.resolution", "settings.apply"},
                                   {"settings.resolution.left", "settings.apply"},
                                   1.0F});
        renderer.setUiText("resolution-label", "RESOLUTION");
        renderer.setUiProperty("page-video", "display", "block");
        renderer.setUiProperty("page-keybinds", "display", "none");
        renderer.setUiText("settings.resolution.value", "1280 x 720");
        renderer.setUiText("settings-controls", "CONTROLS");
        renderer.setUiText("bind.forward.label", "MOVE FORWARD");
        renderer.setUiText("bind.forward.value", "W");
        renderer.setUiText("apply-label", "APPLY AND BACK");
        renderer.beginFrame(1280, 720);
        renderer.drawRmlUi();
        renderer.endFrame();
        glFinish();
        valid = reportCheck(noGlErrors("RmlUi settings menu") && valid, __LINE__);
        valid = reportCheck(
            checkFormPanel(renderer, "settings.resolution", "settings.apply") && valid, __LINE__);
        valid = reportCheck(captureForm(renderer, "settings.bmp") && valid, __LINE__);
        renderer.removeUiScreen(settingsScreen);
        const auto matchScreen = renderer.pushUiScreen(
            {"assets/ui/match_setup.rml",
             {{"match-title", "MATCH SETUP"},
              {"player-label", "PLAYER"},
              {"match.player_country.value", "BRAZIL"},
              {"start-label", "START MATCH"},
              {"generation-label", "TERRAIN GENERATION"},
              {"match.terrain_generation.value", "V2 - REGIONAL TERRAIN"},
              {"generation-description",
               "Stepped natural terrain, broad ramps, supported foundations and layout-controlled "
               "water. Archipelago requires future transport support."}},
             {"match.player_country", "match.start"},
             {"match.player_country.left", "match.player_country.right", "match.start"},
             1.0F});
        renderer.beginFrame(1280, 720);
        renderer.drawRmlUi();
        renderer.endFrame();
        glFinish();
        valid = reportCheck(noGlErrors("RmlUi match setup") && valid, __LINE__);
        valid = reportCheck(
            checkFormPanel(renderer, "match.player_country", "match.start") && valid, __LINE__);
        for (const auto& [id, value] : std::vector<std::pair<std::string, std::string>>{
                 {"match-mode", "1 VS 1 SKIRMISH"},
                 {"players-header", "PLAYERS"},
                 {"world-header", "WORLD"},
                 {"economy-header", "ECONOMY"},
                 {"options-header", "MATCH OPTIONS"},
                 {"opponent-label", "OPPONENT COUNTRY"},
                 {"layout-label", "MAP LAYOUT"},
                 {"size-label", "MAP SIZE"},
                 {"resources-label", "STARTING RESOURCES"},
                 {"abundance-label", "RESOURCE ABUNDANCE"},
                 {"seed-label", "MAP SEED"},
                 {"random-label", "RANDOM SEED"},
                 {"match.opponent_country.value", "JAPAN"},
                 {"match.terrain_layout.value", "ROLLING HILLS"},
                 {"match.map_size.value", "MEDIUM"},
                 {"match.starting_resources.value", "NORMAL"},
                 {"match.abundance.value", "NORMAL"},
                 {"match.seed.value", "123"},
                 {"summary-header", "MATCH SUMMARY"},
                 {"summary-player", "YOU: BRAZIL"},
                 {"summary-opponent", "OPPONENT: JAPAN"},
                 {"summary-map", "MAP: MEDIUM"},
                 {"summary-resources", "START: NORMAL"},
                 {"summary-deposits", "DEPOSITS: NORMAL"},
                 {"preview-caption", "TACTICAL OVERVIEW"},
                 {"back-label", "BACK"}})
            renderer.setUiText(id, value);
        renderer.setUiAttribute("player-emblem", "src", "../textures/ui/country_emblem_brazil.png");
        renderer.setUiAttribute(
            "opponent-emblem", "src", "../textures/ui/country_emblem_japan.png");
        valid = reportCheck(captureForm(renderer, "match-setup.bmp") && valid, __LINE__);
        renderer.setUiScale(1.5F);
        renderer.setUiText("match-mode", strategy::Text::get("match_setup.generation_failed"));
        renderer.setUiProperty("match-mode", "color", "#ffbf75");
        valid =
            reportCheck(captureForm(renderer, "match-generation-failed.bmp") && valid, __LINE__);
        renderer.setUiScale(1.0F);
        renderer.removeUiScreen(matchScreen);
        const strategy::RmlUiScreenHandle buildScreen =
            renderer.pushUiScreen({"assets/ui/build_menu.rml",
                                   {},
                                   {"build.palette.command_hub"},
                                   {"build.palette.command_hub"},
                                   1.0F});
        renderer.setUiText("build-palette",
                           "<button id=\"build.palette.command_hub\" class=\"build-slot selected\">"
                           "<img src=\"../icons/source/building_command_hub.png\"/>"
                           "<span>COMMAND HUB</span></button>");
        renderer.focusUi(0);
        renderer.beginFrame(1280, 720);
        renderer.drawRmlUi();
        renderer.endFrame();
        glFinish();
        valid = reportCheck(noGlErrors("RmlUi build menu") && valid, __LINE__);
        renderer.removeUiScreen(buildScreen);
        const strategy::RmlUiScreenHandle gameplayScreen =
            renderer.pushUiScreen({"assets/ui/gameplay_hud.rml",
                                   {},
                                   {"resources.power", "hud.satellite", "hud.menu"},
                                   {"resources.power", "hud.satellite", "hud.menu"},
                                   1.0F});
        renderer.setUiText("resources-content",
                           "<div class='resource'><img "
                           "src='../icons/source/resource_scrap.png'/><span>125</span></div>");
        renderer.setUiText(
            "entity-content",
            "<div id='test.info' class='entity-info'><div class='entity-title'>CONSTRUCTION "
            "DRONE</div>"
            "<div class='entity-main'><img class='portrait' "
            "src='../icons/source/building_alloy_processor.png'/><div class='bars-stats'>"
            "<div class='hud-bar progress-track'><progress id='test.health' class='progress-fill "
            "health' value='0.72' max='1'/>"
            "<span class='progress-label'>HEALTH 72 / 100</span></div>"
            "<div class='hud-bar progress-track'><progress id='test.power' class='progress-fill "
            "power' value='0.35' max='1'/>"
            "<span class='progress-label'>ENERGY 35 / 100</span></div>"
            "<div class='hud-bar progress-track'><progress id='test.construction' "
            "class='progress-fill construction' value='0.5' max='1'/>"
            "<span class='progress-label'>BUILD 50 / 100</span></div></div></div>"
            "<div class='stats-list'><div class='stat'><span class='stat-label'>CARGO</span>"
            "<span id='test.stat' class='stat-value'>100 / 100</span></div></div></div>"
            "<div class='entity-actions'><button id='action.preview' class='action'>"
            "<img src='../icons/source/building_alloy_processor.png'/></button></div>");
        auto* hudContext = Rml::GetContext("strategy_ui");
        auto* hudDocument = hudContext->GetDocument(hudContext->GetNumDocuments() - 1);
        auto* actionPanel = hudDocument->QuerySelector(".entity-actions");
        std::string palette = "<div class='action-heading'>CONSTRUCTION</div>";
        for (int i = 0; i < 11; ++i) {
            if (i % 4 == 0) palette += "<div class='action-row'>";
            palette += "<button id='" + std::string(i == 0 ? "action.preview" : "test.slot." + std::to_string(i)) +
                       "' class='action'><img src='../icons/source/building_alloy_processor.png'/>"
                       "<span class='action-name'>ALLOY PROCESSOR</span><span class='action-hotkey'>Alt+" +
                       std::string(1, "1234567890-="[i]) + "</span>"
                       "<div class='tooltip'><b>ALLOY PROCESSOR</b><br/>Converts scrap into alloy."
                       "<br/>COST: 150 SCRAP<br/>Requires power.</div></button>";
            if (i % 4 == 3 || i == 10) palette += "</div>";
        }
        actionPanel->SetInnerRML(palette);
        renderer.setUiControls({"resources.power", "action.preview"},
                               {"resources.power", "action.preview"});
        renderer.beginFrame(1280, 720);
        renderer.drawRmlUi();
        renderer.endFrame();
        glFinish();
        valid = reportCheck(noGlErrors("RmlUi gameplay HUD") && valid, __LINE__);
        const auto infoBounds = renderer.uiBounds("test.info");
        const auto statBounds = renderer.uiBounds("test.stat");
        const auto lastSlot = renderer.uiBounds("test.slot.10");
        if (!infoBounds || !statBounds || !lastSlot || statBounds->right > infoBounds->right + 1 ||
            lastSlot->right >= infoBounds->left || lastSlot->bottom > 720) {
            std::cerr << "HUD panels overlap or clip their content\n";
            valid = reportCheck(false, __LINE__);
        }

        valid = reportCheck(
            checkProgress(renderer, "test.health", Rml::Colourb(239, 100, 92)) && valid, __LINE__);
        valid = reportCheck(
            checkProgress(renderer, "test.power", Rml::Colourb(84, 215, 242)) && valid, __LINE__);
        valid = reportCheck(
            checkProgress(renderer, "test.construction", Rml::Colourb(239, 191, 86)) && valid,
            __LINE__);
        valid = reportCheck(captureForm(renderer, "progress-bars.bmp") && valid, __LINE__);
        const auto unscaledActionBounds = renderer.uiBounds("action.preview");
        hudDocument->GetElementById("action.preview")->SetPseudoClass("hover", true);
        hudDocument->GetElementById("action.preview")->QuerySelector(".tooltip")->SetProperty("animation", "none");
        valid = reportCheck(captureForm(renderer, "hud-tooltip.bmp") && valid, __LINE__);
        hudDocument->GetElementById("action.preview")->SetPseudoClass("hover", false);
        renderer.setUiScale(1.5F);
        renderer.beginFrame(1280, 720);
        renderer.drawRmlUi();
        renderer.endFrame();
        glFinish();
        const auto scaledActionBounds = renderer.uiBounds("action.preview");
        const std::string scrollMarkup =
            "<div id='test.scroll' style='height:60px; overflow:auto;'>"
            "<div style='height:500px;'>SCROLL RETENTION</div></div>";
        renderer.setUiText("entity-queue", scrollMarkup);
        hudContext->Update();
        auto* scrollPanel = hudDocument->GetElementById("test.scroll");
        scrollPanel->SetScrollTop(90);
        renderer.setUiText("entity-queue", scrollMarkup);
        hudContext->Update();
        if (hudDocument->GetElementById("test.scroll") != scrollPanel || scrollPanel->GetScrollTop() < 89) {
            std::cerr << "Identical HUD updates reset the scrolling subtree\n";
            valid = reportCheck(false, __LINE__);
        }
        // The cache must detect changes made through another UI API.
        renderer.resetUiScroll("test.scroll");
        if (scrollPanel->GetScrollTop() != 0) {
            std::cerr << "Explicit selection scroll reset failed\n";
            valid = reportCheck(false, __LINE__);
        }
        scrollPanel->SetInnerRML("ALTERED");
        renderer.setUiText("entity-queue", scrollMarkup);
        hudContext->Update();
        scrollPanel = hudDocument->GetElementById("test.scroll");
        if (scrollPanel->GetInnerRML().find("SCROLL RETENTION") == std::string::npos) {
            std::cerr << "Cached markup failed to restore externally changed content\n";
            valid = reportCheck(false, __LINE__);
        }
        const auto scrollBounds = renderer.uiBounds("test.scroll");
        SDL_Event wheel{};
        wheel.type = SDL_EVENT_MOUSE_WHEEL;
        wheel.wheel.mouse_x = (scrollBounds->left + scrollBounds->right) * 0.5F;
        wheel.wheel.mouse_y = (scrollBounds->top + scrollBounds->bottom) * 0.5F;
        wheel.wheel.y = -3;
        renderer.handleUiEvent(wheel);
        for (int i = 0; i < 12; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            hudContext->Update();
        }
        if (!renderer.pointerOverUi() || scrollPanel->GetScrollTop() < 1) {
            std::cerr << "Wheel event did not target and scroll its UI panel\n";
            valid = reportCheck(false, __LINE__);
        }
        renderer.setUiText("entity-queue", "");
        valid = reportCheck(captureForm(renderer, "hud-large.bmp") && valid, __LINE__);
        renderer.setUiAttribute("entity-shell", "class", "info-only");
        hudContext->Update();
        const auto compactInfo = renderer.uiBounds("test.info");
        const auto compactShell = renderer.uiBounds("entity-shell");
        if (!compactInfo || !compactShell || compactInfo->right > compactShell->right ||
            compactInfo->left < compactShell->left ||
            actionPanel->GetProperty<Rml::Style::Display>("display") != Rml::Style::Display::None) {
            std::cerr << "Information-only HUD failed to compact its layout\n";
            valid = reportCheck(false, __LINE__);
        }
        valid = reportCheck(captureForm(renderer, "hud-info-only.bmp") && valid, __LINE__);
        renderer.setUiAttribute("entity-shell", "class", "");
        if (!unscaledActionBounds || !scaledActionBounds ||
            scaledActionBounds->right - scaledActionBounds->left <=
                (unscaledActionBounds->right - unscaledActionBounds->left) * 1.25F) {
            std::cerr << "RmlUi global scale did not resize the active HUD\n";
            valid = reportCheck(false, __LINE__);
        }
        renderer.setUiScale(1.0F);
        renderer.beginFrame(1280, 720);
        renderer.drawRmlUi();
        renderer.endFrame();
        glFinish();
        if (const auto bounds = renderer.uiBounds("action.preview")) {
            SDL_Event down{};
            down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            down.button.button = SDL_BUTTON_LEFT;
            down.button.x = (bounds->left + bounds->right) * 0.5F;
            down.button.y = (bounds->top + bounds->bottom) * 0.5F;
            renderer.handleUiEvent(down);
            const auto clicked = renderer.takeUiAction();
            SDL_Event up = down;
            up.type = SDL_EVENT_MOUSE_BUTTON_UP;
            renderer.handleUiEvent(up);
            if (!clicked || *clicked != "action.preview") {
                std::cerr << "RmlUi gameplay action did not receive a pointer click at "
                          << down.button.x << ", " << down.button.y << "\n";
                valid = reportCheck(false, __LINE__);
            }
        } else {
            std::cerr << "RmlUi gameplay action has no bounds\n";
            valid = reportCheck(false, __LINE__);
        }
        renderer.removeUiScreen(gameplayScreen);
        valid = reportCheck(checkPlacementControls(renderer, logger) && valid, __LINE__);
        valid = reportCheck(checkRememberedCulling(renderer) && valid, __LINE__);
        valid = reportCheck(checkPlacementControls(renderer, logger, true) && valid, __LINE__);
        valid = reportCheck(checkMatchPreparation(renderer, logger) && valid, __LINE__);
        if (const char* screenshotPath = SDL_getenv("STRATEGY_UI_SCREENSHOT")) {
            std::vector<unsigned char> pixels(1280 * 720 * 4), flipped(pixels.size());
            glReadPixels(0, 0, 1280, 720, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            for (int row = 0; row < 720; ++row)
                std::copy_n(pixels.data() + row * 1280 * 4,
                            1280 * 4,
                            flipped.data() + (719 - row) * 1280 * 4);
            SDL_Surface* surface =
                SDL_CreateSurfaceFrom(1280, 720, SDL_PIXELFORMAT_RGBA32, flipped.data(), 1280 * 4);
            valid = reportCheck(surface && SDL_SaveBMP(surface, screenshotPath) && valid, __LINE__);
            SDL_DestroySurface(surface);
        }
    }
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return valid ? 0 : 1;
}

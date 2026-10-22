#include "persistence/SaveGame.hpp"
#include "simulation/GameSession.hpp"
#include "world/GenerationProgress.hpp"
#include "world/Navigation.hpp"
#include "world/StartingPlacement.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int strategyTestMain() {
    using namespace strategy;
    const DefinitionRegistry definitions;
    std::vector<const char*> layouts;
#if defined(_WIN32)
    char* allocatedSelection = nullptr;
    std::size_t selectionLength = 0;
    require(_dupenv_s(&allocatedSelection, &selectionLength, "STRATEGY_V2_AUDIT_LAYOUT") == 0,
            "Unable to read V2 audit selection");
    const std::unique_ptr<char, decltype(&std::free)> selectionStorage{allocatedSelection,
                                                                       &std::free};
    const char* selected = selectionStorage.get();
#else
    const char* selected = std::getenv("STRATEGY_V2_AUDIT_LAYOUT");
#endif
    for (const char* id :
         {"open_plains", "central_water", "rolling_hills", "central_hill", "continental"})
        if (!selected || std::string_view{selected} == id)
            layouts.push_back(id);
    require(!layouts.empty(), "Unknown V2 audit layout");
    for (const char* layout : layouts) {
        for (const auto chunks : {10U, 15U, 20U})
            for (const float abundance : {0.5F, 2.0F}) {
                WorldGenerationProgress matrixProgress;
                GameSession matrix{definitions,
                                   12345,
                                   "spain",
                                   "japan",
                                   "unassigned",
                                   "unassigned",
                                   chunks,
                                   1,
                                   abundance,
                                   TerrainLayoutId{layout},
                                   &matrixProgress,
                                   TerrainMode::v2};
                std::cout << layout << " chunks=" << chunks << " abundance=" << abundance
                          << " repair=" << matrix.terrain().repairLevel() << std::endl;
                require(matrix.terrainSeed() == 12345, "Matrix preserves original seed");
            }
        WorldGenerationProgress progress;
        GameSession session{definitions,
                            7,
                            "spain",
                            "japan",
                            "unassigned",
                            "unassigned",
                            10,
                            1,
                            1,
                            TerrainLayoutId{layout},
                            &progress,
                            TerrainMode::v2};
        std::cout << layout << " repair=" << session.terrain().repairLevel()
                  << " fields=" << session.resourceLayout().fields.size() << std::endl;
        require(session.terrainSeed() == 7, "V2 preserves the originally requested seed");
        for (const auto& node : session.resourceLayout().nodes) {
            const auto* field = definitions.resourceField(node.field);
            const auto* type = definitions.resource(node.archetype);
            require(
                definitions.resourceFieldForNode(node.archetype) == field &&
                    field->maximumRadiusForNode(node.archetype) == field->terrainV2MaximumRadius,
                "Compound deposits retain field lookup and local depletion retargeting after load");
            require(std::find(field->terrainV2HarvestNodes.begin(),
                              field->terrainV2HarvestNodes.end(),
                              node.archetype) != field->terrainV2HarvestNodes.end() &&
                        type->collisionRadius <= 1.65F,
                    "V2 fields contain individual harvest targets rather than compound fields");
        }
        for (std::size_t index = 0; index < session.resourceLayout().fields.size(); ++index) {
            const auto& field = session.resourceLayout().fields[index];
            float amount = 0;
            std::uint32_t count = 0;
            for (const auto& entity : session.world().entities())
                if (entity.resource && entity.resource.fieldId == index + 1) {
                    amount += entity.resource.remaining;
                    ++count;
                }
            require(amount == field.capacity && count == field.desiredNodes && count >= 8,
                    "Many independent nodes preserve each field's exact budget and identity");
        }
        for (std::uint32_t player : {1U, 2U}) {
            float scrap = 0, oil = 0;
            for (const auto& field : session.resourceLayout().fields)
                if (field.startingPlayer == player) {
                    const auto* definition = definitions.resourceField(field.definition);
                    if (definition->resourceType.value == "scrap")
                        scrap += field.capacity;
                    if (definition->resourceType.value == "oil")
                        oil += field.capacity;
                }
            require(scrap == 600 && oil == 400,
                    "Each opening keeps its fixed scrap/oil allocation");
        }
        Navigation navigation{session.terrain(), definitions, 10};
        World empty;
        const auto a = session.startingAnchors()[0], b = session.startingAnchors()[1];
        require(!navigation.findPath(empty, {a.x, 0, a.y}, {b.x, 0, b.y}, 0.5F, 0).empty(),
                "Ground workers can travel between player regions without crossing water");
        const glm::vec3 approachA{a.x + 18, 0, a.y + 18};
        const glm::vec3 approachB{b.x - 18, 0, b.y - 18};
        require(!navigation.findPath(session.world(), approachA, approachB, 0.65F, 0).empty(),
                "Final buildings and resource nodes keep a ground route between openings");
        for (const auto& entity : session.world().entities())
            if (entity.kind == EntityKind::building) {
                const auto* definition = definitions.archetype(entity.archetype);
                auto footprint = *definition->footprint;
                footprint.rotationDegrees = entity.transform.rotationDegrees.y;
                require(session.terrain()
                            .evaluatePlacement(entity.transform.position.x,
                                               entity.transform.position.z,
                                               footprint,
                                               definition->placement)
                            .valid(),
                        "Joint starting buildings fit their opening");
            }
        const auto savePath = std::filesystem::temp_directory_path() /
                              (std::string{"strategy_v2_session_"} + layout + ".json");
        SaveGame::write(savePath,
                        session.terrainSeed(),
                        session.world(),
                        &session.players(),
                        10,
                        layout,
                        TerrainMode::v2,
                        session.terrain().repairLevel());
        const auto save = SaveGame::read(savePath);
        for (const auto& entity : session.world().entities())
            if (entity.resource) {
                const auto restoredNode =
                    std::find_if(save.entities.begin(),
                                 save.entities.end(),
                                 [&](const auto& candidate) { return candidate.id == entity.id; });
                require(restoredNode != save.entities.end() &&
                            restoredNode->resource.fieldId == entity.resource.fieldId &&
                            restoredNode->resource.remaining == entity.resource.remaining,
                        "Field membership and individual remaining supply survive save/load");
            }
        require(save.terrainMode == TerrainMode::v2 &&
                    save.terrainRepairLevel == session.terrain().repairLevel(),
                "Save metadata restores the new V2 and its deterministic repair");
        Terrain restored{save.terrainSeed,
                         TerrainLayoutId{save.terrainLayout},
                         save.terrainMode,
                         save.mapChunksPerSide};
        if (save.terrainRepairLevel)
            restored.repairV2Resources(
                save.terrainSeed, TerrainLayoutId{save.terrainLayout}, save.terrainRepairLevel);
        for (float z = -110; z <= 110; z += 13)
            for (float x = -110; x <= 110; x += 13)
                require(restored.heightAt(x, z) == session.terrain().heightAt(x, z) &&
                            restored.waterCoverageAt(x, z) ==
                                session.terrain().waterCoverageAt(x, z),
                        "Save regenerates identical authoritative surfaces");
        std::filesystem::remove(savePath);
    }
    for (const char* id : layouts) {
        for (const auto chunks : {10U, 15U, 20U}) {
            Terrain fallback{7, TerrainLayoutId{id}, TerrainMode::v2, chunks};
            fallback.repairV2Resources(7, TerrainLayoutId{id}, 2);
            for (float abundance : {0.5F, 1.0F, 2.0F}) {
                World fallbackWorld;
                const auto& anchors = fallback.v2Landscape()->plan.openings();
                for (std::size_t player = 0; player < anchors.size(); ++player) {
                    const auto& starts = player == 0 ? definitions.matchRules().playerOne
                                                     : definitions.matchRules().playerTwo;
                    for (const auto& start : starts) {
                        auto& entity = fallbackWorld.createEntity(
                            "fallback fixture", start.archetype, static_cast<PlayerId>(player + 1));
                        definitions.initializeEntity(entity);
                        entity.transform.position = startingEntityPosition(start, anchors[player]);
                    }
                }
                const auto layout = populateResources(fallbackWorld,
                                                      fallback,
                                                      definitions,
                                                      7,
                                                      chunks,
                                                      abundance,
                                                      fallback.v2Landscape()->plan.openings());
                for (const auto& fieldId : definitions.matchRules().generatedResourceFields) {
                    const auto& settings =
                        definitions.resourceField(ResourceFieldId{fieldId})->generation;
                    const auto expected =
                        static_cast<std::uint32_t>(std::round(
                            chunks * chunks * settings.fieldsPerSquareChunk * abundance / 2)) *
                        2;
                    std::uint32_t expansion = 0, starting = 0;
                    for (const auto& field : layout.fields)
                        if (field.definition.value == fieldId)
                            field.startingPlayer ? ++starting : ++expansion;
                    require(expansion == expected &&
                                starting == (settings.startingNodes.empty() ? 0U : 2U),
                            "Forced fallback preserves every opening and abundance field");
                }
                Navigation fallbackNavigation{fallback, definitions, chunks};
                const auto a = anchors[0], b = anchors[1];
                require(!fallbackNavigation
                             .findPath(fallbackWorld,
                                       {a.x + 18, 0, a.y + 18},
                                       {b.x - 18, 0, b.y - 18},
                                       0.65F,
                                       0)
                             .empty(),
                        "Constructive fallback remains navigable with its final entities");
                std::cout << "fallback " << id << " chunks=" << chunks << " abundance=" << abundance
                          << std::endl;
            }
        }
    }
    std::cout << "V2 session, resources, routes and restoration checks passed\n";
    return 0;
}

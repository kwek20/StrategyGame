#include "gameplay/DefinitionRegistry.hpp"
#include "simulation/GameSession.hpp"
#include "world/GenerationProgress.hpp"
#include "world/Collision.hpp"
#include "world/MapArea.hpp"
#include "world/Navigation.hpp"
#include "world/SpatialShape.hpp"
#include "world/StartingPlacement.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
std::string environment(const char* name) {
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t length = 0;
    _dupenv_s(&value, &length, name);
    const std::string result = value ? value : "";
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value ? value : "";
#endif
}

void inspectOpeningHeight(const strategy::DefinitionRegistry& definitions,
                          strategy::TerrainLayoutId layout, std::uint32_t seed,
                          std::uint32_t size, strategy::WorldGenerationProgress& progress) {
    const strategy::Terrain terrain{seed, layout};
    const auto starts = strategy::selectStartingRegions(terrain, definitions, size, 2, seed);
    const auto& settings = definitions.resourceField(strategy::ResourceFieldId{"scrap_field"})->generation;
    for (std::size_t player = 0; player < starts.size(); ++player) {
        float minimum = std::numeric_limits<float>::max();
        float maximum = -minimum;
        unsigned heightCandidates = 0;
        for (float z = -55; z <= 55; z += 1)
            for (float x = -55; x <= 55; x += 1) {
                const float distance = std::hypot(x, z);
                const auto point = starts[player].anchor + glm::vec2{x, z};
                if (distance < 16 || distance > 55 ||
                    !strategy::MapArea{size}.contains(point, definitions.matchRules().terrainEdgeMargin)) continue;
                const float height = terrain.heightAt(point.x, point.y) / strategy::Terrain::heightScale;
                minimum = std::min(minimum, height);
                maximum = std::max(maximum, height);
                if (height >= settings.minimumHeight && height <= settings.maximumHeight) ++heightCandidates;
            }
        progress.diagnostic("height survey player=" + std::to_string(player + 1) +
            " min=" + std::to_string(minimum) + " max=" + std::to_string(maximum) +
            " heightEligibleSamples=" + std::to_string(heightCandidates));
    }
}

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void verify(const strategy::GameSession& session, const strategy::DefinitionRegistry& definitions, float abundance = 1.0F) {
    const auto& world = session.world();
    const auto& terrain = session.terrain();
    const strategy::MapArea map{session.mapChunksPerSide()};
    const auto& starts = session.startingAnchors();
    require(starts.size() == 2, "Expected two starting anchors");
    require(glm::distance(starts[0], starts[1]) >=
        map.extent() * definitions.matchRules().minimumOpponentSeparationNormalized,
        "Insufficient opponent separation");
    strategy::Navigation navigation{terrain, definitions, session.mapChunksPerSide()};
    std::array<const strategy::Entity*, 2> workers{};
    std::array<const strategy::Entity*, 2> drones{};
    for (const auto& entity : world.entities()) {
        const auto position = entity.transform.position;
        require(std::isfinite(position.x) && std::isfinite(position.z), "Non-finite entity position");
        require(map.contains({position.x, position.z}), "Entity outside match boundary");
        if (entity.authority.owner == 0) continue;
        const auto* type = definitions.archetype(entity.archetype);
        require(type != nullptr, "Missing starting archetype");
        if (entity.unitControl) {
            require(terrain.movementCostAt(position.x, position.z, type->movement.domains) > 0,
                "Starting unit placed on forbidden terrain");
            if (entity.archetype.value == "worker") workers.at(entity.authority.owner - 1) = &entity;
        }
        if (entity.archetype.value == "construction_drone") drones.at(entity.authority.owner - 1) = &entity;
    }
    require(workers[0] && workers[1] && drones[0] && drones[1], "Starting units missing");
    const auto* workerType = definitions.archetype(workers[0]->archetype);
    const auto workerPath = navigation.findPath(terrain, world, workers[0]->transform.position, workers[1]->transform.position,
        workerType->collisionRadius, workers[0]->id,
        {workerType->movement.domains, workerType->movement.ignoresEntityObstacles});
    require(!workerPath.empty() && glm::distance(glm::vec2{workerPath.back().x, workerPath.back().z},
        glm::vec2{workers[1]->transform.position.x, workers[1]->transform.position.z}) <= 3.0F,
        "No navigable worker route between starts");
    const auto& fields = session.resourceLayout().fields;
    const auto& openingNodes = session.resourceLayout().nodes;
    for (std::size_t player = 0; player < starts.size(); ++player) {
        float scrap = 0, oil = 0;
        const auto* droneType = definitions.archetype(drones[player]->archetype);
        for (std::size_t index = 0; index < fields.size(); ++index) {
            const auto& field = fields[index];
            if (field.startingPlayer != player + 1) continue;
            float capacity = 0;
            for (const auto& node : openingNodes) {
                if (node.fieldIndex != index) continue;
                const auto* type = definitions.resource(node.archetype);
                capacity += type->resourceCapacity;
                const strategy::Entity* entity = nullptr;
                for (const auto& candidate : world.entities())
                    if (candidate.resource && glm::vec2{candidate.transform.position.x, candidate.transform.position.z} == node.position)
                        entity = &candidate;
                require(entity, "Generated opening node missing from world");
                const auto path = navigation.findPath(terrain, world, drones[player]->transform.position,
                    strategy::NavigationGoalRegion{strategy::spatialShape(definitions, *entity), 1.5F,
                        {drones[player]->transform.position.x, drones[player]->transform.position.z},
                        entity->id, drones[player]->id}, droneType->collisionRadius, drones[player]->id,
                        {droneType->movement.domains, droneType->movement.ignoresEntityObstacles});
                require(!path.empty(), "Opening node is not navigable");
                require(glm::distance(node.position, starts[player]) < glm::distance(node.position, starts[1 - player]),
                    "Opening node is closer to the opponent");
            }
            require(capacity == field.capacity, "Opening field capacity mismatch");
            if (field.definition.value == "scrap_field") scrap += capacity;
            if (field.definition.value == "oil_field") oil += capacity;
        }
        require(scrap == 600 && oil == 400, "Incorrect reserved starting supply");
    }
    for (const auto& id : definitions.matchRules().generatedResourceFields) {
        const auto& settings = definitions.resourceField(strategy::ResourceFieldId{id})->generation;
        std::array<float, 2> startingCosts{};
        for (const auto& field : fields)
            if (field.definition.value == id && field.startingPlayer)
                startingCosts.at(field.startingPlayer - 1) = field.accessCost;
        if (!settings.startingNodes.empty())
            require(std::abs(startingCosts[0] - startingCosts[1]) <= settings.accessCostTolerance ||
                std::min(startingCosts[0], startingCosts[1]) >= std::max(startingCosts[0], startingCosts[1]) * settings.minimumAccessRatio,
                "Unbalanced starting travel costs");
    }
    const auto& nodes = session.resourceLayout().nodes;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        const auto& field = fields[index];
        const auto& settings = definitions.resourceField(field.definition)->generation;
        float capacity = 0;
        std::size_t count = 0;
        for (const auto& node : nodes)
            if (node.fieldIndex == index) {
                capacity += definitions.resource(node.archetype)->resourceCapacity;
                ++count;
                require(glm::distance(node.position, field.center) <= field.radius, "Node outside field");
            }
        require(capacity == field.capacity && count == field.desiredNodes, "Partial or excessive field inventory");
        for (std::size_t otherIndex = index + 1; otherIndex < fields.size(); ++otherIndex) {
            const auto& other = fields[otherIndex];
            const auto& otherSettings = definitions.resourceField(other.definition)->generation;
            require(glm::distance(field.center, other.center) + 0.001F >= field.radius + other.radius +
                std::max(settings.minimumFieldSeparation, otherSettings.minimumFieldSeparation), "Fields overlap exclusion areas");
        }
    }
    for (const auto& id : definitions.matchRules().generatedResourceFields) {
        const auto& settings = definitions.resourceField(strategy::ResourceFieldId{id})->generation;
        const auto totalFields = std::max(2U, static_cast<std::uint32_t>(std::round(session.mapChunksPerSide() *
            session.mapChunksPerSide() * settings.fieldsPerSquareChunk * abundance / 2)) * 2);
        float startingCapacity = 0, expansionCapacity = 0;
        for (const auto& node : settings.startingNodes) startingCapacity += definitions.resource(node)->resourceCapacity;
        for (const auto& node : settings.expansionNodes) expansionCapacity += definitions.resource(node)->resourceCapacity;
        const auto startingFields = settings.startingNodes.empty() ? 0U : 2U;
        float actual = 0;
        for (const auto& node : nodes)
            if (node.field.value == id) actual += definitions.resource(node.archetype)->resourceCapacity;
        require(actual == startingFields * startingCapacity + (totalFields - startingFields) * expansionCapacity,
            "Resource budget changed during placement");
        for (const auto& node : nodes) {
            float local = 0;
            for (const auto& other : nodes)
                if (other.field.value == id && glm::distance(node.position, other.position) <= settings.localCapacityRadius)
                    local += definitions.resource(other.archetype)->resourceCapacity;
            require(local <= settings.maximumLocalCapacity, "Excessive local resource concentration");
        }
    }
    for (std::size_t a = 0; a < nodes.size(); ++a) {
        const auto* type = definitions.resource(nodes[a].archetype);
        require(type && !terrain.sampleAt(nodes[a].position.x, nodes[a].position.y).submerged,
                "Resource on submerged terrain");
        for (std::size_t b = a + 1; b < nodes.size(); ++b) {
            const auto* other = definitions.resource(nodes[b].archetype);
            require(other && glm::distance(nodes[a].position, nodes[b].position) + 0.0001F >=
                type->collisionRadius + other->collisionRadius, "Overlapping resource nodes");
        }
    }
}

void exportLayout(const strategy::GameSession& session, const std::filesystem::path& path) {
    std::ofstream fields(path.string() + "-fields.csv");
    fields << "family,x,z,radius,capacity,starting_player,travel\n";
    for (const auto& field : session.resourceLayout().fields)
        fields << field.definition.value << ',' << field.center.x << ',' << field.center.y << ',' << field.radius << ','
            << field.capacity << ',' << field.startingPlayer << ',' << field.accessCost << '\n';
    std::ofstream nodes(path.string() + "-nodes.csv");
    nodes << "family,x,z,archetype,field_index\n";
    for (const auto& node : session.resourceLayout().nodes)
        nodes << node.field.value << ',' << node.position.x << ',' << node.position.y << ',' << node.archetype.value << ','
            << node.fieldIndex << '\n';
    std::ofstream starts(path.string() + "-starts.csv");
    starts << "player,x,z\n";
    for (std::size_t player = 0; player < session.startingAnchors().size(); ++player)
        starts << player + 1 << ',' << session.startingAnchors()[player].x << ',' << session.startingAnchors()[player].y << '\n';
    constexpr int pixels = 240;
    std::ofstream image(path.string() + "-terrain.ppm", std::ios::binary);
    image << "P6\n" << pixels << ' ' << pixels << "\n255\n";
    const strategy::MapArea map{session.mapChunksPerSide()};
    for (int z = 0; z < pixels; ++z)
        for (int x = 0; x < pixels; ++x) {
            const auto point = map.gridCellCenter({x, z}, pixels);
            const auto& sample = session.terrain().sampleAt(point.x, point.y);
            const auto color = sample.submerged ? glm::vec3{0.08F, 0.22F, 0.4F} : sample.surfaceColor;
            for (int channel = 0; channel < 3; ++channel) {
                const char value = static_cast<char>(std::clamp(color[channel] * 255.0F, 0.0F, 255.0F));
                image.write(&value, 1);
            }
        }
}

int probeOpeningEconomy(const strategy::DefinitionRegistry& definitions, const std::filesystem::path& output) {
    std::ofstream report(output / "opening-economy.csv");
    report << "size,chain,seconds,player,starting_scrap_remaining,starting_oil_remaining,alloy,fuel\n";
    for (const auto size : {10U, 15U, 20U})
        for (const auto& family : {std::string{"scrap_field"}, std::string{"oil_field"}}) {
            strategy::GameSession session{definitions, 123U, "spain", "japan", "unassigned",
                "unassigned", size, 1.0F, 1.0F, strategy::TerrainLayoutId{"rolling_hills"}};
            std::array<strategy::EntityId, 2> drones{};
            for (const auto& entity : session.world().entities())
                if (entity.archetype.value == "construction_drone") drones.at(entity.authority.owner - 1) = entity.id;
            for (std::size_t player = 0; player < 2; ++player) {
                require(drones[player], "Economy probe requires a starting drone");
                strategy::EntityId resource = 0;
                for (const auto& node : session.resourceLayout().nodes) {
                    const auto& field = session.resourceLayout().fields[node.fieldIndex];
                    if (field.startingPlayer != player + 1 || node.field.value != family) continue;
                    for (const auto& entity : session.world().entities())
                        if (entity.resource && glm::vec2{entity.transform.position.x, entity.transform.position.z} == node.position)
                            resource = entity.id;
                    if (resource) break;
                }
                require(resource && session.submit({static_cast<strategy::PlayerId>(player + 1), 1,
                    strategy::GatherResourceCommand{drones[player], resource, 0}}), "Opening gather command rejected");
            }
            for (unsigned minute = 0; minute <= 10; ++minute) {
                if (minute) session.advanceTicks(60 * 30);
                for (std::size_t player = 0; player < 2; ++player) {
                    float scrap = 0, oil = 0;
                    for (const auto& node : session.resourceLayout().nodes) {
                        const auto& field = session.resourceLayout().fields[node.fieldIndex];
                        if (field.startingPlayer != player + 1) continue;
                        for (const auto& entity : session.world().entities()) {
                            if (!entity.resource || glm::vec2{entity.transform.position.x, entity.transform.position.z} != node.position) continue;
                            if (node.field.value == "scrap_field") scrap += entity.resource.remaining;
                            if (node.field.value == "oil_field") oil += entity.resource.remaining;
                        }
                    }
                    const auto* state = session.players().find(static_cast<strategy::PlayerId>(player + 1));
                    const float alloy = state->resources.at("alloy"), fuel = state->resources.at("fuel");
                    report << size << ',' << family << ',' << minute * 60 << ',' << player + 1 << ',' << scrap << ',' << oil << ',' << alloy << ',' << fuel << '\n' << std::flush;
                    if (minute == 10) {
                        require(family == "scrap_field" ? scrap < 600 && alloy > 150 : oil < 400 && fuel > 0,
                            "Opening gathering/delivery/processing made no progress");
                        std::cout << "opening-economy " << size << ' ' << family << " player=" << player + 1 << " scrap=" << scrap <<
                            " oil=" << oil << " alloy=" << alloy << " fuel=" << fuel << std::endl;
                    }
                }
            }
        }
    return 0;
}

int probeResourceRetries(const strategy::DefinitionRegistry& definitions,
                         const std::filesystem::path& output) {
    struct Case { std::uint32_t size; std::uint32_t seed; float abundance; };
    constexpr std::array cases{Case{10, 0x5EED1234U, 1.0F}, Case{15, 123U, 0.5F},
                              Case{20, 123U, 0.5F}, Case{20, 123123U, 2.0F}};
    std::ofstream report(output / "resource-retry-probe.csv");
    report << "size,requested_seed,candidate_seed,accepted_seed,terrain_generations,result\n";
    unsigned failures = 0;
    for (const auto& test : cases) {
        unsigned generations = 0;
        bool recovered = false;
        for (std::uint32_t attempt = 0; attempt < 8 && generations < 8; ++attempt) {
            const auto candidate = strategy::retryTerrainSeed(test.seed, attempt);
            strategy::WorldGenerationProgress progress;
            std::string result = "OK";
            std::string accepted;
            try {
                strategy::GameSession session{definitions, candidate, "spain", "japan", "unassigned",
                    "unassigned", test.size, 1.0F, test.abundance,
                    strategy::TerrainLayoutId{"archipelago"}, &progress};
                accepted = std::to_string(session.terrainSeed());
                verify(session, definitions, test.abundance);
                strategy::GameSession replay{definitions, session.terrainSeed(), "spain", "japan",
                    "unassigned", "unassigned", test.size, 1.0F, test.abundance,
                    strategy::TerrainLayoutId{"archipelago"}};
                require(session.stateChecksum() == replay.stateChecksum(), "Probe replay mismatch");
                recovered = true;
            } catch (const std::exception& error) {
                result = error.what();
            }
            // Count nested start-selection terrain retries as well; never call these free attempts.
            const auto& diagnostics = progress.diagnostics();
            for (std::size_t offset = 0; (offset = diagnostics.find("terrain attempt=", offset)) != std::string::npos;
                 offset += 16) ++generations;
            if (generations > 8) { recovered = false; result = "Probe exceeded eight terrain generations"; }
            const auto label = std::to_string(test.size) + "-" + std::to_string(test.seed) + "-" + std::to_string(attempt);
            std::ofstream(output / ("retry-probe-" + label + ".log")) << diagnostics;
            report << test.size << ',' << test.seed << ',' << candidate << ',' << accepted << ','
                   << generations << ',' << result << '\n' << std::flush;
            std::cout << "retry-probe-" << label << ": " << result << " (" << generations
                      << " terrain generations)" << std::endl;
            if (recovered) break;
        }
        if (!recovered) ++failures;
    }
    return failures ? 1 : 0;
}
}

int strategyTestMain() {
    const strategy::DefinitionRegistry definitions;
    const auto terrainDefinitions = strategy::TerrainGenerationDefinitions::load();
    const std::filesystem::path output = "build/map-generation-audit";
    std::filesystem::create_directories(output);
    const auto mode = environment("STRATEGY_MAP_AUDIT_MODE");
    if (mode == "opening-economy") return probeOpeningEconomy(definitions, output);
    if (mode == "retry-probe") return probeResourceRetries(definitions, output);
    require(mode.empty(), "Unknown map audit mode");
    const auto filter = environment("STRATEGY_MAP_AUDIT_LAYOUT");
    require(filter.empty() || std::any_of(terrainDefinitions.layouts().begin(), terrainDefinitions.layouts().end(),
        [&](const auto& layout) { return layout.id.value == filter; }), "Unknown audit layout filter");
    std::ofstream report(output / (filter.empty() ? "results.csv" : "results-" + filter + ".csv"));
    report << "layout,size,requested_seed,accepted_seed,abundance,seconds,result\n";
    unsigned failed = 0;
    for (const auto& layout : terrainDefinitions.layouts()) {
        if (!filter.empty() && layout.id.value != filter) continue;
        for (const auto size : {10U, 15U, 20U})
            for (const auto seed : {0x5EED1234U, 123U, 123123U}) {
                const float abundance = seed == 123U ? 0.5F : seed == 123123U ? 2.0F : 1.0F;
                const auto began = std::chrono::steady_clock::now();
                strategy::WorldGenerationProgress progress;
                const auto label = layout.id.value + "-" + std::to_string(size) + "-" + std::to_string(seed);
                std::string result = "OK";
                std::uint32_t accepted = seed;
                try {
                    strategy::GameSession session{definitions, seed, "spain", "japan", "unassigned",
                        "unassigned", size, 1.0F, abundance, layout.id, &progress};
                    accepted = session.terrainSeed();
                    verify(session, definitions, abundance);
                    exportLayout(session, output / label);
                    // Repeat a complete generated match per layout, including retries and resources.
                    if (size == 10 && seed == 0x5EED1234U) {
                        strategy::GameSession repeated{definitions, seed, "spain", "japan", "unassigned",
                            "unassigned", size, 1.0F, abundance, layout.id};
                        require(repeated.terrainSeed() == accepted &&
                                repeated.stateChecksum() == session.stateChecksum(),
                                "Non-deterministic generated match");
                    }
                    if (accepted != seed) {
                        strategy::GameSession replay{definitions, accepted, "spain", "japan", "unassigned",
                            "unassigned", size, 1.0F, abundance, layout.id};
                        require(replay.terrainSeed() == accepted &&
                                replay.stateChecksum() == session.stateChecksum(),
                                "Accepted seed does not reproduce the generated match");
                    }
                } catch (const std::exception& error) {
                    result = error.what();
                    ++failed;
                    if (result.find("Unable to provide starting") != std::string::npos) {
                        try { inspectOpeningHeight(definitions, layout.id, seed, size, progress); }
                        catch (const std::exception& surveyError) { progress.diagnostic(surveyError.what()); }
                    }
                }
                std::ofstream(output / (label + ".log")) << progress.diagnostics();
                const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
                report << layout.id.value << ',' << size << ',' << seed << ',' << accepted << ','
                       << abundance << ',' << seconds << ',' << result << '\n' << std::flush;
                std::cout << label << ": " << result << " (" << seconds << "s)" << std::endl;
            }
    }
    return failed ? 1 : 0;
}

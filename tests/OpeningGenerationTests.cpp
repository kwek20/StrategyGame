#include "gameplay/DefinitionRegistry.hpp"
#include "world/StartingPlacement.hpp"
#include "world/WorldGeneration.hpp"
#include "world/World.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
struct Fixture {
    std::filesystem::path path = std::filesystem::temp_directory_path() / "strategy-opening-fields.json";
    ~Fixture() { std::error_code error; std::filesystem::remove(path, error); }
};
}
int strategyTestMain() {
    const strategy::DefinitionRegistry definitions;
    const strategy::Terrain terrain{123U};
    strategy::WorldGenerationProgress diagnostics;
    const auto starts = strategy::selectStartingRegions(terrain, definitions, 10, 2, 123U, &diagnostics);
    require(diagnostics.diagnostics().find("start player=1") != std::string::npos &&
            diagnostics.diagnostics().find("accepted") != std::string::npos, "Missing start diagnostics");
    const auto again = strategy::selectStartingRegions(terrain, definitions, 10, 2, 123U);
    const auto solo = strategy::selectStartingRegions(terrain, definitions, 10, 1, 123U);
    require(starts[0].anchor == solo[0].anchor, "Player B moved accepted player A");
    require(starts[0].selectionAttempt == solo[0].selectionAttempt, "Player B changed player A's retry sequence");
    bool exercisedRetry = false;
    for (std::size_t i = 0; i < starts.size(); ++i) {
        require(starts[i].anchor == again[i].anchor, "Non-deterministic starts");
        const auto inward = i == 0 ? starts[i].chunk : glm::ivec2{9, 9} - starts[i].chunk;
        strategy::DeterministicRandom random(123U + starts[i].selectionAttempt, "starting.positions");
        const int expectedX = 2 + random.next() % 4;
        const int expectedY = 2 + random.next() % 4;
        require(inward == glm::ivec2{expectedX, expectedY}, "Player did not retry from its own initial seed");
        exercisedRetry = exercisedRetry || starts[i].selectionAttempt > 0;
        require(inward.x >= 2 && inward.x <= 5 && inward.y >= 2 && inward.y <= 5,
                "Start outside assigned corner range");
    }
    for (std::uint32_t seed = 124; seed < 140 && !exercisedRetry; ++seed) {
        const auto retried = strategy::selectStartingRegions(terrain, definitions, 10, 2, seed);
        for (std::size_t i = 0; i < retried.size(); ++i) {
            strategy::DeterministicRandom random(seed + retried[i].selectionAttempt, "starting.positions");
            const int x = 2 + random.next() % 4;
            const int y = 2 + random.next() % 4;
            const auto inward = i == 0 ? retried[i].chunk : glm::ivec2{9, 9} - retried[i].chunk;
            require(inward == glm::ivec2{x, y}, "Retry seed was shared between players");
            exercisedRetry = exercisedRetry || retried[i].selectionAttempt > 0;
        }
    }
    require(exercisedRetry, "Start fixture did not exercise rejected candidates");
    require(starts[0].landComponent == starts[1].landComponent, "Disconnected starts");
    require(glm::distance(starts[0].anchor, starts[1].anchor) >=
            strategy::MapArea{10}.extent() * definitions.matchRules().minimumOpponentSeparationNormalized,
            "Insufficient start separation");
    Fixture fixture;
    std::ifstream input("assets/gameplay/resource_fields.json");
    std::string json{std::istreambuf_iterator<char>{input}, {}};
    const std::string setting = "\"startingRequiredCapacity\": 600";
    const auto offset = json.find(setting);
    require(offset != std::string::npos, "Opening capacity setting missing");
    json.replace(offset, setting.size(), "\"startingRequiredCapacity\": 0");
    // Suppress ordinary Scrap regions, while retaining valid terrain for opening top-ups.
    const std::string threshold = "\"regionThreshold\": 0.43";
    const auto thresholdOffset = json.find(threshold);
    require(thresholdOffset != std::string::npos, "Scrap region fixture setting missing");
    json.replace(thresholdOffset, threshold.size(), "\"regionThreshold\": 1.0");
    { std::ofstream output(fixture.path); output << json; }
    const auto loadFixture = [&]() { return strategy::DefinitionRegistry{
        "assets/gameplay/units.json", "assets/gameplay/buildings.json",
        "assets/gameplay/resource_nodes.json", "assets/gameplay/resources.json",
        "assets/gameplay/weapons.json", "assets/gameplay/power.json",
        "assets/gameplay/recipes.json", "assets/gameplay/countries.json",
        "assets/gameplay/specializations.json", "assets/entities.json", "assets/text/en_us.json", "assets/textures",
        "assets/gameplay/rules.json", "assets/gameplay/upgrades.json",
        "assets/gameplay/conversions.json", "assets/gameplay/decorations.json", fixture.path}; };
    const auto ordinaryDefinitions = loadFixture();
    const std::string disabled = "\"startingRequiredCapacity\": 0";
    json.replace(json.find(disabled), disabled.size(), setting);
    { std::ofstream output(fixture.path); output << json; }
    const auto openingDefinitions = loadFixture();
    const auto withoutResources = strategy::selectStartingRegions(terrain, ordinaryDefinitions, 10, 2, 123U);
    require(withoutResources[0].anchor == starts[0].anchor && withoutResources[1].anchor == starts[1].anchor,
            "Resource settings affected starts");
    // One player isolates opening supply from multiplayer fairness compensation.
    const std::vector<glm::vec2> anchors{starts[0].anchor};
    strategy::World ordinaryWorld;
    const auto ordinary = strategy::populateResources(ordinaryWorld, terrain, ordinaryDefinitions, 123U, 10, 0.5F, anchors);
    strategy::World richWorld;
    auto& supplied = richWorld.createEntity("Existing Scrap", "scrap_node_large", 0);
    definitions.initializeEntity(supplied);
    supplied.transform.position = {anchors[0].x, 0, anchors[0].y};
    supplied.resource.remaining = 600;
    const auto rich = strategy::populateResources(richWorld, terrain, openingDefinitions, 123U, 10, 0.5F, anchors);
    require(rich.nodes.size() == ordinary.nodes.size(), "Added resources despite sufficient existing Scrap");
    strategy::World emptyWorld;
    const auto toppedUp = strategy::populateResources(emptyWorld, terrain, openingDefinitions, 123U, 10, 0.5F, anchors, &diagnostics);
    require(diagnostics.diagnostics().find("opening scrap_field player=1 existing=0") != std::string::npos &&
            diagnostics.diagnostics().find("accepted scrap_field") != std::string::npos,
            "Missing resource diagnostics");
    float nearby = 0;
    for (const auto& node : toppedUp.nodes) {
        const auto* type = definitions.resource(node.archetype);
        if (type->resourceType == "scrap" && glm::distance(node.position, anchors[0]) <= 55)
            nearby += type->resourceCapacity;
    }
    require(nearby >= 600, "Opening shortfall not filled");
    require(toppedUp.nodes.size() > ordinary.nodes.size(), "Fixture did not exercise a shortfall");
    std::cout << "Opening generation checks passed\n";
    return 0;
}

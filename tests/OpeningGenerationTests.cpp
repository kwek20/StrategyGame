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
    std::filesystem::path rules = std::filesystem::temp_directory_path() / "strategy-opening-rules.json";
    ~Fixture() { std::error_code error; std::filesystem::remove(path, error); std::filesystem::remove(rules, error); }
};
}
int strategyTestMain() {
    const strategy::DefinitionRegistry definitions;
    const strategy::Terrain terrain{123U};
    strategy::WorldGenerationProgress cancelled;
    cancelled.requestCancellation();
    bool cancellationPreserved = false;
    try {
        (void)strategy::selectStartingRegions(terrain, definitions, 10, 2, 123U, &cancelled);
    } catch (const strategy::WorldGenerationCancelled&) {
        cancellationPreserved = true;
    }
    require(cancellationPreserved, "Starting selection ignored cancellation");
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
    const std::vector<glm::vec2> anchors{starts[0].anchor, starts[1].anchor};
    strategy::World firstWorld, secondWorld, richWorld;
    const auto first = strategy::populateResources(firstWorld, terrain, definitions, 123U, 10, 0.5F, anchors, &diagnostics);
    const auto second = strategy::populateResources(secondWorld, terrain, definitions, 123U, 10, 2.0F, anchors);
    require(diagnostics.diagnostics().find("budget scrap_field") != std::string::npos,
            "Missing fixed-budget diagnostics");
    for (const auto* layout : {&first, &second}) {
        for (std::size_t player = 0; player < anchors.size(); ++player) {
            float scrap = 0, oil = 0;
            for (const auto& field : layout->fields) {
                if (field.startingPlayer != player + 1) continue;
                if (field.definition.value == "scrap_field") scrap += field.capacity;
                if (field.definition.value == "oil_field") oil += field.capacity;
                require(field.accessCost > 0, "Starting supply is unreachable");
            }
            require(scrap == 600 && oil == 400, "Starting supply changed with abundance");
        }
    }
    for (std::size_t i = 0; i < 4; ++i)
        require(first.fields[i].center == second.fields[i].center,
            "Expansion abundance moved a reserved starting field");
    const auto capacity = [&](const auto& layout, const char* family) {
        float sum = 0;
        for (const auto& node : layout.nodes)
            if (node.field.value == family) sum += definitions.resource(node.archetype)->resourceCapacity;
        return sum;
    };
    require(capacity(first, "scrap_field") == 3200 && capacity(second, "scrap_field") == 11200,
            "Map capacity differs from allocated inventory");
    strategy::WorldGenerationProgress resourceCancellation;
    resourceCancellation.requestCancellation();
    bool cancelledResources = false;
    try { (void)strategy::populateResources(richWorld, terrain, definitions, 123U, 10, 1, anchors, &resourceCancellation); }
    catch (const strategy::WorldGenerationCancelled&) { cancelledResources = true; }
    require(cancelledResources && richWorld.size() == 0, "Cancelled resource generation mutated the world");
    Fixture fixture;
    std::ifstream source("assets/gameplay/resource_fields.json");
    const std::string original{std::istreambuf_iterator<char>{source}, {}};
    const auto loadFixture = [&](const std::filesystem::path& rules = "assets/gameplay/rules.json") {
        return strategy::DefinitionRegistry{
            "assets/gameplay/units.json", "assets/gameplay/buildings.json",
            "assets/gameplay/resource_nodes.json", "assets/gameplay/resources.json",
            "assets/gameplay/weapons.json", "assets/gameplay/power.json",
            "assets/gameplay/recipes.json", "assets/gameplay/countries.json",
            "assets/gameplay/specializations.json", "assets/entities.json", "assets/text/en_us.json", "assets/textures",
            rules, "assets/gameplay/upgrades.json", "assets/gameplay/conversions.json",
            "assets/gameplay/decorations.json", fixture.path};
    };
    auto invalid = original;
    const std::string limit = "\"maximumLocalCapacity\": 2000";
    require(invalid.find(limit) != std::string::npos, "Missing capacity fixture setting");
    invalid.replace(invalid.find(limit), limit.size(), "\"maximumLocalCapacity\": 999");
    { std::ofstream output(fixture.path); output << invalid; }
    bool rejected = false;
    try { (void)loadFixture(); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "Accepted inventory exceeding local capacity limit");
    invalid = original;
    const std::string tags = "\"forbiddenTerrainTags\": [";
    invalid.insert(invalid.rfind(tags) + tags.size(), "\"land\",");
    { std::ofstream output(fixture.path); output << invalid; }
    const auto impossible = loadFixture();
    strategy::World impossibleWorld;
    rejected = false;
    strategy::WorldGenerationProgress failedDiagnostics;
    try { (void)strategy::populateResources(impossibleWorld, terrain, impossible, 123U, 10, 1, anchors, &failedDiagnostics); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected && impossibleWorld.size() == 0, "Failed inventory placement left partial resources");
    require(failedDiagnostics.diagnostics().find("layout uranium_field attempt=8 complete=0") != std::string::npos &&
            failedDiagnostics.diagnostics().find("opening oil_field player=2 capacity=400") != std::string::npos,
            "Late-family failure did not retry the inventory before aborting atomically");
    { std::ofstream output(fixture.path); output << original; }
    std::ifstream ruleSource("assets/gameplay/rules.json");
    std::string rules{std::istreambuf_iterator<char>{ruleSource}, {}};
    const std::string exclusion = "\"baseExclusionRadius\": 15";
    require(rules.find(exclusion) != std::string::npos, "Missing start-exclusion fixture setting");
    rules.replace(rules.find(exclusion), exclusion.size(), "\"baseExclusionRadius\": 60");
    { std::ofstream output(fixture.rules); output << rules; }
    const auto distant = loadFixture(fixture.rules);
    strategy::World distantWorld;
    const auto distantLayout = strategy::populateResources(distantWorld, terrain, distant, 123U, 20, 0.5F, anchors);
    for (const auto& node : distantLayout.nodes) {
        const auto player = distantLayout.fields[node.fieldIndex].startingPlayer;
        if (player) require(glm::distance(node.position, anchors[player - 1]) > 60,
            "Resource search used a fixed starting-distance cutoff");
    }
    std::cout << "Opening generation checks passed\n";
    return 0;
}

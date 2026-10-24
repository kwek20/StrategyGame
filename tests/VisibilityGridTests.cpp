#include "gameplay/DefinitionRegistry.hpp"
#include "persistence/SaveGame.hpp"
#include "players/PlayerRegistry.hpp"
#include "simulation/CombatRules.hpp"
#include "simulation/StateChecksum.hpp"
#include "world/World.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}
int strategyTestMain() {
    using namespace strategy;
    VisibilityGrid grid{1};
    require(!grid.visibleAt({-10000, 10000}) && grid.chunks().empty(),
            "Unknown space is hidden and querying it never allocates chunks");
    grid.revealCircle({-16, -16}, 14);
    require(grid.visibleAt({-24, -24}) && !grid.visibleAt({-28, -28}),
            "A circle crosses negative chunk corners without becoming a square");
    const auto allocated = grid.chunks().size();
    grid.beginTick();
    require(!grid.visibleAt({-24, -24}) && grid.exploredAt({-24, -24}) &&
                grid.chunks().size() == allocated,
            "Exploration survives while live visibility is rebuilt");
    VisibilityGrid first{1}, second{1};
    first.revealCircle({15, -2}, 8); first.revealCircle({-1, -17}, 11);
    second.revealCircle({-1, -17}, 11); second.revealCircle({15, -2}, 8);
    require(first.chunks() == second.chunks(), "Overlapping sight is independent of entity iteration order");

    DefinitionRegistry definitions;
    PlayerRegistry players;
    World world;
    const auto actorId = world.createEntity("Worker", "worker", 1).id;
    const auto targetId = world.createEntity("Enemy", "worker", 2).id;
    definitions.initializeEntity(*world.findEntity(actorId));
    definitions.initializeEntity(*world.findEntity(targetId));
    auto* actor = world.findEntity(actorId);
    auto* target = world.findEntity(targetId);
    actor->transform.position = {230, 0, 0};
    target->transform.position = {245, 0, 0};
    auto& visibility = players.find(1)->visibility;
    visibility.revealCircle({230, 0}, 20);
    require(visibility.visibleAt({245, 0}) && combatTargetVisible(players, *actor, *target, 20),
            "Terrain and entity sight continue beyond the playable edge");
    target->transform.position.x = 270;
    require(!combatTargetVisible(players, *actor, *target, 20),
            "An outside enemy cannot inherit the last visible border cell");
    visibility.beginTick();
    const auto checksum = authoritativeStateChecksum(world, players, 7, 0);
    const auto path = std::filesystem::temp_directory_path() / "strategy-world-visibility.json";
    SaveGame::write(path, 7, world, &players);
    auto data = SaveGame::read(path);
    require(data.visibility[0].exploredAt({245, 0}) && !data.visibility[0].visibleAt({245, 0}),
            "Outside exploration is saved; transient live sight is rebuilt");
    players.find(1)->visibility = std::move(data.visibility[0]);
    require(authoritativeStateChecksum(world, players, 7, 0) == checksum,
            "Sparse exploration round-trips without changing the authoritative checksum");
    std::ifstream input(path);
    std::string json{std::istreambuf_iterator<char>{input}, {}};
    input.close();
    const auto start = json.find("\"explored\"");
    require(start != std::string::npos, "Save has strict chunk exploration data");
    const auto value = json.find('"', json.find(':', start));
    json[value + 1] = '2';
    { std::ofstream output(path); output << json; }
    bool rejected = false;
    try { (void)SaveGame::read(path); } catch (const std::runtime_error&) { rejected = true; }
    std::filesystem::remove(path);
    require(rejected, "Invalid sparse fog data is rejected rather than silently migrated");
    return 0;
}

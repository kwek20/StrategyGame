#include "gameplay/DefinitionRegistry.hpp"
#include "persistence/SaveGame.hpp"
#include "simulation/CommandCodec.hpp"
#include "simulation/GameSession.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
bool allCovered(const std::vector<std::uint8_t>& cells) {
    return std::all_of(cells.begin(), cells.end(), [](auto cell) { return cell != 0; });
}
} // namespace

int strategyTestMain() {
    using namespace strategy;
    const DefinitionRegistry definitions;
    const auto research = definitions.upgradesForResearcher("command_hub");
    const auto lowAction = std::find_if(research.begin(), research.end(), [](auto* upgrade) {
        return upgrade->satelliteCoverage == SatelliteCoverage::lowResolution;
    });
    const auto highAction = std::find_if(research.begin(), research.end(), [](auto* upgrade) {
        return upgrade->satelliteCoverage == SatelliteCoverage::highResolution;
    });
    require(lowAction < highAction && highAction != research.end(),
            "Research controls show low resolution before its high-resolution successor");
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
    EntityId hub = 0, drone = 0, enemy = 0;
    for (const auto& entity : session.world().entities()) {
        if (entity.authority.owner == 1 && entity.archetype.value == "command_hub")
            hub = entity.id;
        if (entity.authority.owner == 1 && entity.archetype.value == "construction_drone")
            drone = entity.id;
        if (entity.authority.owner == 2 && entity.archetype.value == "worker")
            enemy = entity.id;
    }
    require(hub && drone && enemy, "Starting researchers, builders and enemy exist");
    auto* player = session.players().find(1);
    player->resources["alloy"] = 10000;
    player->resources["fuel"] = 10000;
    constexpr auto low = "satellite.low_resolution";
    constexpr auto high = "satellite.high_resolution";
    require(!allCovered(player->discovered) &&
                session.satelliteCoverage(1) == SatelliteCoverage::none,
            "No satellite coverage is granted for free");
    require(!session.canStartUpgrade(1, hub, high),
            "High resolution requires completed low resolution");
    const auto position = session.world().findEntity(hub)->transform.position;
    auto& second = session.world().createEntity("command_hub", "command_hub", 1);
    definitions.initializeEntity(second);
    second.transform.position = position;
    const auto secondHub = second.id;
    std::uint64_t sequence = 1;
    require(session.submit({1, sequence++, StartUpgradeCommand{hub, low}}), "Research submitted");
    session.advanceTicks();
    require(!allCovered(player->discovered), "Queued research does not reveal the world early");
    require(!session.canStartUpgrade(1, secondHub, low),
            "A second hub cannot duplicate player research");
    require(player->resources.at("alloy") == 9900, "Low resolution costs 100 Alloy exactly once");
    require(session.submit({1, sequence++, CancelProductionCommand{hub, 0}}),
            "Cancellation submitted");
    session.advanceTicks();
    require(player->resources.at("alloy") == 10000 && session.canStartUpgrade(1, secondHub, low),
            "Cancelling refunds research and releases its player-wide reservation");
    require(session.submit({1, sequence++, StartUpgradeCommand{hub, low}}),
            "Low resolution resubmitted");
    session.advanceTicks();
    session.world().findEntity(hub)->production.queue.front().remainingTicks = 1;
    session.advanceTicks();
    require(allCovered(player->discovered) && !allCovered(player->visible),
            "Completed low resolution explores all terrain without granting live global vision");
    require(player->researchedUpgrades.at(low) == 1 &&
                !session.canStartUpgrade(1, secondHub, low) &&
                session.canStartUpgrade(1, secondHub, high),
            "Research and prerequisites are shared between the player's hubs");
    require(session.players().find(2)->researchedUpgrades.empty() &&
                !allCovered(session.players().find(2)->discovered),
            "Satellite is owner-only");

    const auto enemyCell =
        MapArea{10}.gridCell({session.world().findEntity(enemy)->transform.position.x,
                              session.world().findEntity(enemy)->transform.position.z},
                             Player::explorationCells);
    require(!player->visible[enemyCell.y * Player::explorationCells + enemyCell.x],
            "Low resolution cannot see a distant enemy");

    // Find a legal site outside the starting drone's sight, then commit it and route the drone.
    PlaceBuildingCommand distant{drone, "construct.storage_silo"};
    bool found = false;
    for (float z = position.z - 80; z <= position.z + 80 && !found; z += 8)
        for (float x = position.x - 80; x <= position.x + 80 && !found; x += 8) {
            distant.position = {x, 0, z};
            const auto cell = MapArea{10}.gridCell({x, z}, Player::explorationCells);
            if (player->visible[cell.y * Player::explorationCells + cell.x])
                continue;
            found = session.planBuildingPlacement(1, distant).valid();
        }
    require(found, "A distant explored buildable site exists");
    require(session.submit({1, sequence++, distant}), "Distant placement submitted");
    session.advanceTicks();
    const auto events = session.consumePlacementEvents();
    require(events.size() == 1 && events.front().accepted(),
            "Distant satellite placement is accepted");
    require(session.world().findEntity(drone)->unitControl.order == UnitOrderKind::construct &&
                session.world().findEntity(drone)->unitControl.orderTarget ==
                    events.front().building,
            "Drone receives the distant construction order");

    const auto save =
        std::filesystem::temp_directory_path() /
        ("strategy-satellite-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
    const auto roundtrip = [&] {
        SaveGame::write(save,
                        session.terrainSeed(),
                        session.world(),
                        &session.players(),
                        10,
                        "open_plains",
                        TerrainMode::v2);
        auto data = SaveGame::read(save);
        session.replaceWorld(std::move(data.entities),
                             data.terrainSeed,
                             std::move(data.foundations),
                             data.mapChunksPerSide,
                             TerrainLayoutId{data.terrainLayout},
                             data.terrainMode,
                             data.terrainRepairLevel);
        for (std::size_t i = 0; i < 2; ++i)
            session.restorePlayerProgress(i + 1,
                                          std::move(data.resources[i]),
                                          std::move(data.discovered[i]),
                                          std::move(data.intelligence[i]),
                                          std::move(data.researchedUpgrades[i]));
    };
    roundtrip();
    require(session.satelliteCoverage(1) == SatelliteCoverage::lowResolution &&
                allCovered(player->discovered) && !allCovered(player->visible),
            "Low-resolution coverage survives saving and loading without granting enemy vision");
    sequence = 1; // replaceWorld clears the command stream.
    require(session.submit({1, sequence++, StartUpgradeCommand{secondHub, high}}),
            "High resolution submitted");
    session.advanceTicks();
    roundtrip();
    require(session.satelliteCoverage(1) == SatelliteCoverage::lowResolution &&
                !allCovered(player->visible) && !session.canStartUpgrade(1, hub, high),
            "Saving queued high resolution preserves its reservation without granting it early");
    session.world().findEntity(secondHub)->production.queue.front().remainingTicks = 1;
    session.advanceTicks();
    require(session.satelliteCoverage(1) == SatelliteCoverage::highResolution &&
                allCovered(player->visible),
            "High resolution grants live global vision on completion");
    auto* movingEnemy = session.world().findEntity(enemy);
    movingEnemy->transform.position.x -= 12;
    const auto movedPosition = movingEnemy->transform.position;
    session.advanceTicks();
    require(std::any_of(player->intelligence.begin(),
                        player->intelligence.end(),
                        [&](const auto& known) {
                            return known.id == enemy &&
                                   glm::distance(known.position, movedPosition) < .1F;
                        }),
            "High resolution tracks live enemy movement");
    session.world().destroyEntity(enemy);
    session.advanceTicks();
    require(std::none_of(player->intelligence.begin(),
                         player->intelligence.end(),
                         [&](const auto& known) { return known.id == enemy; }),
            "Live satellite intelligence removes destroyed enemies");
    session.world().destroyEntity(hub);
    session.world().destroyEntity(secondHub);
    session.advanceTicks();
    require(allCovered(player->visible),
            "Purchased orbital coverage survives losing the research hubs");
    roundtrip();
    require(session.satelliteCoverage(1) == SatelliteCoverage::highResolution &&
                allCovered(player->visible),
            "High resolution is restored before the first rendered frame");
    const auto originalChecksum = session.stateChecksum();
    player->researchedUpgrades.erase(high);
    require(session.stateChecksum() != originalChecksum,
            "Player research participates in checksums");

    const auto encoded = CommandCodec::encode({2, 1, AdvanceSatelliteCommand{}});
    const auto decoded = CommandCodec::decode(encoded);
    require(decoded && decoded->player == 2 &&
                std::holds_alternative<AdvanceSatelliteCommand>(decoded->payload),
            "The instant satellite grant uses the serializable player command stream");
    require(!CommandCodec::decode(CommandCodec::encode({2, 1, AdvanceSatelliteCommand{hub}})),
            "Player-wide satellite commands reserve entity zero");
    auto* opponent = session.players().find(2);
    opponent->resources.clear();
    require(session.submit(*decoded), "First free satellite click submitted");
    session.advanceTicks();
    require(session.satelliteCoverage(2) == SatelliteCoverage::lowResolution &&
                allCovered(opponent->discovered) && !allCovered(opponent->visible) &&
                opponent->resources.empty(),
            "First click grants low resolution without resources or research time");
    std::vector<EntityId> enemyHubs;
    for (const auto& entity : session.world().entities())
        if (entity.authority.owner == 2 && entity.archetype.value == "command_hub")
            enemyHubs.push_back(entity.id);
    for (auto id : enemyHubs)
        session.world().destroyEntity(id);
    require(session.submit({2, 2, AdvanceSatelliteCommand{}}) &&
                session.submit({2, 3, AdvanceSatelliteCommand{}}),
            "Second and repeated free satellite clicks submitted");
    session.advanceTicks();
    require(session.satelliteCoverage(2) == SatelliteCoverage::highResolution &&
                allCovered(opponent->visible) && opponent->resources.empty() &&
                session.satelliteCoverage(1) == SatelliteCoverage::lowResolution,
            "Second click grants high resolution without a hub; repeated clicks and other owners "
            "are safe");

    auto& queuedHub = session.world().createEntity("Researcher", "command_hub", 1);
    definitions.initializeEntity(queuedHub);
    queuedHub.transform.position = position;
    const auto queuedId = queuedHub.id;
    const auto resourcesBeforePurchase = player->resources;
    require(session.submit({1, 1, StartUpgradeCommand{queuedId, high}}),
            "Normal paid hub research remains available");
    session.advanceTicks();
    require(!session.world().findEntity(queuedId)->production.queue.empty(),
            "Normal hub research still queues and waits");
    require(session.submit({1, 2, AdvanceSatelliteCommand{}}),
            "Grant during queued purchase submitted");
    session.advanceTicks();
    require(session.satelliteCoverage(1) == SatelliteCoverage::highResolution &&
                player->resources == resourcesBeforePurchase &&
                session.world().findEntity(queuedId)->production.queue.empty(),
            "Instant unlocking refunds and removes the now-redundant queued purchase");
    roundtrip();
    require(session.satelliteCoverage(1) == SatelliteCoverage::highResolution &&
                session.satelliteCoverage(2) == SatelliteCoverage::highResolution,
            "Instant grants persist through the same save/load path as researched upgrades");
    std::filesystem::remove(save);
    std::cout << "Satellite research, distant placement, enemy movement and save/load passed\n";
    return 0;
}

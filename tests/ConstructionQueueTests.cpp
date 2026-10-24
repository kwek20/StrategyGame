#include "gameplay/DefinitionRegistry.hpp"
#include "persistence/SaveGame.hpp"
#include "simulation/CommandCodec.hpp"
#include "simulation/GameSession.hpp"
#include "world/World.hpp"

#include <filesystem>
#include <stdexcept>
using namespace strategy;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int strategyTestMain() {
    DefinitionRegistry definitions;
    World world;
    const auto add = [&](const char* type, glm::vec3 position) {
        auto& e = world.createEntity(type, type, 1);
        definitions.initializeEntity(e);
        e.transform.position = position;
        return e.id;
    };
    const auto hub = add("command_hub", {-18, 0, 0});
    const auto drone = add("construction_drone", {0, 6, 0});
    const auto planned = [&](float x) {
        const auto id = add("electricity_pole", {x, 0, 0});
        auto* e = world.findEntity(id);
        e->construction.emplace();
        e->construction.recipeId = "construct.electricity_pole";
        e->construction.powerRequired = 30;
        e->construction.state = BuildingLifecycleState::planned;
        return id;
    };
    const auto first = planned(0), second = planned(10), third = planned(20);
    GameSession a{definitions, 17}, b{definitions, 17};
    a.replaceWorld(world.entities(), 17);
    b.replaceWorld(world.entities(), 17);
    for (auto* session : {&a, &b}) {
        require(session->submit({1, 1, ConstructCommand{drone, first, false}}),
                "Initial construction command accepted");
        require(session->submit({1, 2, ConstructCommand{drone, second, true}}),
                "Second construction queued");
        require(session->submit({1, 3, ConstructCommand{drone, third, true}}),
                "Third construction queued");
        session->advanceTicks();
        require(session->world().findEntity(drone)->unitControl.orderTarget == first &&
                    session->world().findEntity(drone)->unitControl.constructionQueue ==
                        std::deque<EntityId>{second, third},
                "Repeated construction preserves the active target and FIFO work");
        session->world().findEntity(drone)->battery.charge = 0;
        session->advanceTicks();
        require(session->world().findEntity(drone)->battery.hasSuspendedOrder &&
                    session->world().findEntity(drone)->unitControl.constructionQueue.size() == 2,
                "Recharge suspends active work without losing the remaining queue");
    }
    require(a.stateChecksum() == b.stateChecksum(), "Queued work is deterministic");
    const auto checksum = a.stateChecksum();
    a.world().findEntity(drone)->unitControl.constructionQueue.pop_back();
    require(a.stateChecksum() != checksum,
            "Queued construction participates in authoritative checksums");
    a.world().findEntity(drone)->unitControl.constructionQueue.push_back(third);
    const auto path = std::filesystem::temp_directory_path() / "strategy-construction-queue.json";
    SaveGame::write(path, 17, a.world(), &a.players());
    auto saved = SaveGame::read(path);
    const auto restored = std::find_if(
        saved.entities.begin(), saved.entities.end(), [&](const auto& e) { return e.id == drone; });
    require(restored != saved.entities.end() &&
                restored->unitControl.constructionQueue == std::deque<EntityId>{second, third} &&
                restored->battery.suspendedTarget == first,
            "Save/load retains queued and suspended construction");
    std::filesystem::remove(path);
    for (auto* session : {&a, &b}) {
        session->submit({1, 4, CancelConstructionCommand{second}});
        session->advanceTicks();
        require(session->world().findEntity(drone)->unitControl.constructionQueue ==
                    std::deque<EntityId>{third},
                "Cancelling a queued blueprint removes it from builder queues");
        session->advanceTicks(900);
        require(isOperational(*session->world().findEntity(first)) &&
                    isOperational(*session->world().findEntity(third)) &&
                    session->world().findEntity(drone)->unitControl.constructionQueue.empty(),
                "A recharged drone finishes the surviving queued buildings");
    }
    require(a.stateChecksum() == b.stateChecksum(),
            "Queue completion and cancellation remain deterministic");
    const auto wire = CommandCodec::decode(CommandCodec::encode(
        {1,
         20,
         PlaceBuildingCommand{
             drone, "construct.electricity_pole", {30, 0, 0}, {drone}, 45, true}}));
    require(wire && std::get<PlaceBuildingCommand>(wire->payload).queueBuilders,
            "Wire commands retain repeat-placement intent");
    const auto construct =
        CommandCodec::decode(CommandCodec::encode({1, 21, ConstructCommand{drone, third, true}}));
    require(construct && std::get<ConstructCommand>(construct->payload).queued,
            "Wire commands retain queued construction orders");
    (void)hub;
    return 0;
}

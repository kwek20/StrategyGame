#include "simulation/CommandCodec.hpp"

#include <bit>
#include <type_traits>

namespace strategy {
namespace {
template <class Value> void write(std::vector<std::byte>& output, Value value) {
    using Bits = std::conditional_t<sizeof(Value) == 8, std::uint64_t, std::uint32_t>;
    Bits bits = std::bit_cast<Bits>(value);
    for (std::size_t index = 0; index < sizeof(Value); ++index)
        output.push_back(static_cast<std::byte>((bits >> (index * 8)) & 0xffU));
}
template <class Value> bool read(std::span<const std::byte>& input, Value& value) {
    if (input.size() < sizeof(Value)) return false;
    using Bits = std::conditional_t<sizeof(Value) == 8, std::uint64_t, std::uint32_t>;
    Bits bits = 0;
    for (std::size_t index = 0; index < sizeof(Value); ++index)
        bits |= static_cast<Bits>(std::to_integer<unsigned>(input[index])) << (index * 8);
    value = std::bit_cast<Value>(bits);
    input = input.subspan(sizeof(Value));
    return true;
}
void writeString(std::vector<std::byte>& output, std::string_view value) {
    write(output, static_cast<std::uint32_t>(value.size()));
    for (char character : value)
        output.push_back(static_cast<std::byte>(character));
}
bool readString(std::span<const std::byte>& input, std::string& value) {
    std::uint32_t size = 0;
    if (!read(input, size) || input.size() < size)
        return false;
    value.assign(reinterpret_cast<const char*>(input.data()), size);
    input = input.subspan(size);
    return true;
}
} // namespace

std::vector<std::byte> CommandCodec::encode(const PlayerCommand& command) {
    std::vector<std::byte> result;
    result.push_back(std::byte{3});
    write(result, command.player);
    write(result, command.sequence);
    result.push_back(static_cast<std::byte>(command.payload.index()));
    std::visit([&](const auto& payload) {
        write(result, payload.entity);
        using T = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<T, DirectUnitInputCommand>) {
            write(result, payload.movement.x); write(result, payload.movement.y);
            result.push_back(payload.running ? std::byte{1} : std::byte{0});
            write(result, payload.facingDegrees);
        } else if constexpr (std::is_same_v<T, MoveUnitCommand>) {
            write(result, payload.destination.x); write(result, payload.destination.y);
            write(result, payload.destination.z);
        } else if constexpr (std::is_same_v<T, GatherResourceCommand>) write(result, payload.resource);
        else if constexpr (std::is_same_v<T, AttackEntityCommand>) write(result, payload.target);
        else if constexpr (std::is_same_v<T, StartRecipeCommand>) writeString(result, payload.recipeId.value);
        else if constexpr (std::is_same_v<T, StartUpgradeCommand>) writeString(result, payload.upgradeId);
        else if constexpr (std::is_same_v<T, PlaceBuildingCommand>) {
            writeString(result, payload.buildingId);
            write(result,payload.position.x); write(result,payload.position.y); write(result,payload.position.z);
            write(result, static_cast<std::uint32_t>(payload.builders.size()));
            for (EntityId builder : payload.builders) write(result, builder);
        }
        else if constexpr (std::is_same_v<T, ConstructCommand>) write(result, payload.building);
        else if constexpr (std::is_same_v<T, RepairCommand>) write(result, payload.target);
    }, command.payload);
    return result;
}

std::optional<PlayerCommand> CommandCodec::decode(std::span<const std::byte> input) {
    if (input.empty() || input.front() != std::byte{3}) return std::nullopt;
    input = input.subspan(1);
    PlayerCommand result;
    if (!read(input, result.player) || !read(input, result.sequence) || input.empty()) return std::nullopt;
    const unsigned kind = std::to_integer<unsigned>(input.front()); input = input.subspan(1);
    EntityId entity{}; if (!read(input, entity)) return std::nullopt;
    switch (kind) {
    case 0: result.payload = PossessUnitCommand{entity}; break;
    case 1: result.payload = ReleaseUnitCommand{entity}; break;
    case 2: { DirectUnitInputCommand value{entity};
        if (!read(input,value.movement.x)||!read(input,value.movement.y)||input.empty()) return std::nullopt;
        value.running=input.front()!=std::byte{0}; input=input.subspan(1);
        if(!read(input,value.facingDegrees)) return std::nullopt; result.payload=value; break; }
    case 3: { MoveUnitCommand value{entity}; if(!read(input,value.destination.x)||!read(input,value.destination.y)||!read(input,value.destination.z)) return std::nullopt; result.payload=value; break; }
    case 4: { EntityId target{}; if(!read(input,target)) return std::nullopt; result.payload=GatherResourceCommand{entity,target}; break; }
    case 5: { EntityId target{}; if(!read(input,target)) return std::nullopt; result.payload=AttackEntityCommand{entity,target}; break; }
    case 6: {
        StartRecipeCommand value;
        value.entity = entity;
        std::string recipe;
        if (!readString(input, recipe)) return std::nullopt;
        value.recipeId = RecipeId{recipe};
        result.payload = std::move(value);
        break;
    }
    case 7: {
        StartUpgradeCommand value{entity};
        if (!readString(input, value.upgradeId)) return std::nullopt;
        result.payload = std::move(value);
        break;
    }
    case 8: {
        PlaceBuildingCommand value{entity};
        std::uint32_t builderCount = 0;
        if(!readString(input,value.buildingId)||!read(input,value.position.x)||
           !read(input,value.position.y)||!read(input,value.position.z)||
           !read(input,builderCount) || builderCount > 1024) return std::nullopt;
        value.builders.reserve(builderCount);
        for (std::uint32_t index = 0; index < builderCount; ++index) {
            EntityId builder = 0;
            if (!read(input, builder)) return std::nullopt;
            value.builders.push_back(builder);
        }
        result.payload=std::move(value); break;
    }
    case 9: { EntityId building{}; if(!read(input,building)) return std::nullopt; result.payload=ConstructCommand{entity,building}; break; }
    case 10: result.payload=StopConstructionCommand{entity}; break;
    case 11: { EntityId target{}; if(!read(input,target)) return std::nullopt; result.payload=RepairCommand{entity,target}; break; }
    default: return std::nullopt;
    }
    return input.empty() ? std::optional<PlayerCommand>{std::move(result)} : std::nullopt;
}

} // namespace strategy

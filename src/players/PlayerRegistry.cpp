#include "players/PlayerRegistry.hpp"
#include <utility>

namespace strategy {

PlayerRegistry::PlayerRegistry(std::string playerOneCountry,std::string playerTwoCountry,
                               std::string playerOneSpecialization,std::string playerTwoSpecialization)
    : players_{{{1,1,"Player 1",std::move(playerOneCountry),std::move(playerOneSpecialization)},
                {2,2,"Player 2",std::move(playerTwoCountry),std::move(playerTwoSpecialization)}}} {}

const Player* PlayerRegistry::find(PlayerId id) const {
    for (const Player& player : players_) {
        if (player.id == id) {
            return &player;
        }
    }
    return nullptr;
}
Player* PlayerRegistry::find(PlayerId id) {
    for(Player& player:players_)if(player.id==id)return &player;
    return nullptr;
}

} // namespace strategy

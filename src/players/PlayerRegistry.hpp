#pragma once

#include "players/Player.hpp"

#include <array>

namespace strategy {

class PlayerRegistry final {
  public:
    explicit PlayerRegistry(std::string playerOneCountry = "spain",
                            std::string playerTwoCountry = "japan",
                            std::string playerOneSpecialization = "unassigned",
                            std::string playerTwoSpecialization = "unassigned");

    [[nodiscard]] const Player* find(PlayerId id) const;
    [[nodiscard]] Player* find(PlayerId id);
    [[nodiscard]] std::array<Player, 2>& players() {
        return players_;
    }
    [[nodiscard]] const std::array<Player, 2>& players() const {
        return players_;
    }

  private:
    std::array<Player, 2> players_;
};

} // namespace strategy

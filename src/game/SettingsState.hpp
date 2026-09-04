#pragma once
#include "app/GameState.hpp"
#include "persistence/GameConfig.hpp"
#include <array>
namespace strategy {
class SettingsState final:public GameState{
public:SettingsState();void handleEvent(const SDL_Event&)override;void update(float)override{};void render(Renderer&)const override;[[nodiscard]]StateRequest request()const override{return request_;}
private:GameConfig config_;StateRequest request_{StateRequest::none};std::size_t resolution_{0};int hovered_{-1};int binding_{-1};std::uint32_t windowId_{0};static constexpr std::array<std::pair<int,int>,4> resolutions{{{1280,720},{1600,900},{1920,1080},{2560,1440}}};void apply();};
}

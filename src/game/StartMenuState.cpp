#include "game/StartMenuState.hpp"

#include "render/Renderer.hpp"
#include "localization/Text.hpp"

#include <SDL3/SDL.h>

#include <charconv>

namespace strategy {

StartMenuState::StartMenuState() {
    const auto& countries=countryCatalogue_.countries();
    for(std::size_t index=0;index<countries.size();++index) {
        if(countries[index].id=="spain")playerOneCountryIndex_=index;
        if(countries[index].id=="japan")playerTwoCountryIndex_=index;
    }
}

void StartMenuState::cycleCountry(std::size_t& index,int direction) {
    const std::size_t count=countryCatalogue_.countries().size();
    index=static_cast<std::size_t>((static_cast<int>(index)+direction+static_cast<int>(count))%static_cast<int>(count));
}

void StartMenuState::updateHover(float mouseX, float mouseY) {
    const bool insideHorizontal = mouseX >= buttonLeft && mouseX <= buttonRight;
    startHovered_ = insideHorizontal && mouseY >= startTop && mouseY <= startBottom;
    buildHovered_ = insideHorizontal && mouseY >= buildTop && mouseY <= buildBottom;
    loadHovered_ = insideHorizontal && mouseY >= loadTop && mouseY <= loadBottom;
    settingsHovered_=insideHorizontal&&mouseY>=settingsTop&&mouseY<=settingsBottom;
    exitHovered_ = insideHorizontal && mouseY >= exitTop && mouseY <= exitBottom;
}

void StartMenuState::handleEvent(const SDL_Event& event) {
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        updateHover(event.motion.x, event.motion.y);
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
        && event.button.button == SDL_BUTTON_LEFT) {
        updateHover(event.button.x, event.button.y);
        seedFocused_ = event.button.x >= 60.0F && event.button.x <= 300.0F
                    && event.button.y >= 185.0F && event.button.y <= 230.0F;
        if(event.button.y>=185.0F&&event.button.y<=230.0F&&event.button.x>=380.0F&&event.button.x<=760.0F) {
            cycleCountry(playerOneCountryIndex_,event.button.x<570.0F?-1:1); return;
        }
        if(event.button.y>=250.0F&&event.button.y<=295.0F&&event.button.x>=380.0F&&event.button.x<=760.0F) {
            cycleCountry(playerTwoCountryIndex_,event.button.x<570.0F?-1:1); return;
        }
        if (startHovered_) {
            request_ = StateRequest::startGame;
        } else if (buildHovered_) {
            request_ = StateRequest::buildMap;
        } else if (loadHovered_) {
            request_ = StateRequest::loadGame;
        } else if(settingsHovered_) {
            request_=StateRequest::openSettings;
        } else if (exitHovered_) {
            request_ = StateRequest::exitApplication;
        }
    }
    if (event.type == SDL_EVENT_KEY_DOWN) {
        if(event.key.key==SDLK_Q){cycleCountry(playerOneCountryIndex_,-1);return;}
        if(event.key.key==SDLK_E){cycleCountry(playerOneCountryIndex_,1);return;}
        if(event.key.key==SDLK_Z){cycleCountry(playerTwoCountryIndex_,-1);return;}
        if(event.key.key==SDLK_C){cycleCountry(playerTwoCountryIndex_,1);return;}
        if (seedFocused_ && event.key.key == SDLK_BACKSPACE) {
            if (!seedText_.empty()) {
                seedText_.pop_back();
            }
            return;
        }
        if (seedFocused_ && event.key.key >= SDLK_0 && event.key.key <= SDLK_9) {
            if (seedText_.size() < 10) {
                seedText_.push_back(static_cast<char>('0' + event.key.key - SDLK_0));
            }
            return;
        }
        if (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE) {
            request_ = StateRequest::startGame;
        } else if (event.key.key == SDLK_ESCAPE) {
            request_ = StateRequest::exitApplication;
        }
    }
}

void StartMenuState::update(float deltaSeconds) {
    (void)deltaSeconds;
}

void StartMenuState::render(Renderer& renderer) const {
    renderer.drawStartMenu(startHovered_, buildHovered_, loadHovered_,settingsHovered_, exitHovered_,
                           seedFocused_, seedText_,
                           Text::get(countryCatalogue_.countries()[playerOneCountryIndex_].nameKey),
                           Text::get(countryCatalogue_.countries()[playerTwoCountryIndex_].nameKey));
}

std::string StartMenuState::playerOneCountry() const { return countryCatalogue_.countries()[playerOneCountryIndex_].id; }
std::string StartMenuState::playerTwoCountry() const { return countryCatalogue_.countries()[playerTwoCountryIndex_].id; }

std::uint32_t StartMenuState::terrainSeed() const {
    std::uint32_t result = 0x5EED1234U;
    const auto conversion = std::from_chars(seedText_.data(),
                                             seedText_.data() + seedText_.size(), result);
    return conversion.ec == std::errc{} ? result : 0x5EED1234U;
}

} // namespace strategy

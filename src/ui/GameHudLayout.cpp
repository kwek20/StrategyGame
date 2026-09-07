#include "ui/GameHudLayout.hpp"
#include "ui/UiLayout.hpp"
#include "localization/Text.hpp"

#include <algorithm>

namespace strategy {

UiDocument GameHudLayout::resources(std::size_t localResourceCount,
                                    std::size_t powerDeviceCount,
                                    bool powerOverlayVisible,
                                    int viewportWidth,
                                    int viewportHeight,
                                    float uiScale) {
    UiDocument ui;
    const UiLayout canvas(viewportWidth, viewportHeight, uiScale);
    const UiRect resources = canvas.rect(UiAnchor::topLeft, 10, 10, 550, 34, 420, 34);
    ui.panel("resources.panel", resources,
             {0.018F, 0.028F, 0.038F});
    for (std::size_t i = 0; i < localResourceCount; ++i) {
        const float left = resources.left + canvas.value(8 + i * 106.0F);
        ui.label("resources.local." + std::to_string(i),
                 {left, resources.top + canvas.value(3), left + canvas.value(96),
                  resources.bottom - canvas.value(3)}, {});
    }
    const float powerLeft = resources.left + canvas.value(8 + localResourceCount * 106.0F);
    auto& power = ui.iconButton("resources.power", {powerLeft, resources.top,
                                powerLeft + canvas.value(122), resources.bottom},
                                "resource_power", "Power grid");
    power.color = {0.018F, 0.028F, 0.038F};
    power.hoverColor = {0.08F, 0.15F, 0.20F};
    if (powerOverlayVisible) {
        const float desiredHeight = 102.0F + static_cast<float>(powerDeviceCount) * 22.0F;
        const UiRect overlay = canvas.rect(UiAnchor::topLeft, 330, 52, 320,
                                           std::min(desiredHeight, 650.0F), 260, 102);
        ui.panel("power.panel", overlay,
                 {0.025F, 0.04F, 0.06F});
    }
    return ui;
}

UiDocument GameHudLayout::minimap(int viewportWidth, int viewportHeight, float uiScale) {
    UiDocument ui;
    const UiLayout canvas(viewportWidth, viewportHeight, uiScale);
    const UiRect map = canvas.rect(UiAnchor::topRight, 20, 20, 190, 190, 150, 150);
    ui.panel("strategy.minimap", map, {0.008F, 0.012F, 0.016F});
    ui.label("strategy.minimap.title",
             {map.left + canvas.value(8), map.top + canvas.value(4), 0, 0},
             Text::get("strategy.minimap"), 1.5F * canvas.scale());
    return ui;
}

UiDocument GameHudLayout::loading(float progress, const std::string& status,
                                  int viewportWidth, int viewportHeight, float uiScale) {
    UiDocument ui;
    const UiLayout canvas(viewportWidth, viewportHeight, uiScale);
    const UiRect content = canvas.rect(UiAnchor::center, 0, 0, 520, 130, 360, 110);
    ui.label("loading.title", {content.left + canvas.value(155), content.top, 0, 0},
             Text::get("loading.title"), 3.0F * canvas.scale());
    ui.label("loading.status", {content.left + canvas.value(50),
             content.top + canvas.value(48), content.right - canvas.value(50),
             content.top + canvas.value(70)}, status, 1.4F * canvas.scale(),
             {0.65F, 0.74F, 0.78F});
    ui.progressBar("loading.progress",
        {content.left + canvas.value(50), content.top + canvas.value(88),
         content.right - canvas.value(50), content.top + canvas.value(108)},
        progress, {0.18F, 0.76F, 0.88F});
    return ui;
}

} // namespace strategy

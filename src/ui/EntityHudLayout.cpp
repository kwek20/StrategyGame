#include "ui/EntityHudLayout.hpp"
#include "ui/UiLayout.hpp"
#include "localization/Text.hpp"

#include <algorithm>
#include <charconv>

namespace strategy {
namespace {
constexpr std::string_view actionPrefix = "action:";
constexpr std::string_view queuePrefix = "queue:";
constexpr std::string_view selectionPrefix = "selection:";
}

std::string EntityHudLayout::selectionElementId(std::string_view archetype) {
    return std::string(selectionPrefix) + std::string(archetype);
}

std::optional<std::string> EntityHudLayout::selectionArchetype(std::string_view elementId) {
    if (!elementId.starts_with(selectionPrefix)) return std::nullopt;
    return std::string(elementId.substr(selectionPrefix.size()));
}

std::string EntityHudLayout::actionElementId(std::string_view actionId) {
    return std::string(actionPrefix) + std::string(actionId);
}

std::string EntityHudLayout::queueElementId(std::size_t index) {
    return std::string(queuePrefix) + std::to_string(index);
}

std::optional<std::string> EntityHudLayout::actionId(std::string_view elementId) {
    if (!elementId.starts_with(actionPrefix)) return std::nullopt;
    return std::string(elementId.substr(actionPrefix.size()));
}

std::optional<std::size_t> EntityHudLayout::queueIndex(std::string_view elementId) {
    if (!elementId.starts_with(queuePrefix)) return std::nullopt;
    std::size_t result = 0;
    const std::string_view value = elementId.substr(queuePrefix.size());
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) return std::nullopt;
    return result;
}

UiDocument EntityHudLayout::construction(const EntityHudModel& model, int width, int height,
                                         float uiScale) {
    UiDocument document;
    const UiLayout canvas(width, height, uiScale);
    const UiRect panel = canvas.rect(UiAnchor::bottomLeft, 18, 18, 622, 217, 420, 180);
    document.panel("construction.panel", panel,
                   {0.025F, 0.04F, 0.06F});
    const std::size_t count = std::min<std::size_t>(model.actions.size(), 8);
    for (std::size_t i = 0; i < count; ++i) {
        const HudActionModel& action = model.actions[i];
        const float left = panel.left + canvas.value(12.0F + static_cast<float>(i) * 70.0F);
        auto& button = document.iconButton(actionElementId(action.id),
            {left, panel.bottom - canvas.value(74), left + canvas.value(60),
             panel.bottom - canvas.value(12)}, action.icon,
            action.name + "  " + action.cost + "  " + action.description, action.enabled);
        button.focused = action.active;
    }
    return document;
}

UiDocument EntityHudLayout::actions(const EntityHudModel& model, int width, int height,
                                    float uiScale) {
    UiDocument document;
    const UiLayout canvas(width, height, uiScale);
    const UiRect panel = canvas.rect(UiAnchor::bottomLeft, 18, 18, 622, 340, 420, 250);
    document.panel("entity.panel", panel,
                   {0.025F, 0.04F, 0.06F});
    document.label("entity.title", {panel.left + canvas.value(12), panel.top + canvas.value(12), 0, 0},
                   model.title, 2.0F * canvas.scale());
    // A multi-entity selection is represented by its compact entity cards. Drawing the
    // single-entity portrait as well duplicates the first unit and overlaps that row.
    if (model.cards.empty()) {
        document.entityCard("entity.portrait",
            {panel.left + canvas.value(12), panel.top + canvas.value(35),
             panel.left + canvas.value(84), panel.top + canvas.value(107)}, model.portraitIcon);
    }
    const std::size_t visibleCards = std::min<std::size_t>(model.cards.size(), 9);
    for (std::size_t i = 0; i < visibleCards; ++i) {
        const float left = panel.left + canvas.value(12 + i * 54.0F);
        const float iconTop = panel.top + canvas.value(42);
        const float iconBottom = panel.top + canvas.value(76);
        document.entityCard("entity.card." + std::to_string(i),
            {left, iconTop, left + canvas.value(44), iconBottom}, model.cards[i].icon);
        const std::size_t visibleBars = std::min<std::size_t>(model.cards[i].bars.size(), 2);
        for (std::size_t barIndex = 0; barIndex < visibleBars; ++barIndex) {
            const HudBarModel& bar = model.cards[i].bars[barIndex];
            const float barTop = panel.top + canvas.value(78.0F + barIndex * 5.0F);
            auto& element = document.progressBar(
                "entity.card." + std::to_string(i) + ".bar." + std::to_string(barIndex),
                {left, barTop, left + canvas.value(44), barTop + canvas.value(3)},
                bar.maximum > 0.0F ? bar.current / bar.maximum : 0.0F,
                bar.kind == HudBarKind::health ? glm::vec3{0.24F, 0.78F, 0.30F}
                                               : glm::vec3{0.20F, 0.68F, 0.95F});
            element.color = {0.10F, 0.12F, 0.13F};
        }
    }
    for (std::size_t i = 0; i < model.bars.size(); ++i) {
        const auto& bar = model.bars[i];
        const float y = panel.top + canvas.value(48.0F + static_cast<float>(i) * 25.0F);
        auto& element = document.progressBar("entity.bar." + std::to_string(i),
            {panel.left + canvas.value(100), y, panel.left + canvas.value(372), y + canvas.value(9)},
            bar.maximum > 0 ? bar.current / bar.maximum : 0,
            bar.kind == HudBarKind::health ? glm::vec3{0.24F, 0.78F, 0.30F}
                                           : glm::vec3{0.20F, 0.68F, 0.95F});
        element.color = {0.10F, 0.12F, 0.13F};
        document.label("entity.bar.label." + std::to_string(i),
            {panel.left + canvas.value(384), y - canvas.value(9), 0, 0},
            bar.label + " " + std::to_string(static_cast<int>(bar.current)) + " / " +
                std::to_string(static_cast<int>(bar.maximum)), 1.0F * canvas.scale());
    }
    std::string stats;
    for (const auto& stat : model.stats)
        stats += (stats.empty() ? "" : "   ") + stat.label + " " + stat.value;
    document.label("entity.stats", {panel.left + canvas.value(100), panel.top + canvas.value(112), 0, 0},
                   stats, 1.0F * canvas.scale(), {0.75F, 0.84F, 0.88F});
    document.label("entity.footer", {panel.left + canvas.value(384), panel.top + canvas.value(86), 0, 0},
                   model.footer, 1.05F * canvas.scale(), {0.82F, 0.88F, 0.90F});
    document.label("entity.tooltip", {panel.left + canvas.value(12), panel.top + canvas.value(137),
                   panel.right - canvas.value(12), panel.top + canvas.value(160)},
                   {}, 1.25F * canvas.scale(), {0.96F, 0.90F, 0.58F});
    const std::size_t queueCount = std::min<std::size_t>(model.queue.size(), 9);
    const UiRect queueArea{panel.left + canvas.value(12), panel.top + canvas.value(170),
                           panel.left + canvas.value(12 + static_cast<float>(queueCount) * 54),
                           panel.top + canvas.value(214)};
    const auto queueCells = canvas.row(queueArea, queueCount, 10, 44);
    for (std::size_t i = 0; i < queueCount; ++i) {
        document.queueSlot(queueElementId(i), queueCells[i],
                           model.queue[i].icon,
                           model.queue[i].name + "  " + Text::get("entity_hud.cancel_refund"),
                           model.queue[i].cancellable);
    }
    if (!model.queue.empty())
        document.progressBar("entity.queue.progress",
            {panel.left + canvas.value(12), panel.top + canvas.value(224),
             panel.right - canvas.value(20), panel.top + canvas.value(234)},
            model.queue.front().progress);
    const std::size_t actionCount = std::min<std::size_t>(model.actions.size(), 6);
    const UiRect actionArea{panel.left + canvas.value(12), panel.bottom - canvas.value(68),
                            panel.left + canvas.value(
                                12.0F + static_cast<float>(actionCount) * 96.0F),
                            panel.bottom - canvas.value(12)};
    const auto actionCells = canvas.row(actionArea, actionCount, 14, 82);
    for (std::size_t i = 0; i < actionCount; ++i) {
        const HudActionModel& action = model.actions[i];
        document.iconButton(actionElementId(action.id), actionCells[i], action.icon,
            action.name + "  " + action.cost + "  " + action.description +
                (!action.enabled && !action.disabledReason.empty()
                    ? "  " + action.disabledReason : ""), action.enabled);
    }
    return document;
}

UiDocument EntityHudLayout::selection(const EntityHudModel& model, int width, int height,
                                      float uiScale) {
    UiDocument document;
    const UiLayout canvas(width, height, uiScale);
    const std::size_t visible = std::min<std::size_t>(model.selectionGroups.size(), 6);
    const float panelHeight = 58.0F + static_cast<float>(visible) * 34.0F;
    const UiRect panel = canvas.rect(UiAnchor::bottomLeft, 18, 18, 392, panelHeight, 300, 100);
    document.panel("selection.panel", panel,
                   {0.025F, 0.04F, 0.06F});
    document.label("selection.title", {panel.left + canvas.value(12),
                   panel.top + canvas.value(8), 0, 0},
                   Text::format("selection.title", {std::to_string(model.totalEntities)}),
                   1.8F * canvas.scale());
    for (std::size_t i = 0; i < visible; ++i) {
        const auto& group = model.selectionGroups[i];
        document.entityCard(selectionElementId(group.archetype),
            {panel.left + canvas.value(12), panel.top + canvas.value(36 + static_cast<float>(i) * 34),
             panel.right - canvas.value(12), panel.top + canvas.value(66 + static_cast<float>(i) * 34)},
            group.icon, group.name);
        document.label("selection.label." + std::to_string(i),
            {panel.left + canvas.value(46), panel.top + canvas.value(40 + static_cast<float>(i) * 34), 0, 0},
            Text::format("selection.group", {group.name, std::to_string(group.count)}),
            1.25F * canvas.scale());
    }
    return document;
}

UiDocument EntityHudLayout::directControl(const EntityHudModel& model, int width, int height,
                                          float uiScale) {
    UiDocument document = actions(model, width, height, uiScale);
    if (UiElement* panel = [&]() -> UiElement* {
            for (UiElement& element : document.elements())
                if (element.id == "entity.panel") return &element;
            return nullptr;
        }()) {
        const UiLayout canvas(width, height, uiScale);
        const UiRect compact = canvas.rect(UiAnchor::bottomLeft, 18, 18, 622, 142, 420, 120);
        const float shift = compact.top - panel->bounds.top;
        for (UiElement& element : document.elements()) {
            element.bounds.top += shift;
            element.bounds.bottom += shift;
        }
        panel->bounds = compact;
    }
    return document;
}

} // namespace strategy

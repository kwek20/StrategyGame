#include "ui/EntityHudLayout.hpp"
#include "ui/UiLayout.hpp"
#include "localization/Text.hpp"

#include <algorithm>
#include <charconv>
#include <iomanip>
#include <sstream>

namespace strategy {
namespace {
constexpr std::string_view actionPrefix = "action:";
constexpr std::string_view queuePrefix = "queue:";
constexpr std::string_view selectionPrefix = "selection:";
std::string decimal(float value) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(1) << value;
    return output.str();
}
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
    return actions(model, width, height, uiScale);
}

UiDocument EntityHudLayout::actions(const EntityHudModel& model, int width, int height,
                                    float uiScale) {
    UiDocument document;
    const UiLayout canvas(width, height, uiScale);
    const std::size_t actionCount = std::min<std::size_t>(model.actions.size(), 9);
    const UiRect panel = canvas.rect(UiAnchor::bottomLeft, 18, 18, 800, 300, 560, 260);
    const float inset = canvas.value(10);
    const float actionWidth = canvas.value(250);
    const UiRect actionPanel{panel.left + inset, panel.top + inset,
                             panel.left + inset + actionWidth, panel.bottom - inset};
    const UiRect infoPanel{actionPanel.right + inset, panel.top + inset,
                           panel.right - inset, panel.bottom - inset};
    document.panel("entity.panel", panel, {0.018F, 0.028F, 0.040F});
    document.panel("entity.actions.panel", actionPanel, {0.030F, 0.050F, 0.070F});
    document.panel("entity.info.panel", infoPanel, {0.025F, 0.040F, 0.058F});

    // Left: all commands, production choices and upgrades share one predictable grid.
    constexpr std::size_t actionColumns = 3;
    const float actionGap = canvas.value(8);
    const float actionCellWidth =
        (actionPanel.right - actionPanel.left - canvas.value(16) - actionGap * 2.0F) / 3.0F;
    const float actionCellHeight = canvas.value(56);
    for (std::size_t i = 0; i < actionCount; ++i) {
        const std::size_t column = i % actionColumns;
        const std::size_t row = i / actionColumns;
        const float left = actionPanel.left + canvas.value(8) +
                           static_cast<float>(column) * (actionCellWidth + actionGap);
        const float top = actionPanel.top + canvas.value(8) +
                          static_cast<float>(row) * (actionCellHeight + actionGap);
        const HudActionModel& action = model.actions[i];
        auto& button = document.iconButton(actionElementId(action.id),
            {left, top, left + actionCellWidth, top + actionCellHeight}, action.icon,
            action.name + "  " + action.cost + "  " + action.description +
                (!action.enabled && !action.disabledReason.empty()
                    ? "  " + action.disabledReason : ""), action.enabled);
        button.focused = action.active;
    }

    document.label("entity.tooltip",
        {actionPanel.left + canvas.value(8), actionPanel.bottom - canvas.value(40),
         actionPanel.right - canvas.value(8), actionPanel.bottom - canvas.value(10)},
        {}, 1.1F * canvas.scale(), {0.96F, 0.90F, 0.58F});

    // Queues are a separate strip above the entity menu and never resize its contents.
    if (!model.queue.empty()) {
        const UiRect queuePanel{panel.left, panel.top - canvas.value(66),
                                panel.right, panel.top - canvas.value(8)};
        document.panel("entity.queue.panel", queuePanel, {0.025F, 0.040F, 0.058F});
        const std::size_t queueCount = std::min<std::size_t>(model.queue.size(), 9);
        const float queueSlotSize = canvas.value(44);
        const float queueGap = canvas.value(8);
        const float queueTop = queuePanel.top + canvas.value(7);
        const float queueLeft = queuePanel.left + canvas.value(8);
        for (std::size_t i = 0; i < queueCount; ++i) {
            const float left = queueLeft + static_cast<float>(i) * (queueSlotSize + queueGap);
            document.queueSlot(queueElementId(i),
                               {left, queueTop, left + queueSlotSize,
                                queueTop + queueSlotSize}, model.queue[i].icon,
                               model.queue[i].name + "  " + Text::get("entity_hud.cancel_refund"),
                               model.queue[i].cancellable);
        }
        document.progressBar("entity.queue.progress",
            {queuePanel.left + canvas.value(8), queuePanel.bottom - canvas.value(9),
             queuePanel.right - canvas.value(8), queuePanel.bottom - canvas.value(4)},
            model.queue.front().progress);
    }

    // Right: identity and component-derived information, never interactive actions.
    document.label("entity.title", {infoPanel.left + canvas.value(12),
                   infoPanel.top + canvas.value(10), infoPanel.right - canvas.value(12),
                   infoPanel.top + canvas.value(34)},
                   model.title, 2.0F * canvas.scale());
    // A multi-entity selection is represented by its compact entity cards. Drawing the
    // single-entity portrait as well duplicates the first unit and overlaps that row.
    if (model.cards.empty()) {
        document.entityCard("entity.portrait",
            {infoPanel.left + canvas.value(12), infoPanel.top + canvas.value(42),
             infoPanel.left + canvas.value(88), infoPanel.top + canvas.value(118)},
            model.portraitIcon);
    }
    const std::size_t visibleCards = std::min<std::size_t>(model.cards.size(), 8);
    for (std::size_t i = 0; i < visibleCards; ++i) {
        const float left = infoPanel.left + canvas.value(12 + i * 54.0F);
        const float iconTop = infoPanel.top + canvas.value(44);
        const float iconBottom = infoPanel.top + canvas.value(78);
        document.entityCard("entity.card." + std::to_string(i),
            {left, iconTop, left + canvas.value(44), iconBottom}, model.cards[i].icon);
        const std::size_t visibleBars = std::min<std::size_t>(model.cards[i].bars.size(), 2);
        for (std::size_t barIndex = 0; barIndex < visibleBars; ++barIndex) {
            const HudBarModel& bar = model.cards[i].bars[barIndex];
            const float barTop = infoPanel.top + canvas.value(80.0F + barIndex * 5.0F);
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
        const float y = infoPanel.top + canvas.value(52.0F + static_cast<float>(i) * 28.0F);
        auto& element = document.progressBar("entity.bar." + std::to_string(i),
            {infoPanel.left + canvas.value(102), y,
             infoPanel.right - canvas.value(122), y + canvas.value(9)},
            bar.maximum > 0 ? bar.current / bar.maximum : 0,
            bar.kind == HudBarKind::health ? glm::vec3{0.24F, 0.78F, 0.30F}
                                           : glm::vec3{0.20F, 0.68F, 0.95F});
        element.color = {0.10F, 0.12F, 0.13F};
        document.label("entity.bar.label." + std::to_string(i),
            {infoPanel.right - canvas.value(112), y - canvas.value(8),
             infoPanel.right - canvas.value(8), y + canvas.value(13)},
            bar.label + " " + std::to_string(static_cast<int>(bar.current)) + " / " +
                std::to_string(static_cast<int>(bar.maximum)), 1.0F * canvas.scale());
    }
    const std::size_t statCount = std::min<std::size_t>(model.stats.size(), 8);
    for (std::size_t i = 0; i < statCount; ++i) {
        const std::size_t column = i / 4;
        const std::size_t row = i % 4;
        const float columnWidth = (infoPanel.right - infoPanel.left - canvas.value(24)) * 0.5F;
        const float left = infoPanel.left + canvas.value(12) +
                           static_cast<float>(column) * columnWidth;
        const float y = infoPanel.top + canvas.value(128.0F + row * 20.0F);
        document.label("entity.stat." + std::to_string(i),
            {left, y, left + columnWidth - canvas.value(6),
             y + canvas.value(18)}, model.stats[i].label + "  " + model.stats[i].value,
            1.0F * canvas.scale(), {0.75F, 0.84F, 0.88F});
    }
    const std::size_t processorRows = std::min<std::size_t>(model.processorInputs.size(), 4);
    for (std::size_t i = 0; i < processorRows; ++i) {
        const auto& row = model.processorInputs[i];
        const float y = infoPanel.top + canvas.value(212.0F + i * 20.0F);
        const std::string capacity = row.capacity > 0.0F
            ? decimal(row.buffered) + "/" + decimal(row.capacity)
            : decimal(row.buffered);
        document.label("entity.processor." + std::to_string(i),
            {infoPanel.left + canvas.value(12), y,
             infoPanel.right - canvas.value(12), y + canvas.value(18)},
            row.inputName + " " + capacity + "  ->  " + row.outputName + " " +
                decimal(row.expectedOutput) + "  (x" + decimal(row.outputPerInput) + ")",
            1.0F * canvas.scale(), {0.78F, 0.90F, 0.94F});
    }
    document.label("entity.footer",
        {infoPanel.left + canvas.value(102), infoPanel.top + canvas.value(102),
         infoPanel.right - canvas.value(12), infoPanel.top + canvas.value(122)},
        model.footer, 1.05F * canvas.scale(), {0.82F, 0.88F, 0.90F});
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
    return actions(model, width, height, uiScale);
}

} // namespace strategy

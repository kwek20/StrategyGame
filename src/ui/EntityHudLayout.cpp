#include "ui/EntityHudLayout.hpp"
#include "ui/UiLayout.hpp"
#include "ui/UiTheme.hpp"
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

std::string tooltip(const HudActionModel& action) {
    std::string result;
    const auto append = [&](std::string_view style, const std::string& value) {
        if (value.empty()) return;
        if (!result.empty()) result += "\n";
        result += "[[" + std::string(style) + "]]" + value;
    };
    if (!action.name.empty()) result = action.name;
    if (!action.cost.empty() && action.cost != Text::get("entity_hud.free"))
        append("cost", Text::format("entity_hud.tooltip_cost", {action.cost}));
    append("power", action.power);
    append("body", action.description);
    append("requirements", action.requirements);
    if (!action.enabled) append("missing", action.disabledReason);
    return result;
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
    const std::size_t actionCount = model.actions.size();
    const UiRect panel = canvas.rect(UiAnchor::bottomLeft, 0, 0, 800, 300, 560, 260);
    const float inset = canvas.value(UiTheme::spaceSm);
    // Reserve enough width for two readable information columns while keeping a four-column
    // command grid on the left.
    const float actionWidth = canvas.value(400);
    const UiRect actionPanel{panel.left + inset, panel.top + inset,
                             panel.left + inset + actionWidth, panel.bottom - inset};
    const UiRect infoPanel{actionPanel.right + inset, panel.top + inset,
                           panel.right - inset, panel.bottom - inset};
    document.panel("entity.panel", panel, {1.0F, 1.0F, 1.0F}).texture = "ui/hud_panel_lifted";
    document.panel("entity.actions.panel", actionPanel, {1.0F, 1.0F, 1.0F})
        .texture = "ui/hud_compact_lifted";
    document.panel("entity.info.panel", infoPanel, {1.0F, 1.0F, 1.0F})
        .texture = "ui/hud_compact_lifted";

    // Left: all commands, production choices and upgrades share one predictable grid.
    constexpr std::size_t actionColumns = 4;
    const float actionPadding = canvas.value(UiTheme::spaceLg);
    const float actionGap = canvas.value(UiTheme::menuControlGap);
    const float availableGridWidth = actionPanel.right - actionPanel.left - actionPadding * 2.0F;
    const float actionCellWidth = std::min(
        canvas.value(64),
        (availableGridWidth - actionGap * static_cast<float>(actionColumns - 1)) /
            static_cast<float>(actionColumns));
    const std::size_t actionRows = std::max<std::size_t>(1, (actionCount + actionColumns - 1) / actionColumns);
    const float actionCellHeight = std::min(actionCellWidth,
        (actionPanel.bottom - actionPanel.top - actionPadding * 2.0F -
         actionGap * static_cast<float>(actionRows - 1)) / static_cast<float>(actionRows));
    const float actionGridHeight = actionCellHeight * static_cast<float>(actionRows) +
                                   actionGap * static_cast<float>(actionRows - 1);
    const float actionGridTop = actionPanel.top +
        ((actionPanel.bottom - actionPanel.top) - actionGridHeight) * 0.5F;
    for (std::size_t i = 0; i < actionCount; ++i) {
        const std::size_t column = i % actionColumns;
        const std::size_t row = i / actionColumns;
        const std::size_t rowStart = row * actionColumns;
        const std::size_t itemsInRow = std::min(actionColumns, actionCount - rowStart);
        const float rowWidth = actionCellWidth * static_cast<float>(itemsInRow) +
                               actionGap * static_cast<float>(itemsInRow - 1);
        const float rowLeft = actionPanel.left +
            ((actionPanel.right - actionPanel.left) - rowWidth) * 0.5F;
        const float left = rowLeft +
                           static_cast<float>(column) * (actionCellWidth + actionGap);
        const float top = actionGridTop +
                          static_cast<float>(row) * (actionCellHeight + actionGap);
        const HudActionModel& action = model.actions[i];
        auto& button = document.iconButton(actionElementId(action.id),
            {left, top, left + actionCellWidth, top + actionCellHeight}, action.icon,
            tooltip(action), action.enabled);
        button.active = action.active;
        button.texture = "ui/action_slot_transparent";
        button.color = {1.0F, 1.0F, 1.0F};
        button.hoverColor = {0.62F, 0.94F, 1.0F};
        button.tooltipIcons = action.costIcons;
    }

    auto& tooltipBox = document.tooltip("entity.tooltip",
        {panel.left + canvas.value(16), canvas.value(16),
         std::min(static_cast<float>(width) - canvas.value(16),
                  panel.left + canvas.value(376)),
         panel.top - canvas.value(16)}, {});
    tooltipBox.textScale = 1.45F * canvas.scale();
    tooltipBox.texture = "ui/hud_compact_lifted";

    // Queues are a separate strip above the entity menu and never resize its contents.
    if (!model.queue.empty()) {
        const UiRect queuePanel{panel.left, panel.top - canvas.value(58), panel.right, panel.top};
        document.panel("entity.queue.panel", queuePanel, {0.82F, 0.84F, 0.86F})
            .texture = "ui/hud_panel_lifted";
        const std::size_t queueCount = std::min<std::size_t>(model.queue.size(), 9);
        const float queueSlotSize = canvas.value(44);
        const float queueGap = canvas.value(8);
        const float queueTop = queuePanel.top + canvas.value(7);
        const float queueLeft = queuePanel.left + canvas.value(8);
        for (std::size_t i = 0; i < queueCount; ++i) {
            const float left = queueLeft + static_cast<float>(i) * (queueSlotSize + queueGap);
            auto& slot = document.queueSlot(queueElementId(i),
                               {left, queueTop, left + queueSlotSize,
                                queueTop + queueSlotSize}, model.queue[i].icon,
                               model.queue[i].name + " - " + Text::get("entity_hud.cancel"),
                               model.queue[i].cancellable);
            slot.texture = "ui/action_slot_transparent";
            slot.color = {1.0F, 1.0F, 1.0F};
            slot.hoverColor = {0.62F, 0.94F, 1.0F};
        }
        auto& queueProgress = document.progressBar("entity.queue.progress",
            {queuePanel.left + canvas.value(8), queuePanel.bottom - canvas.value(9),
             queuePanel.right - canvas.value(8), queuePanel.bottom - canvas.value(4)},
            model.queue.front().progress);
        queueProgress.color = {1.0F, 1.0F, 1.0F};
        queueProgress.texture = "ui/entity_progress_track";
        queueProgress.progressTexture = "ui/production_progress_fill";
    }

    // Right: identity and component-derived information, never interactive actions.
    document.label("entity.title", {infoPanel.left + canvas.value(28),
                   infoPanel.top + canvas.value(37), infoPanel.right - canvas.value(24),
                   infoPanel.top + canvas.value(57)},
                   model.title, 1.55F * canvas.scale());
    // A multi-entity selection is represented by its compact entity cards. Drawing the
    // single-entity portrait as well duplicates the first unit and overlaps that row.
    if (model.cards.empty()) {
        auto& portrait = document.entityCard("entity.portrait",
            {infoPanel.left + canvas.value(22), infoPanel.top + canvas.value(65),
             infoPanel.left + canvas.value(90), infoPanel.top + canvas.value(133)},
            model.portraitIcon);
        portrait.texture = "ui/action_slot_transparent";
        portrait.color = {1.0F, 1.0F, 1.0F};
    }
    const std::size_t cardCapacity = std::max<std::size_t>(1,
        static_cast<std::size_t>((infoPanel.right - infoPanel.left - canvas.value(24)) /
                                 canvas.value(54)));
    const std::size_t visibleCards = std::min(model.cards.size(), cardCapacity);
    for (std::size_t i = 0; i < visibleCards; ++i) {
        const float left = infoPanel.left + canvas.value(12 + i * 54.0F);
        const float iconTop = infoPanel.top + canvas.value(44);
        const float iconBottom = infoPanel.top + canvas.value(78);
        auto& card = document.entityCard("entity.card." + std::to_string(i),
            {left, iconTop, left + canvas.value(44), iconBottom}, model.cards[i].icon);
        card.texture = "ui/action_slot_transparent";
        card.color = {1.0F, 1.0F, 1.0F};
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
            if (bar.kind == HudBarKind::health) {
                element.color = {1.0F, 1.0F, 1.0F};
                element.texture = "ui/entity_progress_track";
                element.progressTexture = "ui/health_progress_fill";
            }
        }
    }
    for (std::size_t i = 0; i < model.bars.size(); ++i) {
        const auto& bar = model.bars[i];
        const float y = infoPanel.top + canvas.value(70.0F + static_cast<float>(i) * 29.0F);
        const float barHeight = canvas.value(18.0F);
        auto& element = document.progressBar("entity.bar." + std::to_string(i),
            {infoPanel.left + canvas.value(106), y,
             infoPanel.right - canvas.value(18), y + barHeight},
            bar.maximum > 0 ? bar.current / bar.maximum : 0,
            bar.kind == HudBarKind::health ? glm::vec3{0.92F, 0.24F, 0.22F}
                : (bar.kind == HudBarKind::construction ? glm::vec3{1.0F, 0.58F, 0.16F}
                                                        : glm::vec3{0.20F, 0.68F, 0.95F}));
        element.color = {1.0F, 1.0F, 1.0F};
        element.texture = "ui/entity_progress_track";
        element.progressTexture = bar.kind == HudBarKind::health
            ? "ui/health_progress_fill"
            : (bar.kind == HudBarKind::construction ? "ui/production_progress_fill"
                                                    : "ui/entity_progress_fill");
        element.icon = bar.kind == HudBarKind::health ? "status_damaged"
            : (bar.kind == HudBarKind::construction ? "status_constructing" : "resource_power");
        document.label("entity.bar.label." + std::to_string(i),
            {infoPanel.left + canvas.value(118), y + canvas.value(2),
             infoPanel.right - canvas.value(24), y + barHeight},
            bar.label + " " + std::to_string(static_cast<int>(bar.current)) + " / " +
                std::to_string(static_cast<int>(bar.maximum)), 0.95F * canvas.scale());
    }
    const std::size_t statCount = std::min<std::size_t>(model.stats.size(), 8);
    const float statColumnGap = canvas.value(16);
    const float statSideInset = canvas.value(30);
    const float statColumnWidth =
        (infoPanel.right - infoPanel.left - statSideInset * 2.0F - statColumnGap) * 0.5F;
    for (std::size_t i = 0; i < statCount; ++i) {
        const std::size_t column = i / 4;
        const std::size_t row = i % 4;
        const float left = infoPanel.left + statSideInset +
                           static_cast<float>(column) * (statColumnWidth + statColumnGap);
        const float y = infoPanel.top + canvas.value(146.0F + row * 22.0F);
        const float valueLeft = left + canvas.value(64);
        document.label("entity.stat.label." + std::to_string(i),
            {left, y, valueLeft - canvas.value(4), y + canvas.value(18)},
            model.stats[i].label, 0.86F * canvas.scale(), {0.48F, 0.72F, 0.82F});
        document.label("entity.stat." + std::to_string(i),
            {valueLeft, y, left + statColumnWidth, y + canvas.value(18)},
            model.stats[i].value, 0.94F * canvas.scale(), {0.84F, 0.91F, 0.94F});
        if (!model.stats[i].icon.empty()) {
            auto& statIcon = document.label("entity.stat.icon." + std::to_string(i),
                {left - canvas.value(22), y - canvas.value(1),
                 left - canvas.value(2), y + canvas.value(19)},
                {});
            statIcon.icon = model.stats[i].icon;
        }
    }
    const std::size_t processorRows = std::min<std::size_t>(model.processorInputs.size(), 3);
    for (std::size_t i = 0; i < processorRows; ++i) {
        const auto& row = model.processorInputs[i];
        const float y = infoPanel.top + canvas.value(232.0F + i * 15.0F);
        const std::string capacity = row.capacity > 0.0F
            ? decimal(row.buffered) + "/" + decimal(row.capacity)
            : decimal(row.buffered);
        document.label("entity.processor." + std::to_string(i),
            {infoPanel.left + canvas.value(18), y,
             infoPanel.right - canvas.value(18), y + canvas.value(13)},
            row.inputName + " " + capacity + "  ->  " + row.outputName + " " +
                decimal(row.expectedOutput) + "  (x" + decimal(row.outputPerInput) + ")",
            0.9F * canvas.scale(),
            row.buffered > 0.0F || row.expectedOutput > 0.0F
                ? glm::vec3{0.78F, 0.90F, 0.94F}
                : glm::vec3{0.48F, 0.57F, 0.61F});
        if (row.capacity > 0.0F) {
            auto& progress = document.progressBar("entity.processor.progress." + std::to_string(i),
                {infoPanel.left + canvas.value(18), y + canvas.value(12),
                 infoPanel.right - canvas.value(18), y + canvas.value(16)},
                row.buffered / row.capacity);
            progress.color = {1.0F, 1.0F, 1.0F};
            progress.texture = "ui/entity_progress_track";
            progress.progressTexture = "ui/processing_progress_fill";
        }
    }
    document.label("entity.footer",
        {infoPanel.left + canvas.value(106), infoPanel.top + canvas.value(124),
         infoPanel.right - canvas.value(18), infoPanel.top + canvas.value(140)},
        model.footer, 1.05F * canvas.scale(), {0.82F, 0.88F, 0.90F});
    return document;
}

UiDocument EntityHudLayout::selection(const EntityHudModel& model, int width, int height,
                                      float uiScale) {
    UiDocument document;
    const UiLayout canvas(width, height, uiScale);
    const std::size_t visible = std::min<std::size_t>(model.selectionGroups.size(), 6);
    const float panelHeight = 58.0F + static_cast<float>(visible) * 34.0F;
    const UiRect panel = canvas.rect(UiAnchor::bottomLeft, 0, 0, 392, panelHeight, 300, 100);
    document.panel("selection.panel", panel, {0.82F, 0.84F, 0.86F})
        .texture = "ui/hud_compact_lifted";
    document.label("selection.title", {panel.left + canvas.value(12),
                   panel.top + canvas.value(8), 0, 0},
                   Text::format("selection.title", {std::to_string(model.totalEntities)}),
                   1.8F * canvas.scale());
    for (std::size_t i = 0; i < visible; ++i) {
        const auto& group = model.selectionGroups[i];
        auto& card = document.entityCard(selectionElementId(group.archetype),
            {panel.left + canvas.value(12), panel.top + canvas.value(36 + static_cast<float>(i) * 34),
             panel.right - canvas.value(12), panel.top + canvas.value(66 + static_cast<float>(i) * 34)},
            group.icon, group.name);
        card.texture = "ui/action_slot_transparent";
        card.color = {1.0F, 1.0F, 1.0F};
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

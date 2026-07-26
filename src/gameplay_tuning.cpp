#include <algorithm>
#include <cstddef>

#include "arena_level.h"
#include "gameplay_internal.h"
#include "text_renderer.h"

namespace game {

void EnterPostBossTuningRoom() {
    if (!RunMode()) return;
    debugRoom = false;
    postBossTuningRoom = true;
    run.mapActive = false;
    currentMap = "interior";

    projectiles.clear();
    bombs.clear();
    explosions.clear();
    enemyRails.clear();
    enemies.clear();
    spawners.clear();
    shieldBlocks.clear();
    textBoxes.clear();
    shooting = false;
    shotCooldown = 0;
    lastPlayerRoom = -1;
    run.boss = BossFightState{};

    constexpr std::size_t columns = 3;
    constexpr float columnWidth = 780.0f;
    constexpr float rowHeight = 30.0f;
    constexpr float horizontalMargin = 70.0f;
    constexpr float verticalMargin = 260.0f;
    constexpr float exitBuffer = 300.0f;
    const std::size_t rows =
        (worldConstants.size() + columns - 1) / columns;
    const float width = horizontalMargin * 2.0f +
        columnWidth * static_cast<float>(columns);
    const float height = std::max(
        kRunArenaHeight,
        verticalMargin + rowHeight * static_cast<float>(rows) + exitBuffer);
    runArena = BuildArenaLevel(
        {0, 0, width, height},
        0x54554e494e47524fULL, {}, 0, kWall);

    constexpr float left = 70.0f;
    constexpr float top = 105.0f;
    for (std::size_t index = 0; index < worldConstants.size(); ++index) {
        const WorldConstant& entry = worldConstants[index];
        if (!entry.numeric || entry.value.empty()) continue;
        const std::size_t column = rows > 0 ? index / rows : 0;
        const std::size_t row = rows > 0 ? index % rows : 0;
        TextBox box;
        box.rect = {
            left + static_cast<float>(column) * columnWidth +
                static_cast<float>(text_renderer::MeasureWidth(
                    entry.label.size() + 2)),
            top + static_cast<float>(row) * rowHeight,
            static_cast<float>(
                text_renderer::MeasureWidth(entry.value.size())),
            static_cast<float>(text_renderer::kGlyphHeight)};
        box.worldConstant = static_cast<int>(index);
        textBoxes.push_back(box);
    }

    const Rect exit = PhysicalExitPortalRect();
    playerX = CenterX(exit) - kPlayerSize * 0.5f;
    playerY = exit.y - 90.0f;
}

}  // namespace game

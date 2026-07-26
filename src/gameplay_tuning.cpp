#include <algorithm>
#include <cstddef>

#include "arena_level.h"
#include "gameplay_internal.h"

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
    const std::size_t rows =
        (worldConstants.size() + columns - 1) / columns;
    const float width = horizontalMargin * 2.0f +
        columnWidth * static_cast<float>(columns);
    const float height = std::max(
        kRunArenaHeight,
        verticalMargin + rowHeight * static_cast<float>(rows));
    runArena = BuildArenaLevel(
        {0, 0, width, height},
        0x54554e494e47524fULL, {}, 0, kWall);

    const Rect exit = PhysicalExitPortalRect();
    playerX = CenterX(exit) - kPlayerSize * 0.5f;
    playerY = exit.y - 90.0f;
}

}  // namespace game

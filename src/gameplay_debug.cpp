#include "arena_level.h"
#include "audio.h"
#include "gameplay_internal.h"

namespace game {

Rect DebugUpgradeTarget(std::size_t index) {
    return {80.0f + static_cast<float>(index % 3) * 340.0f,
            140.0f + static_cast<float>(index / 3) * 150.0f,
            260, 75};
}

Rect DebugWeaponTarget(std::size_t index) {
    return {80.0f + static_cast<float>(index % 3) * 340.0f,
            510.0f + static_cast<float>(index / 3) * 100.0f, 260, 75};
}

Rect DebugSpawnerTarget(std::size_t index) {
    return {80.0f + static_cast<float>(index) * 340.0f,
            780.0f, 260, 70};
}

bool DebugSpawnerEnabled(std::size_t index) {
    return index < debugSpawnerOn.size() && debugSpawnerOn[index];
}

float DebugPickupRespawn(std::size_t index) {
    return index < debugPickupTimers.size() ? debugPickupTimers[index] : 0;
}

void EnterDebugRoom() {
    if (!RunMode()) return;
    debugRoom = true;
    run.mapActive = false;
    currentMap = "interior";
    projectiles.clear();
    bombs.clear();
    explosions.clear();
    enemyRails.clear();
    enemies.clear();
    shieldBlocks.clear();
    spawners.clear();
    debugPickupTimers.fill(0);
    debugSpawnerOn.fill(false);
    const float waveformWidth = static_cast<float>(
        (AudioSampleCount(Sound::LaserShoot) + 1) / 2);
    runArena = BuildArenaLevel(
        {0, 0, std::max(kRunArenaWidth, waveformWidth + 240.0f),
         kRunArenaHeight},
        0x4445425547524f4fULL, {}, 12, kWall, false, true);
    for (std::size_t index = 0; index < kDebugEnemyTypes.size(); ++index) {
        Spawner spawner;
        spawner.room = 0;
        spawner.enemyType = kDebugEnemyTypes[index];
        spawner.x = 180.0f + static_cast<float>(index) * 360.0f;
        spawner.y = 920.0f;
        spawner.health = 1000000;
        ResetSpawnerTimer(spawner, 0.25f);
        spawner.id = nextSpawnerId++;
        spawners.push_back(spawner);
    }
    playerX = kRunArenaWidth * 0.5f - kPlayerSize * 0.5f;
    // Start above the south-wall waveform instead of inside its pixels.
    playerY = kRunArenaHeight - 430.0f;
    lastPlayerRoom = -1;
    textBoxes.clear();
    AppendPortalTextBoxes();
}

}  // namespace game

#include "arena_level.h"
#include "audio.h"
#include "gameplay_internal.h"

namespace game {

namespace {
int debugInteraction = -1;
int completedDebugInteraction = -1;
float debugInteractionTimer = 0;

int DebugInteractionAt(const Rect& player) {
    for (std::size_t index = 0; index < kDebugUpgrades.size(); ++index)
        if (Overlaps(player, DebugUpgradeTarget(index)))
            return static_cast<int>(index);
    for (std::size_t index = 0;
         index < playerInteriorState.permanent.size(); ++index)
        if (Overlaps(player, DebugLimiterTarget(index)))
            return 100 + static_cast<int>(index);
    for (std::size_t index = 0; index < 6; ++index)
        if (Overlaps(player, DebugWeaponTarget(index)))
            return 200 + static_cast<int>(index);
    for (std::size_t index = 0; index < kDebugEnemyTypes.size(); ++index)
        if (Overlaps(player, DebugSpawnerTarget(index)))
            return 300 + static_cast<int>(index);
    return -1;
}
}  // namespace

Rect DebugUpgradeTarget(std::size_t index) {
    return {80.0f + static_cast<float>(index % 3) * 340.0f,
            140.0f + static_cast<float>(index / 3) * 150.0f,
            260, 75};
}

Rect DebugLimiterTarget(std::size_t index) {
    return {1140.0f + static_cast<float>(index % 2) * 340.0f,
            140.0f + static_cast<float>(index / 2) * 150.0f,
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

void UpdateDebugInteractions(float dt) {
    if (!debugRoom) return;
    const int action = DebugInteractionAt(
        {playerX, playerY, kPlayerSize, kPlayerSize});
    if (action != debugInteraction) {
        debugInteraction = action;
        debugInteractionTimer = 0;
        completedDebugInteraction = -1;
    }
    if (action < 0 || action == completedDebugInteraction) return;
    if (debugInteractionTimer <= 0) PlaySoundEffect(Sound::AimTick);
    debugInteractionTimer += dt;
    if (debugInteractionTimer < rogueliteTuning.shopPurchaseSeconds) return;
    debugInteractionTimer = 0;
    if (action < 100) {
        const UpgradeType upgrade = kDebugUpgrades[action];
        AddUpgradeStep(upgrade);
        if (upgrade == UpgradeType::MaxHealth) ++playerHealth;
    } else if (action < 200) {
        const int index = action - 100;
        playerInteriorState.permanent[index] =
            !playerInteriorState.permanent[index];
        completedDebugInteraction = action;
    } else if (action < 300) {
        const int index = action - 200;
        if (index < 3) {
            const PrimaryWeapon weapon = static_cast<PrimaryWeapon>(index);
            if (playerInteriorState.permanent[4])
                run.primaryWeapons[static_cast<int>(weapon)] = true;
            else
                run.primaryWeapon = weapon;
        } else {
            const SecondaryWeapon weapon = index == 3 ? SecondaryWeapon::Bomb :
                index == 4 ? SecondaryWeapon::HomingRocket :
                SecondaryWeapon::ContactBomb;
            if (playerInteriorState.permanent[5])
                run.secondaryWeapons[static_cast<int>(weapon)] = true;
            else
                run.secondaryWeapon = weapon;
        }
        completedDebugInteraction = action;
    } else {
        const int index = action - 300;
        debugSpawnerOn[index] = !debugSpawnerOn[index];
        ResetSpawnerTimer(spawners[index], 0.25f);
        completedDebugInteraction = action;
    }
    PlaySoundEffect(Sound::HitEnemy);
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
    debugInteraction = -1;
    completedDebugInteraction = -1;
    debugInteractionTimer = 0;
    const float waveformWidth = static_cast<float>(
        (AudioSampleCount(Sound::LaserShoot) + 1) / 2);
    runArena = BuildArenaLevel(
        {0, 0, std::max(kRunArenaWidth + 500.0f, waveformWidth + 240.0f),
         kRunArenaHeight + 300.0f},
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
    playerX = runArena.bounds.width * 0.5f - kPlayerSize * 0.5f;
    // Start above the south-wall waveform instead of inside its pixels.
    playerY = runArena.bounds.height - 430.0f;
    lastPlayerRoom = -1;
    textBoxes.clear();
    AppendPortalTextBoxes();
}

}  // namespace game

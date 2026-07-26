#include <algorithm>

#include "gameplay_internal.h"
#include "roguelite.h"
#include "world.h"

namespace game {

namespace {

PlayerAlteration AlterationForRoom(const RunNode& node, int room) {
    (void)node;
    static constexpr std::array<PlayerAlteration, 9> layout{{
        PlayerAlteration::SecondMultishot, PlayerAlteration::ExtraProjectileLimiter,
        PlayerAlteration::DualPrimary, PlayerAlteration::InfiniteHoming,
        PlayerAlteration::Regeneration, PlayerAlteration::InfiniteMultishot,
        PlayerAlteration::DualSecondary, PlayerAlteration::ExtraLife,
        PlayerAlteration::InfiniteAutoRocket}};
    return room >= 0 && room < static_cast<int>(layout.size())
        ? layout[room] : PlayerAlteration::Count;
}

Rect PlayerAlterationNumberTarget(int room) {
    const Rect target = PlayerAlterationTarget(room);
    return {
        CenterX(target) - 35.0f, CenterY(target),
        70.0f, target.height * 0.5f};
}

void SealPlayerInteriorRoom(int room) {
    const Room& value = rooms[room];
    const float x = RoomX(value), y = RoomY(value);
    const float edge = (interior.roomSize - kExitWidth) * 0.5f;
    const Rect seals[] = {
        {x + edge, y, kExitWidth, kWall},
        {x + edge, y + interior.roomSize - kWall, kExitWidth, kWall},
        {x, y + edge, kWall, kExitWidth},
        {x + interior.roomSize - kWall, y + edge, kWall, kExitWidth}};
    for (const Rect& seal : seals) shieldBlocks.push_back({seal, -1, 1000000});
}

void StartPlayerInteriorWave(RunNode& node, int room) {
    node.playerInteriorWave = true;
    node.playerInteriorRoom = room;
    enemies.clear();
    spawners.clear();
    SealPlayerInteriorRoom(room);
    static constexpr const char* enemyTypes[]{
        "circle", "triangle", "charger", "shooter"};
    const Room& value = rooms[room];
    for (std::uint32_t index = 0;
         index < rogueliteTuning.playerInteriorWaveSpawners; ++index) {
        const std::uint64_t seed = DeriveRunSeed(
            node.seed, 0x414c544552574156ULL, index);
        Spawner spawner;
        spawner.room = room;
        spawner.enemyType = enemyTypes[seed % 4];
        spawner.x = RoomX(value) + 120.0f +
            static_cast<float>((seed >> 8) %
                static_cast<std::uint64_t>(interior.roomSize - 270));
        spawner.y = RoomY(value) + 120.0f +
            static_cast<float>((seed >> 24) %
                static_cast<std::uint64_t>(interior.roomSize - 270));
        spawner.health = 8;
        ResetSpawnerTimer(
            spawner, 0.2f +
                static_cast<float>((seed >> 40) % 30) / 100.0f);
        spawner.id = nextSpawnerId++;
        spawners.push_back(spawner);
    }
}

}  // namespace

bool HitPlayerAlteration(const Rect& shot) {
    RunNode* node = CurrentRunNode();
    if (!node || node->type != RunNodeType::PlayerInterior ||
        node->playerInteriorAlteration >= 0 || node->playerInteriorWave)
        return false;
    for (int room = 0; room < static_cast<int>(rooms.size()); ++room) {
        if (!Overlaps(shot, PlayerAlterationNumberTarget(room))) continue;
        const PlayerAlteration alteration = AlterationForRoom(*node, room);
        const int value = static_cast<int>(alteration);
        if (!playerInteriorState.permanent[0] &&
            alteration != PlayerAlteration::Regeneration)
            return true;
        if (alteration == PlayerAlteration::Count ||
            playerInteriorState.permanent[value - 3])
            return true;
        playerInteriorState.permanent[value - 3] = true;
        if (value < static_cast<int>(playerInteriorState.values.size()))
            playerInteriorState.values[value] = std::max(
                0, playerInteriorState.values[value] - 1);
        if (alteration == PlayerAlteration::Regeneration)
            playerHealth = std::min(
                EffectivePlayerMaxHealth(), playerHealth + 1);
        node->playerInteriorAlteration = value;
        node->playerInteriorAlterationRoom = room;
        node->playerInteriorAlterationTimer =
            kPlayerAlterationDisplaySeconds;
        SaveMutations();
        return true;
    }
    return false;
}

void UpdatePlayerInteriorAlteration(float dt) {
    RunNode* node = CurrentRunNode();
    if (!node || node->type != RunNodeType::PlayerInterior ||
        node->playerInteriorWave ||
        node->playerInteriorAlterationRoom < 0 ||
        node->playerInteriorAlterationTimer <= 0)
        return;
    node->playerInteriorAlterationTimer = std::max(
        0.0f, node->playerInteriorAlterationTimer - dt);
    if (node->playerInteriorAlterationTimer <= 0) {
        const int room = node->playerInteriorAlterationRoom;
        node->playerInteriorAlterationRoom = -1;
        StartPlayerInteriorWave(*node, room);
    }
}

Rect PlayerAlterationTarget(int room) {
    if (room < 0 || room >= static_cast<int>(rooms.size())) return {};
    return {RoomX(rooms[room]) + interior.roomSize * 0.5f - 75.0f,
            RoomY(rooms[room]) + interior.roomSize * 0.5f - 45.0f,
            150.0f, 90.0f};
}

const char* PlayerAlterationName(PlayerAlteration alteration) {
    static constexpr const char* names[]{
        "", "", "",
        "REGENERATION LIMITER", "MULTISHOT LIMITER", "HOMING LIMITER",
        "AUTO ROCKET LIMITER", "PRIMARY LIMITER", "SECONDARY LIMITER",
        "EXTRA PROJECTILE LIMITER", "MULTISHOT LIMITER II", "EXTRA LIFE"};
    const int index = static_cast<int>(alteration);
    return index >= 0 && index < 12 ? names[index] : "ALTERATION";
}

int PlayerAlterationValue(PlayerAlteration alteration) {
    const int index = static_cast<int>(alteration);
    return index >= 0 && index < static_cast<int>(playerInteriorState.values.size())
        ? playerInteriorState.values[index] : 0;
}

}  // namespace game

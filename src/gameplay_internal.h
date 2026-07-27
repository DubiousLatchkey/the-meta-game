#pragma once

// Shared declarations for gameplay_*.cpp translation units only.
// Nothing outside the gameplay split should include this header.

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "arena_level.h"
#include "gameplay.h"
#include "state.h"

namespace game {

inline constexpr std::array<const char*, 4> kDebugEnemyTypes{{
    "circle", "triangle", "charger", "shooter"}};
inline constexpr std::array<UpgradeType, 8> kDebugUpgrades{{
    UpgradeType::MaxHealth, UpgradeType::MoveSpeed,
    UpgradeType::FireRate, UpgradeType::ProjectileDamage,
    UpgradeType::BombCooldown, UpgradeType::BombDamage,
    UpgradeType::Invincibility, UpgradeType::ExtraProjectile}};
// Frame/run-scoped state shared across the gameplay split.
// Defined in gameplay.cpp.
extern bool pendingLevelSelection;
extern ArenaLevel runArena;
extern std::string legacyInteriorArchetype;
extern int legacyInteriorRoomSize;
extern bool debugRoom;
extern bool postBossTuningRoom;
extern std::array<bool, 4> debugSpawnerOn;
extern std::array<float, 3> debugPickupTimers;

// Mode predicates (gameplay.cpp).
bool RunMode();
bool RunArenaMode();
bool PlayerInteriorMode();
bool RunWall(const Rect& rectangle);

// Upgrade / difficulty bookkeeping (gameplay.cpp).
std::uint32_t UpgradeRank(UpgradeType type);
int EffectivePlayerMaxHealth();
void AddUpgradeStep(UpgradeType type);
float EffectiveShotInterval();
float EffectiveBombCooldown();
float EffectiveInvincibilityDuration();
WeaponStats ResolvePlayerWeapon(const std::string& id);
std::uint32_t EnemyStage(
    const std::string& archetype, EnemyDifficultyStat stat);
std::uint32_t& MutableEnemyStage(
    const std::string& archetype, EnemyDifficultyStat stat);
bool OrganDifficultyStat(const std::string& id, EnemyDifficultyStat& stat);
int BaseStatValue(const EnemyType& type, EnemyDifficultyStat stat);
int DisplayEnemyOrganValue(
    const EnemyType& type, EnemyDifficultyStat stat,
    std::uint32_t stage);
void ApplyArenaDownside(RunNode& node);

// Text/portal/arena rebuild helpers (gameplay.cpp).
void RebuildRunArena(RunNode& node);
void RebuildGameplayTextBoxes();
void AppendPortalTextBoxes();
void UnlockInteriorPortals();
void BuildArenaChoicePortals(RunNode& node);
void BuildPostBossPortals(RunNode& node);
void BuildInteriorArenaDestinations(RunNode& node);
bool HitText(Projectile& projectile);
void TriggerMainMenuStart();
void UpdateMainMenu(float dt);
void LeaveMainMenu();

// Hit-resolution family (gameplay.cpp) — needed by ConfigureNode's
// wave setup and by the boss/player-interior/levels files.
bool HitShield(const Rect& shot, int damage);
bool DamagePlayer(int damage);
bool HitSpawner(const Rect& shot, int damage);
bool HitEnemy(const Rect& shot, int damage);
bool HitRoomWall(const Rect& shot);
bool HitShopOffer(const Rect& shot);
int SpawnerReward(const std::string& type);
void DetonateBomb(float x, float y, int damage, float radius);
bool BombObstacle(const Rect& bomb);
void SpawnBurst(Spawner& spawner);
void AwardSpawnerDeaths();
void HomeProjectile(Projectile& projectile);
void HomeBomb(Bomb& bomb);
std::string ArenaArchetype(std::uint64_t seed);
void CreateWaveSpawners(RunNode& node, std::uint32_t wave);
void SyncInteriorStats();
void ResetEnemyDifficultyProgress();
Rect PhysicalExitPortalRect();
void ConfigureNode(RunNode& node);
void EnterRunMap();
void UpdateShopPurchases(float dt);
void UpdateDebugInteractions(float dt);
void UpdatePlayerWeapons(float dt);

// Boss (gameplay_boss.cpp).
void BuildBossTurrets(
    std::uint64_t seed, bool includeCleared, BossQuadrant onlyQuadrant,
    bool filterQuadrant);
bool PointInBoss(float x, float y);
bool RectHitsBoss(const Rect& shot);
void UnlockBossInteriorPortals();
void CheckBossInteriorCompletion();
bool HitBossTurretTarget(const Rect& shot, int damage);
bool HitBoss(const Rect& shot, int damage);
void UpdateBossFight(float dt);
void InitializeBossFight(RunNode& node);

// Player interior (gameplay_player_interior.cpp).
bool HitPlayerAlteration(const Rect& shot);
void UpdatePlayerInteriorAlteration(float dt);
bool PlayerInteriorRoomHasAvailableAlteration(int room);

// Debug room support that the core file's hit-resolution still needs
// (gameplay_debug.cpp only reads/writes the shared debug* state above).

// Levels split (gameplay_levels.cpp).
void UpdateAudioMap(float dt);
void UpdateGlyphMap(float dt);

}  // namespace game

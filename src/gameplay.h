#pragma once

#include <string>

#include "arena_level.h"
#include "state.h"

namespace game {

void ShootToward(int mouseX, int mouseY);
void LaunchBomb(int mouseX, int mouseY);
void BeginPrimaryFire(int mouseX, int mouseY);
void ReleasePrimaryFire(int mouseX, int mouseY);
bool FirePlayerRail();
void FireSecondary(int mouseX, int mouseY);
void StartFreshRun(bool enterFirstNode = true);
void EnterMainMenu();
bool MainMenuActive();
bool MainMenuStarting();
bool MainMenuPortalActive();
float MainMenuTitleAlpha();
Rect MainMenuPortalRect();
void EnterRunNode(RunNodeId id);
void EnterDebugRoom();
int EffectivePlayerMaxHealth();
void DebugClearCurrentNode();
bool DebugRoomActive();
const ArenaLevel& ActiveRunArena();
Rect ExitPortalRect();
Rect PortalLabelRect();
Rect ShopOfferTarget(std::size_t index);
Rect ShopOfferPurchaseArea(std::size_t index);
Rect PlayerAlterationTarget(int room);
const char* PlayerAlterationName(PlayerAlteration alteration);
int PlayerAlterationValue(PlayerAlteration alteration);
const char* BossQuadrantName(BossQuadrant quadrant);
Rect BossTurretTargetRect(const BossTurret& turret);
bool BossFightMode();
bool BossInteriorMode();
Rect ResetWordsTarget();
Rect DebugUpgradeTarget(std::size_t index);
Rect DebugLimiterTarget(std::size_t index);
Rect DebugWeaponTarget(std::size_t index);
Rect DebugSpawnerTarget(std::size_t index);
bool DebugSpawnerEnabled(std::size_t index);
float DebugPickupRespawn(std::size_t index);
const char* UpgradeName(UpgradeType type);
const char* PrimaryWeaponName(PrimaryWeapon type);
const char* SecondaryWeaponName(SecondaryWeapon type);
const char* ShopOfferName(const ShopOffer& offer);
float PrimaryChargeProgress();
float SecondaryCooldownDuration();
float UpgradeCurrentValue(UpgradeType type);
Rect EnemyRect(const Enemy& enemy);
int EnemyScale(const Enemy& enemy);
bool EnemyVisualOverlaps(const Enemy& enemy, const Rect& target);
bool EnemyVisualFitsNetwork(const Enemy& enemy, float x, float y);
bool EnemyVisualWithinRadius(const Enemy& enemy, float x, float y, float radius);
void Update(float dt);

}  // namespace game

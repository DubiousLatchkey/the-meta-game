#include <algorithm>
#include <chrono>
#include <cmath>
#include <queue>

#include "arena_level.h"
#include "gameplay_internal.h"
#include "roguelite.h"
#include "text_renderer.h"
#include "world.h"

namespace game {

int SpawnerReward(const std::string& type) {
    if (type == "triangle") return 1;
    if (type == "charger") return 2;
    if (type == "shooter") return 3;
    return 2;
}

std::uint32_t UpgradeRank(UpgradeType type) {
    for (const RunUpgrade& upgrade : run.upgrades)
        if (upgrade.type == type) return upgrade.rank;
    return 0;
}

void AddUpgradeStep(UpgradeType type) {
    for (RunUpgrade& upgrade : run.upgrades)
        if (upgrade.type == type) {
            ++upgrade.rank;
            return;
        }
    run.upgrades.push_back({type, 1});
}

float EffectiveShotInterval() {
    return ResolvePlayerWeapon(
        run.primaryWeapon == PrimaryWeapon::Boomerang
            ? "boomerang" : "standard").cadence;
}

float EffectiveBombCooldown() {
    return ResolvePlayerWeapon("bomb").cadence;
}

float EffectiveInvincibilityDuration() {
    return std::min(
        5.0f, rogueliteTuning.playerInvincibilitySeconds +
            0.1f * UpgradeRank(UpgradeType::Invincibility));
}

WeaponStats ResolvePlayerWeapon(const std::string& id) {
    WeaponStats result;
    const auto found = playerWeapons.find(id);
    if (found != playerWeapons.end()) result = found->second;
    const std::uint32_t fireRanks =
        UpgradeRank(UpgradeType::FireRate);
    result.cadence = std::max(
        0.05f, result.cadence -
            0.0125f * result.cadenceEffectScale * fireRanks);
    if (id == "bomb" || id == "contact_bomb" || id == "homing_rocket")
        result.cadence = std::max(
            0.05f, result.cadence -
                0.2f * result.cadenceEffectScale *
                    UpgradeRank(UpgradeType::BombCooldown));
    result.damage += static_cast<int>(
        UpgradeRank(UpgradeType::ProjectileDamage));
    if (playerInteriorState.repeatableRanks[2] > 0)
        result.projectilesPerShot +=
            std::max(0, 100 - playerInteriorState.values[2]);
    if (run.multishotRemaining > 0)
        result.count += 2;
    if (playerInteriorState.permanent[1])
        result.count += std::max(0, 3 - playerInteriorState.values[4]);
    if (run.homingRemaining > 0 ||
        (playerInteriorState.permanent[2] &&
         playerInteriorState.values[5] == 0))
        result.homing = true;
    if (id == "auto_rocket" && playerInteriorState.permanent[3])
        result.cadence = std::max(
            0.05f, playerInteriorState.values[6] / 1000.0f);
    if (playerInteriorState.repeatableRanks[1] > 0)
        result.cadence /= std::max(
            0.1f, 1.0f + playerFireImprovementPerStep *
                static_cast<float>(
                    playerInteriorDefaults[1] -
                    playerInteriorState.values[1]));
    return result;
}

std::string ArenaArchetype(std::uint64_t seed) {
    static const char* ids[]{"circle", "triangle", "charger", "shooter"};
    return ids[DeriveRunSeed(seed, 0x41524348ULL) % 4];
}

std::uint32_t EnemyStage(
    const std::string& archetype, EnemyDifficultyStat stat) {
    return RunMode() ? DifficultyStage(run, archetype, stat) : 0;
}

std::uint32_t& MutableEnemyStage(
    const std::string& archetype, EnemyDifficultyStat stat) {
    EnemyDifficultyStages& stages = DifficultyStages(archetype);
    switch (stat) {
        case EnemyDifficultyStat::Size: return stages.size;
        case EnemyDifficultyStat::Speed: return stages.speed;
        case EnemyDifficultyStat::Health: return stages.health;
        case EnemyDifficultyStat::Burst: return stages.burst;
    }
    return stages.size;
}

bool OrganDifficultyStat(
    const std::string& id, EnemyDifficultyStat& stat) {
    if (id == "size") stat = EnemyDifficultyStat::Size;
    else if (id == "speed") stat = EnemyDifficultyStat::Speed;
    else if (id == "health") stat = EnemyDifficultyStat::Health;
    else if (id == "burst") stat = EnemyDifficultyStat::Burst;
    else return false;
    return true;
}

int BaseStatValue(const EnemyType& type, EnemyDifficultyStat stat) {
    switch (stat) {
        case EnemyDifficultyStat::Size: return type.pixelScale;
        case EnemyDifficultyStat::Speed:
            return static_cast<int>(std::lround(type.speed));
        case EnemyDifficultyStat::Health: return type.maxHealth;
        case EnemyDifficultyStat::Burst:
            return (type.burstMin + type.burstMax + 1) / 2;
    }
    return 0;
}

int DisplayEnemyOrganValue(
    const EnemyType& type, EnemyDifficultyStat stat,
    std::uint32_t stage) {
    // Size, speed, and burst retain their native simulation units. Their
    // shootable labels instead expose the normalized difficulty rank, whose
    // floor is one. Health remains a direct health-value display.
    if (stat != EnemyDifficultyStat::Health)
        return 1 + static_cast<int>(stage);
    return BaseStatValue(type, stat) + static_cast<int>(stage);
}

void SyncInteriorStats() {
    const EnemyType& type = types.at(interior.archetype);
    for (Organ& organ : organs) {
        EnemyDifficultyStat stat;
        if (!OrganDifficultyStat(organ.id, stat)) continue;
        organ.value = DisplayEnemyOrganValue(
            type, stat, EnemyStage(interior.archetype, stat));
        organ.maximum = organ.value;
        UpdateValueWord(organ);
    }
}

void ResetEnemyDifficultyProgress() {
    run.enemyDifficulty.clear();
    run.clearedBossQuadrants.fill(false);
    const std::string archetype =
        types.count(interior.archetype) ? interior.archetype :
        types.count("circle") ? "circle" : "";
    if (archetype.empty()) return;
    const EnemyType& type = types.at(archetype);
    for (Organ& organ : organs) {
        EnemyDifficultyStat stat;
        if (!OrganDifficultyStat(organ.id, stat)) continue;
        organ.value = DisplayEnemyOrganValue(type, stat, 0);
        organ.maximum = organ.value;
        UpdateValueWord(organ);
    }
    SaveMutations();
}

void ApplyArenaDownside(RunNode& node) {
    if (node.type != RunNodeType::EnemyArena || node.downsideApplied)
        return;
    static constexpr const char* enemyArchetypes[]{
        "circle", "triangle", "charger", "shooter"};
    for (const char* archetype : enemyArchetypes)
        if (types.count(archetype))
            ++MutableEnemyStage(archetype, node.downside);
    node.downsideApplied = true;
}

void RebuildRunArena(RunNode& node) {
    const bool fullAudioWall = node.type == RunNodeType::EnemyArena &&
        DeriveRunSeed(node.seed, 0x46554c4c41554449ULL) % 100 <
            rogueliteTuning.fullAudioWallChancePercent;
    static constexpr std::array<Sound, 4> fullAudioSounds{{
        Sound::LaserShoot, Sound::HitEnemy, Sound::HitHurt,
        Sound::Explosion}};
    const Sound fullAudioSound = fullAudioSounds[DeriveRunSeed(
        node.seed, 0x46554c4c534f554eULL) % fullAudioSounds.size()];
    const PortalDirection fullAudioDirection =
        static_cast<PortalDirection>(DeriveRunSeed(
            node.seed, 0x46554c4c44495245ULL) % 4);
    runArena = BuildArenaLevel(
        {0, 0, kRunArenaWidth, kRunArenaHeight},
        node.seed, {}, 12, kWall, false, fullAudioWall, fullAudioSound,
        fullAudioDirection);
}

Rect InteriorEntryPortalRect(int entryRoom) {
    if (entryRoom < 0 ||
        entryRoom >= static_cast<int>(rooms.size()))
        return {};
    const Room& entry = rooms[entryRoom];
    int rowDirection = 0;
    int columnDirection = 0;
    static constexpr int dr[]{-1, 1, 0, 0};
    static constexpr int dc[]{0, 0, -1, 1};
    for (int direction = 0; direction < 4; ++direction) {
        const int neighbor = RoomIndexAt(
            entry.row + dr[direction], entry.column + dc[direction]);
        if (neighbor >= 0 && RoomsConnected(entryRoom, neighbor)) {
            rowDirection = dr[direction];
            columnDirection = dc[direction];
            break;
        }
    }
    const float offset = interior.roomSize * 0.3f;
    return {
        RoomX(entry) + interior.roomSize * 0.5f -
            columnDirection * offset - 36.0f,
        RoomY(entry) + interior.roomSize * 0.5f -
            rowDirection * offset - 36.0f,
        72, 72};
}

Rect PhysicalExitPortalRect() {
    if (debugRoom)
        return {kRunArenaWidth * 0.5f - 36.0f,
                kRunArenaHeight * 0.5f - 36.0f, 72, 72};
    const RunNode* node = CurrentRunNode();
    if (node && (node->type == RunNodeType::Interior ||
                 node->type == RunNodeType::PlayerInterior ||
                 node->type == RunNodeType::BossInterior) &&
        !rooms.empty()) {
        if (!node->portals.empty() &&
            node->portals.front().interiorTrigger.width > 0)
            return node->portals.front().interiorTrigger;
        const int room = node->interiorEntryRoom >= 0
            ? node->interiorEntryRoom : 0;
        return {RoomX(rooms[room]) + interior.roomSize * 0.5f - 36.0f,
                RoomY(rooms[room]) + interior.roomSize * 0.5f - 36.0f,
                72, 72};
    }
    return {kRunArenaWidth * 0.5f - 36.0f,
            kRunArenaHeight * 0.5f - 36.0f, 72, 72};
}

void AppendPortalTextBoxes() {
    const bool active = debugRoom ||
        (CurrentRunNode() && !CurrentRunNode()->portals.empty() &&
         CurrentRunNode()->portals.front().active);
    const auto phrase = phrases.find("exit_to_map");
    if (!active || phrase == phrases.end()) return;
    const Rect label = PortalLabelRect();
    float x = label.x;
    for (int word : phrase->second) {
        const float width = static_cast<float>(
            text_renderer::MeasureWidth(words[word].bytes.size()));
        textBoxes.push_back({
            {x, label.y, width,
             static_cast<float>(text_renderer::kGlyphHeight)}, word});
        x += width + text_renderer::kGlyphAdvance;
    }
}

void RebuildGameplayTextBoxes() {
    const RunNode* node = CurrentRunNode();
    if (debugRoom || !node ||
        (node->type != RunNodeType::Interior &&
         node->type != RunNodeType::BossInterior))
        textBoxes.clear();
    else if (node->type == RunNodeType::Interior)
        BuildWorldTextBoxes();
    else
        textBoxes.clear();
    AppendPortalTextBoxes();
}

void CreateWaveSpawners(RunNode& node, std::uint32_t wave) {
    spawners.clear();
    const std::uint32_t count =
        std::max<std::uint32_t>(2, 1 + wave) +
        static_cast<std::uint32_t>(node.hardArena);
    static constexpr std::array<const char*, 4> enemyTypes{{
        "circle", "triangle", "charger", "shooter"}};
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::uint64_t seed =
            DeriveRunSeed(node.seed, 0x57415645ULL + wave, index);
        Spawner spawner;
        spawner.room = 0;
        if (index == 0) {
            spawner.enemyType = node.arenaArchetype;
            spawner.guaranteedArchetype = true;
        } else {
            std::array<const char*, 3> alternatives{};
            std::size_t alternativeCount = 0;
            for (const char* type : enemyTypes)
                if (node.arenaArchetype != type)
                    alternatives[alternativeCount++] = type;
            spawner.enemyType = alternatives[
                DeriveRunSeed(seed, 0x4d49584544ULL) % alternativeCount];
        }
        spawner.x = 220.0f +
            static_cast<float>(
                seed % static_cast<std::uint64_t>(
                    kRunArenaWidth - 440.0f));
        spawner.y = 180.0f +
            static_cast<float>(
                (seed >> 16) % static_cast<std::uint64_t>(
                    kRunArenaHeight - 360.0f));
        spawner.health = 3 + static_cast<int>(wave);
        ResetSpawnerTimer(
            spawner, 0.75f +
                static_cast<float>((seed >> 32) % 100) / 100.0f);
        spawner.wave = wave;
        spawner.id = nextSpawnerId++;
        spawners.push_back(std::move(spawner));
    }
}

void ConfigureNode(RunNode& node) {
    debugRoom = false;
    if (legacyInteriorArchetype.empty())
        legacyInteriorArchetype = interior.archetype;
    if (legacyInteriorRoomSize == 0)
        legacyInteriorRoomSize = interior.roomSize;
    currentMap = "interior";
    run.mapActive = false;
    projectiles.clear();
    bombs.clear();
    explosions.clear();
    enemyRails.clear();
    textBoxes.clear();
    shieldBlocks.clear();
    spawners.clear();
    enemies.clear();
    playerX = kRunArenaWidth * 0.5f - kPlayerSize * 0.5f;
    playerY = kRunArenaHeight - 100.0f;
    random.seed(static_cast<std::uint32_t>(
        node.seed ^ (node.seed >> 32)));
    ApplyArenaDownside(node);
    if (node.type == RunNodeType::Interior) {
        interior.roomSize = legacyInteriorRoomSize;
        if (node.arenaArchetype.empty())
            node.arenaArchetype = ArenaArchetype(node.seed);
        interior.archetype = node.arenaArchetype;
        interior.enemy = node.arenaArchetype;
        SyncInteriorStats();
        BuildInteriorWorld(node.seed);
        node.interiorEntryRoom = CurrentRoom();
        GenerateInteriorSpawners(node.seed);
    } else if (node.type == RunNodeType::PlayerInterior) {
        interior.archetype = "player";
        interior.roomSize = static_cast<int>(
            rogueliteTuning.playerInteriorRoomSize);
        BuildPlayerInteriorWorld(node.seed, node.playerInteriorSpawnRoom);
        node.interiorEntryRoom = node.playerInteriorSpawnRoom;
        node.playerInteriorAlteration = -1;
        node.playerInteriorAlterationRoom = -1;
        node.playerInteriorAlterationTimer = 0;
        node.playerInteriorRoom = -1;
        node.playerInteriorWave = false;
        std::array<bool, 9> reachable{};
        std::queue<int> pending;
        reachable[node.playerInteriorSpawnRoom] = true;
        pending.push(node.playerInteriorSpawnRoom);
        while (!pending.empty()) {
            const int room = pending.front();
            pending.pop();
            for (int doorway = 0; doorway < 12; ++doorway) {
                if (!playerInteriorState.brokenDoorways[doorway]) continue;
                const auto [first, second] =
                    PlayerInteriorDoorwayRooms(doorway);
                const int next = first == room ? second :
                    second == room ? first : -1;
                if (next >= 0 && !reachable[next]) {
                    reachable[next] = true;
                    pending.push(next);
                }
            }
        }
        const int doorwayStart = static_cast<int>(
            DeriveRunSeed(node.seed, 0x444f4f52574159ULL) % 12);
        for (int offset = 0; offset < 12; ++offset) {
            const int doorway = (doorwayStart + offset) % 12;
            if (playerInteriorState.brokenDoorways[doorway]) continue;
            const auto [first, second] =
                PlayerInteriorDoorwayRooms(doorway);
            if (first >= 0 && reachable[first] != reachable[second]) {
                node.playerInteriorRoom = doorway;
                break;
            }
        }
        if (node.playerInteriorRoom >= 0) {
            OpenPlayerInteriorDoorway(node.playerInteriorRoom);
            shieldBlocks.push_back({
                PlayerInteriorDoorwayRect(node.playerInteriorRoom), -1, 10});
        }
    } else if (node.type == RunNodeType::BossInterior) {
        interior.archetype = "boss";
        interior.roomSize = bossTuning.interiorRoomSize;
        BuildBossInteriorWorld(
            node.seed, node.bossQuadrant, node.interiorEntryRoom);
        BuildBossTurrets(
            node.seed, true, node.bossQuadrant, true);
        run.boss.active = false;
    } else {
        interior.archetype = legacyInteriorArchetype;
        interior.roomSize = legacyInteriorRoomSize;
        run.boss = BossFightState{};
    }
    node.portals.clear();
    if (node.type != RunNodeType::Boss) {
        RunPortal portal;
        portal.destination = kInvalidRunNode;
        portal.direction = PortalDirection::North;
        portal.center = kRunArenaWidth * 0.5f;
        portal.width = rogueliteTuning.portalWidth;
        portal.active =
            node.completed || node.type == RunNodeType::Interior;
        if ((node.type == RunNodeType::Interior ||
             node.type == RunNodeType::PlayerInterior ||
             node.type == RunNodeType::BossInterior) && !rooms.empty()) {
            const int entryRoom = node.interiorEntryRoom;
            if (node.type == RunNodeType::Interior)
                portal.interiorTrigger =
                    InteriorEntryPortalRect(entryRoom);
            else {
                const Room& destinationRoom = rooms[
                    entryRoom >= 0
                        ? static_cast<std::size_t>(entryRoom) : 0];
                portal.interiorTrigger = {
                    RoomX(destinationRoom) +
                        interior.roomSize * 0.5f - 36.0f,
                    RoomY(destinationRoom) +
                        interior.roomSize * 0.5f - 36.0f,
                    72, 72};
            }
        }
        node.portals.push_back(portal);
    }
    if (node.type == RunNodeType::Boss) {
        InitializeBossFight(node);
        node.waves.clear();
        node.activeWave = 0;
        node.waveCooldown = 0;
        enemies.clear();
        spawners.clear();
    } else if (node.type == RunNodeType::Shop) {
        node.completed = true;
        if (node.shopOffers.empty()) {
            static const UpgradeType upgrades[]{
                UpgradeType::FireRate, UpgradeType::BombCooldown,
                UpgradeType::MoveSpeed, UpgradeType::Invincibility};
            const std::uint32_t rotation = static_cast<std::uint32_t>(
                DeriveRunSeed(node.seed, 0x4f46464552ULL) % 4);
            for (std::uint32_t index = 0; index < 3; ++index) {
                ShopOffer offer;
                offer.id =
                    DeriveRunSeed(node.seed, 0x53484f50ULL, index);
                offer.upgrade = upgrades[(rotation + index) % 4];
                offer.price = rogueliteTuning.shopPrice;
                node.shopOffers.push_back(offer);
            }
            ShopOffer railgun;
            railgun.id = DeriveRunSeed(node.seed, 0x53484f50ULL, 3);
            railgun.kind = ShopOfferKind::PrimaryWeapon;
            railgun.primaryWeapon = PrimaryWeapon::Railgun;
            railgun.price = rogueliteTuning.shopPrice;
            node.shopOffers.push_back(railgun);
            ShopOffer boomerang;
            boomerang.id = DeriveRunSeed(node.seed, 0x53484f50ULL, 6);
            boomerang.kind = ShopOfferKind::PrimaryWeapon;
            boomerang.primaryWeapon = PrimaryWeapon::Boomerang;
            boomerang.price = rogueliteTuning.shopPrice;
            node.shopOffers.push_back(boomerang);
            ShopOffer rocket;
            rocket.id = DeriveRunSeed(node.seed, 0x53484f50ULL, 4);
            rocket.kind = ShopOfferKind::SecondaryWeapon;
            rocket.secondaryWeapon = SecondaryWeapon::HomingRocket;
            rocket.price = rogueliteTuning.shopPrice;
            node.shopOffers.push_back(rocket);
            ShopOffer contactBomb;
            contactBomb.id = DeriveRunSeed(node.seed, 0x53484f50ULL, 5);
            contactBomb.kind = ShopOfferKind::SecondaryWeapon;
            contactBomb.secondaryWeapon = SecondaryWeapon::ContactBomb;
            contactBomb.price = rogueliteTuning.shopPrice;
            node.shopOffers.push_back(contactBomb);
        }
        for (RunPortal& portal : node.portals) portal.active = true;
    } else if (node.type == RunNodeType::Interior ||
               node.type == RunNodeType::PlayerInterior ||
               node.type == RunNodeType::BossInterior) {
        node.waves.clear();
        node.activeWave = 0;
        node.waveCooldown = 0;
        enemies.clear();
    } else {
        if (node.arenaArchetype.empty())
            node.arenaArchetype = ArenaArchetype(node.seed);
        if (node.waves.empty()) {
            const std::uint32_t waveCount =
                node.type == RunNodeType::Interior
                    ? rogueliteTuning.interiorWaves
                    : rogueliteTuning.arenaWaves;
            for (std::uint32_t index = 0; index < waveCount; ++index)
                node.waves.push_back({
                    index, 1 + index,
                    DeriveRunSeed(node.seed, 0x57415645ULL, index), false});
        }
        node.activeWave = 0;
        node.waveCooldown = 0;
        CreateWaveSpawners(node, 0);
    }
    RebuildRunArena(node);
    if (!runArena.audioWalls.empty())
        playerY = kRunArenaHeight - 430.0f;
    RebuildGameplayTextBoxes();
}

void EnterRunMap() {
    debugRoom = false;
    const RunMapVertex* current =
        GetRunMapVertex(run, run.currentNode);
    if (!current) return;
    run.mapActive = true;
    projectiles.clear();
    bombs.clear();
    explosions.clear();
    enemyRails.clear();
    spawners.clear();
    enemies.clear();
    textBoxes.clear();
    shieldBlocks.clear();
    playerX = std::clamp(
        current->x - 115.0f, 0.0f, kRunMapWidth - kPlayerSize);
    playerY = std::clamp(
        current->y - kPlayerSize * 0.5f,
        0.0f, kRunMapHeight - kPlayerSize);
}

void DebugClearCurrentNode() {
    if (MainMenuActive() || run.mapActive) return;
    RunNode* node = CurrentRunNode();
    if (!node) return;
    node->completed = true;
    for (RunWave& wave : node->waves) wave.completed = true;
    for (RunPortal& portal : node->portals) portal.active = true;
    EnterRunMap();
}

bool RunWall(const Rect& rectangle) {
    return RunArenaMode() && ArenaGeometryOverlaps(runArena, rectangle);
}

bool HitShopOffer(const Rect& shot) {
    RunNode* node = CurrentRunNode();
    if (!node || node->type != RunNodeType::Shop) return false;
    for (std::size_t index = 0; index < node->shopOffers.size(); ++index) {
        const Rect target = ShopOfferTarget(index);
        if (!Overlaps(shot, target)) continue;
        ShopOffer& offer = node->shopOffers[index];
        if (!offer.purchased && run.currency >= offer.price) {
            run.currency -= offer.price;
            if (offer.kind == ShopOfferKind::Upgrade)
                AddUpgradeStep(offer.upgrade);
            else if (offer.kind == ShopOfferKind::PrimaryWeapon)
                run.primaryWeapon = offer.primaryWeapon;
            else
                run.secondaryWeapon = offer.secondaryWeapon;
            offer.purchased = true;
        }
        return true;
    }
    return false;
}

Rect PortalLabelRect() {
    const Rect portal = PhysicalExitPortalRect();
    const auto phrase = phrases.find("exit_to_map");
    const float width = phrase == phrases.end()
        ? 0.0f : static_cast<float>(PhraseWidth(phrase->second));
    return {CenterX(portal) - width * 0.5f,
            portal.y - text_renderer::kGlyphHeight - 14.0f,
            width, static_cast<float>(text_renderer::kGlyphHeight)};
}

Rect ResetWordsTarget() {
    return {kRunArenaWidth * 0.5f - 130.0f,
            kRunArenaHeight * 0.5f + 210.0f, 260, 70};
}

Rect ShopOfferTarget(std::size_t index) {
    return {
        kRunArenaWidth * 0.5f - 500.0f +
            static_cast<float>(index % 3) * 400.0f,
        kRunArenaHeight * 0.5f - 180.0f +
            static_cast<float>(index / 3) * 250.0f,
        200.0f, 130.0f};
}

const char* UpgradeName(UpgradeType type) {
    switch (type) {
        case UpgradeType::FireRate: return "SHOT DELAY";
        case UpgradeType::BombCooldown: return "BOMB DELAY";
        case UpgradeType::MoveSpeed: return "MOVE TIME";
        case UpgradeType::MaxHealth: return "MAX HEALTH";
        case UpgradeType::ProjectileDamage: return "SHOT DAMAGE";
        case UpgradeType::Invincibility: return "HIT INVINCIBILITY";
    }
    return "UPGRADE";
}

const char* PrimaryWeaponName(PrimaryWeapon type) {
    if (type == PrimaryWeapon::Railgun) return "RAILGUN";
    if (type == PrimaryWeapon::Boomerang) return "BOOMERANG";
    return "STANDARD SHOT";
}

const char* SecondaryWeaponName(SecondaryWeapon type) {
    if (type == SecondaryWeapon::HomingRocket) return "HOMING ROCKET";
    if (type == SecondaryWeapon::ContactBomb) return "CONTACT BOMB";
    return "BOMB";
}

const char* ShopOfferName(const ShopOffer& offer) {
    if (offer.kind == ShopOfferKind::PrimaryWeapon)
        return PrimaryWeaponName(offer.primaryWeapon);
    if (offer.kind == ShopOfferKind::SecondaryWeapon)
        return SecondaryWeaponName(offer.secondaryWeapon);
    return UpgradeName(offer.upgrade);
}

float PrimaryChargeProgress() {
    return std::clamp(
        run.primaryCharge /
            std::max(0.01f, ResolvePlayerWeapon("railgun").cadence),
        0.0f, 1.0f);
}

float SecondaryCooldownDuration() {
    const char* weapon = run.secondaryWeapon == SecondaryWeapon::Bomb
        ? "bomb" : run.secondaryWeapon == SecondaryWeapon::ContactBomb
        ? "contact_bomb" : "homing_rocket";
    float duration = ResolvePlayerWeapon(weapon).cadence;
    if (playerInteriorState.permanent[5] &&
        playerInteriorState.values[8] == 0)
        duration = std::max(
            ResolvePlayerWeapon("bomb").cadence,
            std::max(
                ResolvePlayerWeapon("contact_bomb").cadence,
                ResolvePlayerWeapon("homing_rocket").cadence));
    return duration;
}

float UpgradeCurrentValue(UpgradeType type) {
    if (type == UpgradeType::FireRate)
        return EffectiveShotInterval();
    if (type == UpgradeType::BombCooldown)
        return EffectiveBombCooldown();
    if (type == UpgradeType::MoveSpeed)
        return 100.0f /
            (kPlayerSpeed + 12.0f * UpgradeRank(UpgradeType::MoveSpeed));
    if (type == UpgradeType::Invincibility)
        return EffectiveInvincibilityDuration();
    return static_cast<float>(UpgradeRank(type));
}

void EnterRunNode(RunNodeId id) {
    LeaveMainMenu();
    if (!SetCurrentRunNode(id)) return;
    RunNode* node = CurrentRunNode();
    if (node) ConfigureNode(*node);
}

void StartFreshRun(bool enterFirstNode) {
    LeaveMainMenu();
    static std::uint64_t sequence = 0;
    const std::uint64_t clockSeed = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now()
            .time_since_epoch().count());
    StartRun(DeriveRunSeed(clockSeed, 0x4652455348ULL, ++sequence));
    ResetEnemyDifficultyProgress();
    playerHealth = kPlayerMaxHealth;
    playerInvincibility = 0;
    if (enterFirstNode) {
        RunNode* node = CurrentRunNode();
        if (node) ConfigureNode(*node);
    }
}

}  // namespace game

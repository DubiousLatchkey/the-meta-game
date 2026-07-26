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

int EffectivePlayerMaxHealth() {
    return kPlayerMaxHealth + static_cast<int>(
        UpgradeRank(UpgradeType::MaxHealth));
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
            0.025f * result.cadenceEffectScale * fireRanks);
    if (id == "bomb" || id == "contact_bomb" || id == "homing_rocket")
        result.cadence = std::max(
            0.05f, result.cadence -
                0.2f * result.cadenceEffectScale *
                    UpgradeRank(UpgradeType::BombCooldown));
    result.damage += static_cast<int>(
        UpgradeRank(UpgradeType::ProjectileDamage));
    result.projectilesPerShot +=
        playerInteriorState.permanent[6] ? 1 : 0;
    if (run.multishotRemaining > 0)
        result.count += 2;
    if (playerInteriorState.permanent[1])
        result.count += std::max(0, 3 - playerInteriorState.values[4]);
    if (playerInteriorState.permanent[7])
        ++result.count;
    if (run.homingRemaining > 0 ||
        (playerInteriorState.permanent[2] &&
         playerInteriorState.values[5] == 0))
        result.homing = true;
    if (id == "auto_rocket" && playerInteriorState.permanent[3])
        result.cadence = std::max(
            0.05f, playerInteriorState.values[6] / 1000.0f);
    return result;
}

std::string ArenaArchetype(std::uint64_t seed) {
    static const char* ids[]{"circle", "triangle", "charger", "shooter"};
    return ids[DeriveRunSeed(seed, 0x41524348ULL) % 4];
}

std::uint32_t EnemyThreat(const std::string& archetype) {
    const EnemyDifficultyStages& stages =
        DifficultyStages(run, archetype);
    const std::array<std::uint32_t, 6> values{{
        stages.size, stages.speed, stages.health, stages.burst,
        stages.damage, stages.spawnerHealth}};
    const std::uint32_t total =
        stages.size + stages.speed + stages.health + stages.burst +
        stages.damage + stages.spawnerHealth;
    const std::uint32_t peak =
        *std::max_element(values.begin(), values.end());
    // Total growth catches broadly upgraded enemies; the squared peak term
    // makes one dangerously over-upgraded stat matter on its own.
    return 1 + total + total * total / 4 + peak * peak;
}

std::string ThreatWeightedInteriorArchetype(std::uint64_t seed) {
    static constexpr std::array<const char*, 4> ids{{
        "circle", "triangle", "charger", "shooter"}};
    std::array<std::uint32_t, ids.size()> weights{};
    std::uint32_t totalWeight = 0;
    for (std::size_t index = 0; index < ids.size(); ++index) {
        weights[index] = EnemyThreat(ids[index]);
        totalWeight += weights[index];
    }
    std::uint32_t roll = static_cast<std::uint32_t>(
        DeriveRunSeed(seed, 0x544852454154ULL) % totalWeight);
    for (std::size_t index = 0; index < ids.size(); ++index) {
        if (roll < weights[index]) return ids[index];
        roll -= weights[index];
    }
    return ids.back();
}

RunNodeId AppendChoiceNode(
    RunNodeId sourceId, RunNodeType type, std::uint32_t level,
    std::uint64_t seed, std::size_t slot) {
    RunNode choice;
    choice.id = static_cast<RunNodeId>(run.nodes.size());
    choice.type = type;
    choice.depth = level;
    choice.seed = seed;
    if (type == RunNodeType::EnemyArena) {
        choice.arenaArchetype = ArenaArchetype(seed);
    } else if (type == RunNodeType::Interior) {
        choice.arenaArchetype =
            ThreatWeightedInteriorArchetype(seed);
    }
    if (type == RunNodeType::EnemyArena) {
        choice.downside = static_cast<EnemyDifficultyStat>(
            DeriveRunSeed(seed, 0x444f574e53494445ULL) % 6);
        choice.hardArena =
            DeriveRunSeed(seed, 0x48415244ULL) % 4 == 0;
    } else if (type == RunNodeType::BossInterior) {
        choice.bossQuadrant = static_cast<BossQuadrant>(
            DeriveRunSeed(seed, 0x51554144ULL) % 4);
    } else if (type == RunNodeType::Shop) {
        choice.shopReturnDestination = sourceId;
    }
    const RunNodeId id = choice.id;
    run.nodes.push_back(std::move(choice));
    run.nodes[sourceId].next.push_back(id);
    (void)slot;
    return id;
}

Rect ArenaChoicePortalRect(std::size_t index, std::size_t count) {
    constexpr float size = 72.0f;
    constexpr float inset = 170.0f;
    const std::array<Rect, 4> cardinal{{
        {kRunArenaWidth * 0.5f - size * 0.5f, inset, size, size},
        {kRunArenaWidth - inset - size,
         kRunArenaHeight * 0.5f - size * 0.5f, size, size},
        {kRunArenaWidth * 0.5f - size * 0.5f,
         kRunArenaHeight - inset - size, size, size},
        {inset, kRunArenaHeight * 0.5f - size * 0.5f, size, size},
    }};
    if (count == 2) return cardinal[index == 0 ? 3 : 1];
    return cardinal[index % cardinal.size()];
}

void BuildArenaChoicePortals(RunNode& node) {
    const RunNodeId sourceId = node.id;
    const std::uint32_t sourceLevel = node.depth;
    const std::uint64_t sourceSeed = node.seed;
    std::vector<RunNodeType> choices;
    const std::uint32_t nextLevel =
        sourceLevel > 0 ? sourceLevel - 1 : 0;
    if (nextLevel == 0) {
        choices = {RunNodeType::Boss, RunNodeType::Shop};
    } else {
        const std::uint64_t roll =
            DeriveRunSeed(sourceSeed, 0x504f5254414c53ULL);
        const bool enemyInterior =
            run.arenasWithoutEnemyInterior >= 2 || roll % 3 == 0;
        if (enemyInterior)
            choices.push_back(RunNodeType::Interior);
        run.arenasWithoutEnemyInterior = enemyInterior
            ? 0 : run.arenasWithoutEnemyInterior + 1;
        if ((roll >> 8) % 4 == 0)
            choices.push_back(RunNodeType::BossInterior);
        if ((roll >> 16) % 7 == 0)
            choices.push_back(RunNodeType::PlayerInterior);
        if ((roll >> 24) % 10 != 0)
            choices.push_back(RunNodeType::Shop);
        while (choices.size() < 4)
            choices.push_back(RunNodeType::EnemyArena);
        for (std::size_t index = choices.size(); index > 1; --index) {
            const std::size_t other = static_cast<std::size_t>(
                DeriveRunSeed(roll, 0x53485546464c45ULL, index) % index);
            std::swap(choices[index - 1], choices[other]);
        }
    }
    run.nodes.reserve(run.nodes.size() + choices.size());
    RunNode& source = run.nodes[sourceId];
    source.portals.clear();
    source.next.clear();
    for (std::size_t index = 0; index < choices.size(); ++index) {
        const std::uint64_t seed = DeriveRunSeed(
            sourceSeed, 0x43484f494345ULL, index);
        const RunNodeId destination = AppendChoiceNode(
            sourceId, choices[index], nextLevel, seed, index);
        RunPortal portal;
        portal.destination = destination;
        portal.direction = choices.size() == 2
            ? (index == 0 ? PortalDirection::West : PortalDirection::East)
            : static_cast<PortalDirection>(index);
        portal.active = true;
        portal.interiorTrigger =
            ArenaChoicePortalRect(index, choices.size());
        run.nodes[sourceId].portals.push_back(portal);
    }
}

void BuildInteriorArenaDestinations(RunNode& node) {
    const RunNodeId sourceId = node.id;
    const std::uint64_t sourceSeed = node.seed;
    const std::size_t portalCount = node.portals.size();
    const std::uint32_t nextLevel =
        node.depth > 0 ? node.depth - 1 : 0;
    run.nodes.reserve(run.nodes.size() + portalCount);
    run.nodes[sourceId].next.clear();
    for (std::size_t index = 0; index < portalCount; ++index) {
        RunPortal& portal = run.nodes[sourceId].portals[index];
        if (portal.destination != kInvalidRunNode) continue;
        portal.destination = AppendChoiceNode(
            sourceId, RunNodeType::EnemyArena, nextLevel,
            DeriveRunSeed(sourceSeed, 0x494e5445584954ULL, index), index);
        run.nodes[sourceId].portals[index].armed = false;
    }
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
        case EnemyDifficultyStat::Damage: return stages.damage;
        case EnemyDifficultyStat::SpawnerHealth: return stages.spawnerHealth;
    }
    return stages.size;
}

bool OrganDifficultyStat(
    const std::string& id, EnemyDifficultyStat& stat) {
    if (id == "size") stat = EnemyDifficultyStat::Size;
    else if (id == "speed") stat = EnemyDifficultyStat::Speed;
    else if (id == "health") stat = EnemyDifficultyStat::Health;
    else if (id == "burst") stat = EnemyDifficultyStat::Burst;
    else if (id == "damage") stat = EnemyDifficultyStat::Damage;
    else if (id == "spawner_health") stat = EnemyDifficultyStat::SpawnerHealth;
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
        case EnemyDifficultyStat::Damage: return type.contactDamage;
        case EnemyDifficultyStat::SpawnerHealth: return 5;
    }
    return 0;
}

int DisplayEnemyOrganValue(
    const EnemyType& type, EnemyDifficultyStat stat,
    std::uint32_t stage) {
    // Size, speed, and burst retain their native simulation units. Their
    // shootable labels instead expose the normalized difficulty rank, whose
    // floor is one. Health remains a direct health-value display.
    if (stat != EnemyDifficultyStat::Health &&
        stat != EnemyDifficultyStat::Damage &&
        stat != EnemyDifficultyStat::SpawnerHealth)
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
    // Keep choice portals in their intended cardinal directions while moving
    // them inward until the complete trigger is clear of arena-wall geometry.
    if (node.type == RunNodeType::EnemyArena && node.completed)
        for (RunPortal& portal : node.portals) {
            Rect candidate = portal.interiorTrigger;
            float stepX = 0;
            float stepY = 0;
            if (portal.direction == PortalDirection::North) stepY = 24;
            else if (portal.direction == PortalDirection::East) stepX = -24;
            else if (portal.direction == PortalDirection::South) stepY = -24;
            else stepX = 24;
            for (int attempt = 0; attempt < 24; ++attempt) {
                if (ArenaAllowsPlayer(runArena, candidate)) {
                    portal.interiorTrigger = candidate;
                    break;
                }
                candidate.x += stepX;
                candidate.y += stepY;
            }
        }
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
    const bool active = debugRoom;
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
    const bool firstArena = node.id == run.startNode;
    const std::uint32_t count = std::min<std::uint32_t>(
        firstArena ? 3 : UINT32_MAX,
        std::max<std::uint32_t>(2, 1 + wave) +
        static_cast<std::uint32_t>(node.hardArena));
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
        } else if (firstArena) {
            spawner.enemyType = DeriveRunSeed(seed, 0x45415359ULL) % 2 == 0
                ? "circle" : "triangle";
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
        spawner.health = 5 + static_cast<int>(wave) + static_cast<int>(
            EnemyStage(node.arenaArchetype, EnemyDifficultyStat::SpawnerHealth));
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
    if (node.portals.empty() && node.type != RunNodeType::Boss) {
        RunPortal portal;
        portal.destination = node.type == RunNodeType::Shop
            ? node.shopReturnDestination : kInvalidRunNode;
        portal.direction = PortalDirection::North;
        portal.center = kRunArenaWidth * 0.5f;
        portal.width = rogueliteTuning.portalWidth;
        portal.active = node.completed || node.type == RunNodeType::Interior ||
            node.type == RunNodeType::Shop;
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
        // The return portal lives below the shop layout so it remains a
        // distinct navigation target rather than reading as another offer.
        for (RunPortal& portal : node.portals)
            portal.interiorTrigger = {
                kRunArenaWidth * 0.5f - 36.0f,
                kRunArenaHeight - 150.0f, 72.0f, 72.0f};
        if (node.shopOffers.empty()) {
            static const UpgradeType upgrades[]{
                UpgradeType::FireRate, UpgradeType::BombCooldown,
                UpgradeType::MoveSpeed, UpgradeType::Invincibility,
                UpgradeType::ProjectileDamage, UpgradeType::MaxHealth};
            const std::uint32_t rotation = static_cast<std::uint32_t>(
                DeriveRunSeed(node.seed, 0x4f46464552ULL) % 6);
            for (std::uint32_t index = 0; index < 3; ++index) {
                ShopOffer offer;
                offer.id =
                    DeriveRunSeed(node.seed, 0x53484f50ULL, index);
                offer.upgrade = upgrades[(rotation + index) % 6];
                offer.price = rogueliteTuning.shopPrice;
                node.shopOffers.push_back(offer);
            }
            ShopOffer weapon;
            weapon.id = DeriveRunSeed(node.seed, 0x53484f50ULL, 3);
            weapon.price = rogueliteTuning.shopPrice;
            const std::uint32_t weaponChoice = static_cast<std::uint32_t>(
                DeriveRunSeed(node.seed, 0x574541504f4eULL) % 4);
            for (std::uint32_t attempt = 0; attempt < 4; ++attempt) {
                const std::uint32_t choice = (weaponChoice + attempt) % 4;
                if (choice == 0 && (!playerInteriorState.permanent[4]
                    ? run.primaryWeapon != PrimaryWeapon::Railgun
                    : !run.primaryWeapons[
                        static_cast<int>(PrimaryWeapon::Railgun)])) {
                    weapon.kind = ShopOfferKind::PrimaryWeapon;
                    weapon.primaryWeapon = PrimaryWeapon::Railgun;
                    break;
                }
                if (choice == 1 && (!playerInteriorState.permanent[4]
                    ? run.primaryWeapon != PrimaryWeapon::Boomerang
                    : !run.primaryWeapons[
                        static_cast<int>(PrimaryWeapon::Boomerang)])) {
                    weapon.kind = ShopOfferKind::PrimaryWeapon;
                    weapon.primaryWeapon = PrimaryWeapon::Boomerang;
                    break;
                }
                if (choice == 2 && (!playerInteriorState.permanent[5]
                    ? run.secondaryWeapon != SecondaryWeapon::HomingRocket
                    : !run.secondaryWeapons[
                        static_cast<int>(SecondaryWeapon::HomingRocket)])) {
                    weapon.kind = ShopOfferKind::SecondaryWeapon;
                    weapon.secondaryWeapon = SecondaryWeapon::HomingRocket;
                    break;
                }
                if (choice == 3 && (!playerInteriorState.permanent[5]
                    ? run.secondaryWeapon != SecondaryWeapon::ContactBomb
                    : !run.secondaryWeapons[
                        static_cast<int>(SecondaryWeapon::ContactBomb)])) {
                    weapon.kind = ShopOfferKind::SecondaryWeapon;
                    weapon.secondaryWeapon = SecondaryWeapon::ContactBomb;
                    break;
                }
            }
            node.shopOffers.push_back(weapon);
        }
        for (RunPortal& portal : node.portals) {
            portal.active = true;
            portal.armed = false;
        }
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
        if (node.waves.empty() && !node.completed) {
            const std::uint32_t waveCount =
                node.type == RunNodeType::Interior
                    ? rogueliteTuning.interiorWaves
                    : rogueliteTuning.arenaWaves;
            for (std::uint32_t index = 0; index < waveCount; ++index)
                node.waves.push_back({
                    index, 1 + index,
                    DeriveRunSeed(node.seed, 0x57415645ULL, index), false});
        }
        if (!node.completed) {
            node.activeWave = 0;
            node.waveCooldown = 0;
            CreateWaveSpawners(node, 0);
        }
    }
    RebuildRunArena(node);
    if (!runArena.audioWalls.empty())
        playerY = kRunArenaHeight - 430.0f;
    RebuildGameplayTextBoxes();
    if (node.type == RunNodeType::Interior)
        BuildInteriorArenaDestinations(node);
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
    // Debug win resolves the encounter in place: eliminate every active
    // spawner/enemy and finish every wave before creating the same exits a
    // normal completion would create.
    for (Spawner& spawner : spawners) spawner.health = 0;
    for (Enemy& enemy : enemies) enemy.health = 0;
    spawners.clear();
    enemies.clear();
    node->completed = true;
    for (RunWave& wave : node->waves) wave.completed = true;
    node->activeWave = static_cast<std::uint32_t>(node->waves.size());
    node->waveCooldown = 0;
    if (node->type == RunNodeType::EnemyArena) {
        BuildArenaChoicePortals(*node);
        RunNode* current = CurrentRunNode();
        if (current) RebuildRunArena(*current);
    } else if (node->type == RunNodeType::Boss) {
        run.boss.health = 0;
        run.boss.projectiles.clear();
        run.status = RunStatus::Won;
    } else {
        for (RunPortal& portal : node->portals) {
            portal.active = true;
            portal.armed = false;
        }
        BuildInteriorArenaDestinations(*node);
    }
    RebuildGameplayTextBoxes();
}

bool RunWall(const Rect& rectangle) {
    return RunArenaMode() && ArenaGeometryOverlaps(runArena, rectangle);
}

bool HitShopOffer(const Rect&) {
    return false;
}

void UpdateShopPurchases(float dt) {
    RunNode* node = CurrentRunNode();
    if (!node || node->type != RunNodeType::Shop) return;
    const Rect player{playerX, playerY, kPlayerSize, kPlayerSize};
    for (std::size_t index = 0; index < node->shopOffers.size(); ++index) {
        ShopOffer& offer = node->shopOffers[index];
        const bool repeatable = offer.kind == ShopOfferKind::Upgrade;
        if ((!repeatable && offer.purchased) || run.currency < offer.price ||
            !Overlaps(player, ShopOfferPurchaseArea(index))) {
            offer.purchaseTimer = 0;
            continue;
        }
        if (offer.purchaseTimer <= 0) PlaySoundEffect(Sound::AimTick);
        offer.purchaseTimer += dt;
        if (offer.purchaseTimer < rogueliteTuning.shopPurchaseSeconds)
            continue;
        run.currency -= offer.price;
        if (offer.kind == ShopOfferKind::Upgrade) {
            AddUpgradeStep(offer.upgrade);
            if (offer.upgrade == UpgradeType::MaxHealth)
                ++playerHealth;
        } else if (offer.kind == ShopOfferKind::PrimaryWeapon) {
            if (playerInteriorState.permanent[4])
                run.primaryWeapons[static_cast<int>(offer.primaryWeapon)] = true;
            else
                run.primaryWeapon = offer.primaryWeapon;
        } else {
            if (playerInteriorState.permanent[5])
                run.secondaryWeapons[
                    static_cast<int>(offer.secondaryWeapon)] = true;
            else
                run.secondaryWeapon = offer.secondaryWeapon;
        }
        if (!repeatable) offer.purchased = true;
        offer.purchaseTimer = 0;
        PlaySoundEffect(Sound::PowerUp);
    }
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

Rect ShopOfferPurchaseArea(std::size_t index) {
    const Rect offer = ShopOfferTarget(index);
    return {offer.x + offer.width + 20.0f, offer.y, 120.0f, offer.height};
}

const char* UpgradeName(UpgradeType type) {
    switch (type) {
        case UpgradeType::FireRate: return "FIRE RATE";
        case UpgradeType::BombCooldown: return "BOMB RATE";
        case UpgradeType::MoveSpeed: return "MOVE SPEED";
        case UpgradeType::MaxHealth: return "MAX HEALTH";
        case UpgradeType::ProjectileDamage: return "SHOT DAMAGE";
        case UpgradeType::Invincibility: return "HIT INVINCIBILITY";
        case UpgradeType::ExtraProjectile: return "EXTRA PROJECTILE";
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

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

namespace {

bool IsBossLevel(int level) {
    return level <= 0 && (-level) % 10 == 0;
}

std::uint32_t DownsideStatCount(int level) {
    return level <= 1 ? 3U : 1U;
}

std::uint32_t DownsideAmount(int level) {
    if (level >= 5) return 1;
    if (level >= 0) return 2;
    const int magnitude = -level;
    const int digit = magnitude % 10;
    return static_cast<std::uint32_t>(
        2 + magnitude / 10 + (digit >= 5 ? 1 : 0) +
        (digit >= 9 ? 1 : 0));
}

}  // namespace

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
    if (id == "auto_rocket" && playerInteriorState.permanent[3])
        result.cadence = std::max(
            0.05f, playerInteriorState.values[6] / 1000.0f);
    const bool bombRateWeapon =
        id == "bomb" || id == "contact_bomb" || id == "homing_rocket";
    const bool primaryRateWeapon =
        id == "standard" || id == "railgun" || id == "boomerang";
    const std::uint32_t fireRanks =
        UpgradeRank(UpgradeType::FireRate);
    // Rail charge speed scales at half the rate of projectile fire speed:
    // two railgun ranks produce the same multiplier as one projectile rank.
    const float fireRateRanks = static_cast<float>(fireRanks) *
        (id == "railgun" ? 0.5f : 1.0f);
    if (primaryRateWeapon)
        result.cadence = std::max(
            0.05f, result.cadence /
                std::pow(1.1f, fireRateRanks));
    if (bombRateWeapon)
        result.cadence = std::max(
            0.05f, result.cadence /
                std::pow(
                    1.1f, static_cast<float>(
                        UpgradeRank(UpgradeType::BombCooldown))));
    if (primaryRateWeapon)
        result.damage += static_cast<int>(
            UpgradeRank(UpgradeType::ProjectileDamage) / 2);
    if (id == "bomb" || id == "contact_bomb" ||
        id == "homing_rocket" || id == "auto_rocket")
        result.damage += static_cast<int>(
            UpgradeRank(UpgradeType::BombDamage) / 2);
    result.projectilesPerShot +=
        playerInteriorState.permanent[6] ? 1 : 0;
    if (run.multishotRemaining > 0)
        result.count += 2;
    if (playerInteriorState.permanent[1] && primaryRateWeapon)
        result.count += std::max(1, 3 - playerInteriorState.values[4]);
    if (playerInteriorState.permanent[7])
        ++result.count;
    if (run.homingRemaining > 0 ||
        (playerInteriorState.permanent[2] &&
         playerInteriorState.values[5] == 0))
        result.homing = true;
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
    RunNodeId sourceId, RunNodeType type, int level,
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
            DeriveRunSeed(seed, 0x444f574e53494445ULL) % 8);
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

void BuildPostBossPortals(RunNode& node) {
    const RunNodeId sourceId = node.id;
    const int nextLevel = node.depth - 1;
    node.portals.clear();
    node.next.clear();
    const RunNodeId continuation = AppendChoiceNode(
        sourceId, RunNodeType::EnemyArena, nextLevel,
        DeriveRunSeed(node.seed, 0x434f4e54494e5545ULL), 0);
    RunNode& source = run.nodes[sourceId];

    constexpr float size = 72.0f;
    const float left = runArena.bounds.x +
        runArena.bounds.width * 0.25f - size * 0.5f;
    const float right = runArena.bounds.x +
        runArena.bounds.width * 0.75f - size * 0.5f;
    const float top = runArena.bounds.y +
        runArena.bounds.height * 0.5f - size * 0.5f;

    RunPortal tuning;
    tuning.active = true;
    tuning.postBossTuning = true;
    tuning.direction = PortalDirection::West;
    tuning.interiorTrigger = {left, top, size, size};
    source.portals.push_back(tuning);

    RunPortal onward;
    onward.destination = continuation;
    onward.active = true;
    onward.continueRun = true;
    onward.direction = PortalDirection::East;
    onward.interiorTrigger = {right, top, size, size};
    source.portals.push_back(onward);
}

void BuildArenaChoicePortals(RunNode& node) {
    const RunNodeId sourceId = node.id;
    const int sourceLevel = node.depth;
    const std::uint64_t sourceSeed = node.seed;
    std::vector<RunNodeType> choices;
    const int nextLevel = sourceLevel - 1;
    if (IsBossLevel(nextLevel)) {
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
        const std::uint64_t playerInteriorRoll = (roll >> 16) % 14;
        const bool earlyDepth = sourceLevel >= 5 && sourceLevel <= 10;
        const int playerInteriorBatch = sourceLevel >= 0 ? 0 :
            1 + (-sourceLevel - 1) / 10;
        const bool batchAlreadyUsed = std::find(
            run.playerInteriorPortalBatches.begin(),
            run.playerInteriorPortalBatches.end(),
            playerInteriorBatch) != run.playerInteriorPortalBatches.end();
        if (!batchAlreadyUsed &&
            playerInteriorRoll < (earlyDepth ? 3ULL : 2ULL)) {
            choices.push_back(RunNodeType::PlayerInterior);
            run.playerInteriorPortalBatches.push_back(playerInteriorBatch);
        }
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
    const int nextLevel = node.depth - 1;
    const RunNodeType destinationType =
        IsBossLevel(nextLevel)
            ? RunNodeType::Boss : RunNodeType::EnemyArena;
    run.nodes.reserve(run.nodes.size() + portalCount);
    run.nodes[sourceId].next.clear();
    for (std::size_t index = 0; index < portalCount; ++index) {
        RunPortal& portal = run.nodes[sourceId].portals[index];
        if (portal.destination != kInvalidRunNode) continue;
        portal.destination = AppendChoiceNode(
            sourceId, destinationType, nextLevel,
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
        case EnemyDifficultyStat::SpawnSpeed: return stages.spawnSpeed;
        case EnemyDifficultyStat::ChildCapacity: return stages.childCapacity;
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
    else if (id == "spawn_speed") stat = EnemyDifficultyStat::SpawnSpeed;
    else if (id == "child_capacity")
        stat = EnemyDifficultyStat::ChildCapacity;
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
        case EnemyDifficultyStat::SpawnSpeed:
            return static_cast<int>(std::lround(type.spawnSpeed));
        case EnemyDifficultyStat::ChildCapacity: return type.childCapacity;
    }
    return 0;
}

int DisplayEnemyOrganValue(
    const EnemyType& type, EnemyDifficultyStat stat,
    std::uint32_t stage) {
    // Size, speed, and burst retain their native simulation units. Their
    // shootable labels instead expose the normalized difficulty rank, whose
    // floor is one. Health remains a direct health-value display.
    if (stat == EnemyDifficultyStat::SpawnSpeed ||
        stat == EnemyDifficultyStat::ChildCapacity)
        return BaseStatValue(type, stat) + static_cast<int>(stage);
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
    // The opening arena is the baseline tutorial encounter: it must not
    // advance any session-only enemy difficulty stage.
    if (node.id == run.startNode) {
        node.downsideApplied = true;
        return;
    }
    static constexpr const char* enemyArchetypes[]{
        "circle", "triangle", "charger", "shooter"};
    std::vector<EnemyDifficultyStat> increases{node.downside};
    const std::uint32_t targetStatCount = DownsideStatCount(node.depth);
    if (targetStatCount > 1) {
        constexpr int totalStatCount = 8;
        for (int offset = 1; increases.size() < targetStatCount; ++offset) {
            const auto candidate = static_cast<EnemyDifficultyStat>(
                DeriveRunSeed(node.seed, 0x3344494646535441ULL, offset) %
                totalStatCount);
            if (std::find(increases.begin(), increases.end(), candidate) ==
                increases.end())
                increases.push_back(candidate);
        }
    }
    const std::uint32_t amount = DownsideAmount(node.depth);
    for (const char* archetype : enemyArchetypes)
        if (types.count(archetype))
            for (EnemyDifficultyStat stat : increases)
                MutableEnemyStage(archetype, stat) += amount;
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
    const float arenaScale = node.type == RunNodeType::Boss ? 1.5f : 1.0f;
    runArena = BuildArenaLevel(
        {0, 0, kRunArenaWidth * arenaScale, kRunArenaHeight * arenaScale},
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
    if (postBossTuningRoom)
        return {
            runArena.bounds.x + runArena.bounds.width * 0.5f - 36.0f,
            runArena.bounds.y + runArena.bounds.height - 150.0f,
            72, 72};
    if (debugRoom)
        return {kRunArenaWidth * 0.5f - 36.0f,
                kRunArenaHeight * 0.5f - 36.0f, 72, 72};
    RunNode* node = CurrentRunNode();
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
    RunNode* node = CurrentRunNode();
    if (!node || (node->type != RunNodeType::EnemyArena &&
                  node->type != RunNodeType::Interior &&
                  node->type != RunNodeType::Boss))
        return;
    auto addTarget = [](int word, const Rect& rect) {
        if (word >= 0) textBoxes.push_back({rect, word});
    };
    const auto coreWord = [](const std::string& id) {
        const auto found = wordIds.find(id);
        return found == wordIds.end() ? -1 : found->second;
    };
    const auto detailWordId = [](EnemyDifficultyStat stat) {
        switch (stat) {
            case EnemyDifficultyStat::Size:
                return "portal_size_up";
            case EnemyDifficultyStat::Speed:
                return "portal_speed_up";
            case EnemyDifficultyStat::Health:
                return "portal_health_up";
            case EnemyDifficultyStat::Burst:
                return "portal_spawn_herd_size_up";
            case EnemyDifficultyStat::Damage:
                return "portal_damage_up";
            case EnemyDifficultyStat::SpawnerHealth:
                return "portal_spawner_health_up";
            case EnemyDifficultyStat::SpawnSpeed:
                return "portal_spawn_speed_up";
            case EnemyDifficultyStat::ChildCapacity:
                return "portal_child_capacity_up";
        }
        return "";
    };
    for (RunPortal& portal : node->portals) {
        if (!portal.active) continue;
        if (portal.postBossTuning) {
            const Rect portalRect = portal.interiorTrigger;
            const int word = coreWord("portal_tuning_room");
            if (word < 0) continue;
            portal.labelWord = word;
            const float width = static_cast<float>(
                text_renderer::MeasureWidth(words[word].bytes.size()));
            textBoxes.push_back({{
                CenterX(portalRect) - width * 0.5f,
                portalRect.y - text_renderer::kGlyphHeight - 16, width,
                static_cast<float>(text_renderer::kGlyphHeight)}, word});
            continue;
        }
        const RunNode* destination = GetRunNode(run, portal.destination);
        if (!destination) continue;
        const std::string archetype = destination->arenaArchetype;
        const std::string labelId =
            portal.continueRun ? "portal_continue_descent" :
            destination->type == RunNodeType::EnemyArena
                ? "portal_" + archetype + "_arena" :
            destination->type == RunNodeType::Interior
                ? "portal_inside_" + archetype :
            destination->type == RunNodeType::PlayerInterior
                ? "portal_inside_player" :
            destination->type == RunNodeType::BossInterior
                ? "portal_inside_boss" :
            destination->type == RunNodeType::Boss ? "portal_boss" :
                "portal_shop";
        const int labelWord = coreWord(labelId);
        if (labelWord >= 0) portal.labelWord = labelWord;
        const bool hasDetail =
            destination->type == RunNodeType::EnemyArena &&
            !portal.continueRun;
        if (hasDetail) {
            portal.detailWord = coreWord("portal_all_enemy");
            const std::uint32_t amount =
                DownsideAmount(destination->depth);
            const int detailWord = DownsideStatCount(destination->depth) > 1
                ? coreWord("portal_three_stats_up")
                : coreWord(detailWordId(destination->downside));
            if (detailWord >= 0) portal.detailWord2 = detailWord;
            if (amount > 1) {
                portal.detailWord3 = coreWord("portal_multiplier");
                if (portal.detailWord3 >= 0) {
                    const std::string multiplier =
                        "X" + std::to_string(amount);
                    words[portal.detailWord3].bytes.assign(
                        multiplier.begin(), multiplier.end());
                }
            } else {
                portal.detailWord3 = -1;
            }
        }
        const Rect portalRect = portal.interiorTrigger.width > 0
            ? portal.interiorTrigger : PhysicalExitPortalRect();
        const bool horizontal = portal.direction == PortalDirection::East ||
            portal.direction == PortalDirection::West;
        const float leftWidth = portal.labelWord >= 0
            ? static_cast<float>(text_renderer::MeasureWidth(
                  words[portal.labelWord].bytes.size()))
            : 0.0f;
        if (horizontal)
            addTarget(portal.labelWord,
                {CenterX(portalRect) - leftWidth * 0.5f,
                portalRect.y - text_renderer::kGlyphHeight - 16,
                leftWidth, static_cast<float>(text_renderer::kGlyphHeight)});
        else
            addTarget(portal.labelWord,
                {portalRect.x - leftWidth - 18,
                CenterY(portalRect) - text_renderer::kGlyphHeight * 0.5f,
                leftWidth, static_cast<float>(text_renderer::kGlyphHeight)});
        if (!hasDetail) continue;
        const float rightWidth = portal.detailWord >= 0
            ? static_cast<float>(text_renderer::MeasureWidth(
                  words[portal.detailWord].bytes.size()))
            : 0.0f;
        const float secondWidth = portal.detailWord2 >= 0
            ? static_cast<float>(text_renderer::MeasureWidth(
                  words[portal.detailWord2].bytes.size()))
            : 0.0f;
        const float thirdWidth = portal.detailWord3 >= 0
            ? static_cast<float>(text_renderer::MeasureWidth(
                  words[portal.detailWord3].bytes.size()))
            : 0.0f;
        if (horizontal)
            {
            addTarget(portal.detailWord, {
                CenterX(portalRect) - rightWidth * 0.5f,
                portalRect.y + portalRect.height + 16, rightWidth,
                static_cast<float>(text_renderer::kGlyphHeight)});
            addTarget(portal.detailWord2, {
                CenterX(portalRect) - secondWidth * 0.5f,
                portalRect.y + portalRect.height + 16 +
                    text_renderer::kGlyphHeight + 6,
                secondWidth, static_cast<float>(text_renderer::kGlyphHeight)});
            addTarget(portal.detailWord3, {
                CenterX(portalRect) + secondWidth * 0.5f + 8.0f,
                portalRect.y + portalRect.height + 16 +
                    text_renderer::kGlyphHeight + 6,
                thirdWidth, static_cast<float>(text_renderer::kGlyphHeight)});
            }
        else
            {
            const float top = CenterY(portalRect) -
                text_renderer::kGlyphHeight - 3;
            addTarget(portal.detailWord, {
                portalRect.x + portalRect.width + 18, top,
                rightWidth, static_cast<float>(text_renderer::kGlyphHeight)});
            addTarget(portal.detailWord2, {
                portalRect.x + portalRect.width + 18,
                top + text_renderer::kGlyphHeight + 6,
                secondWidth, static_cast<float>(text_renderer::kGlyphHeight)});
            addTarget(portal.detailWord3, {
                portalRect.x + portalRect.width + 26 + secondWidth,
                top + text_renderer::kGlyphHeight + 6,
                thirdWidth, static_cast<float>(text_renderer::kGlyphHeight)});
            }
    }
}

void AppendShopTextBoxes() {
    RunNode* node = CurrentRunNode();
    if (!node || node->type != RunNodeType::Shop) return;
    const auto addWord = [](const std::string& id, float x, float y) {
        const auto found = wordIds.find(id);
        if (found == wordIds.end()) return;
        const float width = static_cast<float>(
            text_renderer::MeasureWidth(words[found->second].bytes.size()));
        textBoxes.push_back({{x, y, width,
            static_cast<float>(text_renderer::kGlyphHeight)}, found->second});
    };
    const auto addCenteredWord = [&](const std::string& id, float centerX,
                                      float y) {
        const auto found = wordIds.find(id);
        if (found == wordIds.end()) return;
        const float width = static_cast<float>(
            text_renderer::MeasureWidth(words[found->second].bytes.size()));
        addWord(id, centerX - width * 0.5f, y);
    };
    const auto nameId = [](const ShopOffer& offer) {
        if (offer.kind == ShopOfferKind::PrimaryWeapon) {
            if (offer.primaryWeapon == PrimaryWeapon::Railgun)
                return "shop_railgun";
            if (offer.primaryWeapon == PrimaryWeapon::Boomerang)
                return "shop_boomerang";
            return "shop_standard_shot";
        }
        if (offer.kind == ShopOfferKind::SecondaryWeapon) {
            if (offer.secondaryWeapon == SecondaryWeapon::HomingRocket)
                return "shop_homing_rocket";
            if (offer.secondaryWeapon == SecondaryWeapon::ContactBomb)
                return "shop_contact_bomb";
            return "shop_bomb";
        }
        switch (offer.upgrade) {
            case UpgradeType::MaxHealth: return "shop_max_health";
            case UpgradeType::MoveSpeed: return "shop_move_speed";
            case UpgradeType::FireRate: return "shop_fire_rate";
            case UpgradeType::ProjectileDamage: return "shop_shot_damage";
            case UpgradeType::BombCooldown: return "shop_bomb_rate";
            case UpgradeType::BombDamage: return "shop_bomb_damage";
            case UpgradeType::Invincibility: return "shop_invincibility";
            case UpgradeType::ExtraProjectile: return "shop_extra_projectile";
        }
        return "shop_max_health";
    };
    for (std::size_t index = 0; index < node->shopOffers.size(); ++index) {
        const Rect target = ShopOfferTarget(index);
        const Rect purchase = ShopOfferPurchaseArea(index);
        addWord(nameId(node->shopOffers[index]), target.x, target.y - 66);
        addWord("shop_cost_5", target.x, target.y - 40);
        const bool repeatable =
            node->shopOffers[index].kind == ShopOfferKind::Upgrade;
        if (repeatable || !node->shopOffers[index].purchased)
            addCenteredWord(
                "shop_stand_here", CenterX(purchase), purchase.y + 32);
    }
    const Rect purchase = ResetWordsPurchaseArea();
    addCenteredWord(
        "shop_cost_3", CenterX(purchase), purchase.y - 56.0f);
    addCenteredWord(
        "shop_reset_words", CenterX(purchase), purchase.y - 28.0f);
    addCenteredWord(
        "shop_stand_here", CenterX(purchase), purchase.y + 20.0f);
}

void RebuildGameplayTextBoxes() {
    const RunNode* node = CurrentRunNode();
    if (debugRoom || !node ||
        (node->type != RunNodeType::Interior &&
         node->type != RunNodeType::BossInterior &&
         node->type != RunNodeType::Boss))
        textBoxes.clear();
    else if (node->type == RunNodeType::Interior)
        BuildWorldTextBoxes();
    else
        textBoxes.clear();
    AppendPortalTextBoxes();
    AppendShopTextBoxes();
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
    postBossTuningRoom = false;
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
    lastPlayerRoom = -1;
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
        node.playerInteriorDoorway = -1;
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
        portal.active = node.completed || node.type == RunNodeType::Shop;
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
    if (node.type == RunNodeType::Interior && !node.portals.empty()) {
        RunPortal& spawnExit = node.portals.front();
        spawnExit.active = true;
        spawnExit.armed = false;
        spawnExit.sourceRoom = node.interiorEntryRoom;
        spawnExit.interiorTrigger =
            InteriorEntryPortalRect(node.interiorEntryRoom);
    }
    const bool allPlayerAlterations = std::all_of(
        playerInteriorState.permanent.begin(),
        playerInteriorState.permanent.end(),
        [](bool unlocked) { return unlocked; });
    if (node.type == RunNodeType::PlayerInterior &&
        allPlayerAlterations && !node.portals.empty()) {
        node.completed = true;
        const Room& spawn = rooms[
            node.playerInteriorSpawnRoom >= 0
                ? static_cast<std::size_t>(node.playerInteriorSpawnRoom) : 0];
        RunPortal& spawnExit = node.portals.front();
        spawnExit.active = true;
        spawnExit.armed = false;
        spawnExit.sourceRoom = node.playerInteriorSpawnRoom;
        spawnExit.interiorTrigger = {
            RoomX(spawn) + interior.roomSize * 0.5f - 36.0f,
            RoomY(spawn) + interior.roomSize * 0.5f - 36.0f,
            72.0f, 72.0f};
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
                UpgradeType::ProjectileDamage, UpgradeType::BombDamage,
                UpgradeType::MaxHealth};
            const std::uint32_t rotation = static_cast<std::uint32_t>(
                DeriveRunSeed(node.seed, 0x4f46464552ULL) % 7);
            for (std::uint32_t index = 0; index < 3; ++index) {
                ShopOffer offer;
                offer.id =
                    DeriveRunSeed(node.seed, 0x53484f50ULL, index);
                offer.upgrade = upgrades[(rotation + index) % 7];
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
    if (node.type == RunNodeType::Interior ||
        (node.type == RunNodeType::PlayerInterior &&
         allPlayerAlterations))
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
    lastPlayerRoom = -1;
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
        BuildPostBossPortals(*node);
        run.status = RunStatus::Active;
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
    constexpr std::uint32_t resetPrice = 3;
    if (run.currency < resetPrice ||
        !Overlaps(player, ResetWordsPurchaseArea())) {
        node->resetWordsPurchaseTimer = 0;
    } else {
        if (node->resetWordsPurchaseTimer <= 0)
            PlaySoundEffect(Sound::AimTick);
        node->resetWordsPurchaseTimer += dt;
        if (node->resetWordsPurchaseTimer >=
            rogueliteTuning.shopPurchaseSeconds) {
            run.currency -= resetPrice;
            ResetWordMutations();
            RebuildGameplayTextBoxes();
            node->resetWordsPurchaseTimer = 0;
            PlaySoundEffect(Sound::PowerUp);
        }
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

Rect ResetWordsPurchaseArea() {
    const Rect target = ResetWordsTarget();
    return {target.x + target.width + 20.0f, target.y, 120.0f, target.height};
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
        case UpgradeType::BombDamage: return "BOMB DAMAGE";
        case UpgradeType::MoveSpeed: return "MOVE SPEED";
        case UpgradeType::MaxHealth: return "MAX HEALTH";
        case UpgradeType::ProjectileDamage: return "SHOT DAMAGE";
        case UpgradeType::Invincibility: return "INVINCIBILITY AFTER HIT";
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
        return kPlayerSpeed +
            12.0f * UpgradeRank(UpgradeType::MoveSpeed);
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
    run.extraLifeAvailable = playerInteriorState.permanent[8];
    if (enterFirstNode) {
        RunNode* node = CurrentRunNode();
        if (node) ConfigureNode(*node);
    }
}

}  // namespace game

#include "gameplay.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>

#include "arena_level.h"
#include "audio.h"
#include "audio_level.h"
#include "gameplay_internal.h"
#include "glyph_level.h"
#include "roguelite.h"
#include "text_renderer.h"
#include "world.h"

namespace game {

bool pendingLevelSelection = false;
ArenaLevel runArena;
std::string legacyInteriorArchetype;
int legacyInteriorRoomSize = 0;
bool debugRoom = false;
bool postBossTuningRoom = false;
std::array<bool, 4> debugSpawnerOn{{false, false, false, false}};
std::array<float, 3> debugPickupTimers{{0, 0, 0}};

bool RunMode() {
    return run.status == RunStatus::Active ||
        run.status == RunStatus::Won;
}

bool RunArenaMode() {
    if (debugRoom || MainMenuActive()) return true;
    const RunNode* node = CurrentRunNode();
    return RunMode() && !run.mapActive && node &&
        node->type != RunNodeType::Interior &&
        node->type != RunNodeType::PlayerInterior &&
        node->type != RunNodeType::BossInterior;
}

bool PlayerInteriorMode() {
    const RunNode* node = CurrentRunNode();
    return RunMode() && !run.mapActive && node &&
        node->type == RunNodeType::PlayerInterior;
}

static bool EnemyIsSimulated(const Enemy& enemy) {
    const Rect rect = EnemyRect(enemy);
    return WithinSimulationRange(CenterX(rect), CenterY(rect));
}

static bool SpawnerIsSimulated(const Spawner& spawner) {
    return WithinSimulationRange(spawner.x + 15.0f, spawner.y + 15.0f);
}

bool DamagePlayer(int damage) {
    if (damage <= 0 || playerHealth <= 0 || playerInvincibility > 0)
        return false;
    playerHealth = std::max(0, playerHealth - damage);
    playerInvincibility = EffectiveInvincibilityDuration();
    PlaySoundEffect(Sound::HitHurt);
    return true;
}

bool HitSpecialControl(const Rect& shot) {
    if (MainMenuActive()) return false;
    if (HitPlayerAlteration(shot)) return true;
    RunNode* node = CurrentRunNode();
    if (!debugRoom && node && node->type == RunNodeType::Shop &&
        Overlaps(shot, ResetWordsTarget())) {
        ResetWordMutations();
        RebuildGameplayTextBoxes();
        return true;
    }
    return false;
}

void AwardSpawnerDeaths() {
    const RunNode* node = CurrentRunNode();
    if (node && node->type == RunNodeType::PlayerInterior) return;
    for (Spawner& spawner : spawners)
        if (spawner.health <= 0 && !spawner.rewardClaimed) {
            spawner.rewardClaimed = true;
            run.currency += static_cast<std::uint32_t>(
                SpawnerReward(spawner.enemyType));
        }
}

bool AcquireHomingTarget(float x, float y, float& targetX, float& targetY) {
    bool found = false;
    float best = std::numeric_limits<float>::max();
    constexpr float kHomingRange = 520.0f;
    constexpr float kHomingRangeSquared = kHomingRange * kHomingRange;
    const int activeRoom = RunArenaMode() ? -1 : CurrentRoom();
    for (const Enemy& enemy : enemies) {
        if (enemy.health <= 0 || !EnemyIsSimulated(enemy) ||
            (activeRoom >= 0 && enemy.room != activeRoom))
            continue;
        const Rect rect = EnemyRect(enemy);
        const float candidateX = CenterX(rect);
        const float candidateY = CenterY(rect);
        const float dx = candidateX - x;
        const float dy = candidateY - y;
        const float distance = dx * dx + dy * dy;
        if (distance > kHomingRangeSquared) continue;
        if (distance < best) {
            best = distance;
            targetX = candidateX;
            targetY = candidateY;
            found = true;
        }
    }
    for (const Spawner& spawner : spawners) {
        if (spawner.health <= 0 || !SpawnerIsSimulated(spawner) ||
            (activeRoom >= 0 && spawner.room != activeRoom))
            continue;
        const float candidateX = spawner.x + 15.0f;
        const float candidateY = spawner.y + 15.0f;
        const float dx = candidateX - x;
        const float dy = candidateY - y;
        const float distance = dx * dx + dy * dy;
        if (distance > kHomingRangeSquared) continue;
        if (distance < best) {
            best = distance;
            targetX = candidateX;
            targetY = candidateY;
            found = true;
        }
    }
    return found;
}

void HomeProjectile(Projectile& projectile) {
    if (!projectile.homing) return;
    float targetX = 0, targetY = 0;
    if (!AcquireHomingTarget(
            projectile.x, projectile.y, targetX, targetY))
        return;
    float dx = targetX - projectile.x;
    float dy = targetY - projectile.y;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length > 0.01f) {
        targetX += -dy / length * projectile.homingLateralOffset;
        targetY += dx / length * projectile.homingLateralOffset;
        dx = targetX - projectile.x;
        dy = targetY - projectile.y;
        length = std::sqrt(dx * dx + dy * dy);
    }
    const float speed =
        std::sqrt(projectile.vx * projectile.vx + projectile.vy * projectile.vy);
    if (length > 0.01f) {
        projectile.vx = dx / length * speed;
        projectile.vy = dy / length * speed;
    }
}

void HomeBomb(Bomb& bomb) {
    if (!bomb.homing) return;
    float targetX = 0, targetY = 0;
    if (!AcquireHomingTarget(bomb.x, bomb.y, targetX, targetY))
        return;
    float dx = targetX - bomb.x;
    float dy = targetY - bomb.y;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length > 0.01f) {
        targetX += -dy / length * bomb.homingLateralOffset;
        targetY += dx / length * bomb.homingLateralOffset;
        dx = targetX - bomb.x;
        dy = targetY - bomb.y;
        length = std::sqrt(dx * dx + dy * dy);
    }
    const float speed = std::sqrt(bomb.vx * bomb.vx + bomb.vy * bomb.vy);
    if (length > 0.01f) {
        bomb.vx = dx / length * speed;
        bomb.vy = dy / length * speed;
    }
}

Rect InteriorOrganExitTrigger(const RunNode& node, int roomIndex) {
    if (roomIndex < 0 || roomIndex >= static_cast<int>(rooms.size()))
        return {};
    const Room& room = rooms[roomIndex];
    constexpr float size = 72.0f;
    const float margin = std::max(70.0f, interior.roomSize * 0.1f);
    const std::array<Rect, 4> corners{{
        {RoomX(room) + margin, RoomY(room) + margin, size, size},
        {RoomX(room) + interior.roomSize - margin - size,
         RoomY(room) + margin, size, size},
        {RoomX(room) + interior.roomSize - margin - size,
         RoomY(room) + interior.roomSize - margin - size, size, size},
        {RoomX(room) + margin,
         RoomY(room) + interior.roomSize - margin - size, size, size},
    }};
    return corners[node.portals.size() % corners.size()];
}

void UnlockInteriorPortals() {
    RunNode* node = CurrentRunNode();
    if (!node || node->type != RunNodeType::Interior)
        return;
    const int organRoom = CurrentRoom();
    if (organRoom >= 0 && !node->portals.empty()) {
        if (!node->completed) {
            RunPortal& portal = node->portals.front();
            portal.active = true;
            portal.armed = false;
            portal.interiorTrigger = InteriorOrganExitTrigger(*node, organRoom);
        } else {
            RunPortal portal = node->portals.front();
            portal.destination = kInvalidRunNode;
            portal.active = true;
            portal.armed = false;
            node->portals.push_back(portal);
            node->portals.back().interiorTrigger =
                InteriorOrganExitTrigger(*node, organRoom);
        }
    }
    node->completed = true;
    BuildInteriorArenaDestinations(*node);
    RebuildGameplayTextBoxes();
}

bool DamageTextBox(
    TextBox& box, float impactX, int* organEdits = nullptr) {
    if (box.levelValue) {
        levelNumber = std::max(0, levelNumber - 1);
        if (levelValueWord >= 0) {
            const std::string value = std::to_string(levelNumber);
            words[levelValueWord].bytes.assign(
                value.begin(), value.end());
        }
        return true;
    }
    if (box.value && box.organ >= 0) {
        Organ& organ = organs[box.organ];
        EnemyDifficultyStat stat;
        if (!OrganDifficultyStat(organ.id, stat)) return false;
        std::uint32_t& stage =
            MutableEnemyStage(interior.archetype, stat);
        if (stage == 0) return false;
        --stage;
        PlaySoundEffect(Sound::ValueLowered);
        if (organEdits) ++*organEdits;
        organ.value = DisplayEnemyOrganValue(
            types.at(interior.archetype), stat, stage);
        UpdateValueWord(organ);
    } else if (box.word >= 0 && !words[box.word].bytes.empty()) {
        const int character = std::clamp(
            static_cast<int>(
                (impactX - box.rect.x) /
                text_renderer::kGlyphAdvance),
            0, static_cast<int>(words[box.word].bytes.size()) - 1);
        std::uint8_t& value = words[box.word].bytes[character];
        value = static_cast<std::uint8_t>(
            std::max(0, static_cast<int>(value) - 1));
    }
    return false;
}

bool HitText(Projectile& projectile) {
    const Rect shot{
        projectile.x, projectile.y, projectile.width, projectile.width};
    for (TextBox& box : textBoxes) {
        if (!Overlaps(shot, box.rect)) continue;
        int organEdits = 0;
        const bool changedLevel =
            DamageTextBox(box, CenterX(shot), &organEdits);
        const bool startGame = MainMenuActive() && box.startGame;
        SaveMutations();
        if (startGame) TriggerMainMenuStart();
        for (int edit = 0; edit < organEdits; ++edit)
            UnlockInteriorPortals();
        if (changedLevel)
            pendingLevelSelection = true;
        else if (!MainMenuActive())
            RebuildGameplayTextBoxes();
        return true;
    }
    return false;
}

bool HitShield(const Rect& shot, int damage) {
    for (ShieldBlock& shield : shieldBlocks)
        if (shield.health > 0 && Overlaps(shot, shield.rect)) {
            RunNode* node = CurrentRunNode();
            if (node && node->type == RunNodeType::PlayerInterior &&
                !playerInteriorState.permanent[0] && shield.organ >= 0)
                return true;
            if (node && node->type == RunNodeType::PlayerInterior &&
                shield.organ >= 0 && node->playerInteriorRoom >= 0 &&
                shield.organ != node->playerInteriorRoom)
                return true;
            shield.health = std::max(0, shield.health - damage);
            if (shield.health == 0 && node &&
                node->type == RunNodeType::PlayerInterior &&
                !node->playerInteriorWave &&
                shield.organ >= 0) {
                node->playerInteriorRoom = shield.organ;
                playerInteriorState.brokenDoorways[shield.organ] = true;
                SaveMutations();
            }
            PlaySoundEffect(Sound::HitEnemy);
            return true;
        }
    return false;
}

bool HitSpawner(const Rect& shot, int damage) {
    for (Spawner& spawner : spawners)
        if (spawner.health > 0 && SpawnerIsSimulated(spawner) &&
            Overlaps(shot, {spawner.x, spawner.y, 30, 30})) {
            spawner.health -= damage;
            PlaySoundEffect(
                spawner.health <= 0 ? Sound::SpawnerDeath : Sound::SpawnerHit);
            return true;
        }
    return false;
}

bool HitEnemy(const Rect& shot, int damage) {
    for (Enemy& enemy : enemies)
        if (enemy.health > 0 && EnemyIsSimulated(enemy) &&
            EnemyVisualOverlaps(enemy, shot)) {
            enemy.health -= damage;
            PlaySoundEffect(Sound::HitEnemy);
            return true;
        }
    return false;
}

bool HitRoomWall(const Rect& shot) {
    if (RunArenaMode()) return HitArenaWallMotif(runArena, shot);
    int room = -1;
    if (!HitsWall(shot, &room)) return false;
    if (room < 0) return false;
    Room& value = rooms[room];
    Pixel& pixel =
        types.at(interior.archetype).sprite[value.pixelRow][value.pixelColumn];
    const float localX = std::clamp(
        CenterX(shot) - RoomX(value), 0.0f,
        static_cast<float>(interior.roomSize) - 0.01f);
    const int channel = std::clamp(
        static_cast<int>(localX * 3 / interior.roomSize), 0, 2);
    pixel.rgb[channel] =
        std::max(0, pixel.rgb[channel] - kWallChannelDamage);
    SaveMutations();
    return true;
}

bool HitMutableGeometry(const Rect& shot) {
    if (currentMap == "audio" && HitAudioGeometry(shot)) return true;
    if (currentMap == "glyph" && HitGlyphGeometry(shot)) {
        SaveMutations();
        return true;
    }
    return HitRoomWall(shot);
}

void DetonateBomb(float x, float y, int damage, float radius) {
    PlaySoundEffect(Sound::Explosion);
    for (Enemy& enemy : enemies) {
        if (EnemyIsSimulated(enemy) &&
            EnemyVisualWithinRadius(enemy, x, y, radius))
            enemy.health -= damage;
    }
    const float radiusSquared = radius * radius;
    if (BossFightMode() && PointInBoss(x, y))
        HitBoss({x - 1, y - 1, 2, 2}, damage);
    for (BossTurret& turret : run.boss.turrets) {
        if (!turret.alive || !BossInteriorMode()) continue;
        const Rect target = BossTurretTargetRect(turret);
        const float dx = x - std::clamp(
            x, target.x, target.x + target.width);
        const float dy = y - std::clamp(
            y, target.y, target.y + target.height);
        if (dx * dx + dy * dy <= radiusSquared) {
            HitBossTurretTarget(target, damage);
        }
    }
    for (Spawner& spawner : spawners) {
        if (!SpawnerIsSimulated(spawner)) continue;
        const Rect rect{spawner.x, spawner.y, 30, 30};
        const float dx = x - std::clamp(x, rect.x, rect.x + rect.width);
        const float dy = y - std::clamp(y, rect.y, rect.y + rect.height);
        if (spawner.health > 0 &&
            dx * dx + dy * dy <= radiusSquared) {
            spawner.health -= damage;
            PlaySoundEffect(
                spawner.health <= 0 ? Sound::SpawnerDeath : Sound::SpawnerHit);
        }
    }
    for (ShieldBlock& shield : shieldBlocks) {
        RunNode* node = CurrentRunNode();
        if (node && node->type == RunNodeType::PlayerInterior &&
            !playerInteriorState.permanent[0] && shield.organ >= 0)
            continue;
        if (node && node->type == RunNodeType::PlayerInterior &&
            shield.organ >= 0 && node->playerInteriorRoom >= 0 &&
            shield.organ != node->playerInteriorRoom)
            continue;
        const float dx = x - std::clamp(
            x, shield.rect.x, shield.rect.x + shield.rect.width);
        const float dy = y - std::clamp(
            y, shield.rect.y, shield.rect.y + shield.rect.height);
        if (shield.health > 0 &&
            dx * dx + dy * dy <= radiusSquared) {
            shield.health = std::max(0, shield.health - damage);
            if (shield.health == 0 && node &&
                node->type == RunNodeType::PlayerInterior &&
                !node->playerInteriorWave &&
                shield.organ >= 0) {
                node->playerInteriorRoom = shield.organ;
                playerInteriorState.brokenDoorways[shield.organ] = true;
                SaveMutations();
            }
        }
    }
    bool changedText = false;
    bool changedLevel = false;
    int organEdits = 0;
    bool startGame = false;
    for (TextBox& text : textBoxes) {
        const float dx = x - std::clamp(
            x, text.rect.x, text.rect.x + text.rect.width);
        const float dy = y - std::clamp(
            y, text.rect.y, text.rect.y + text.rect.height);
        if (dx * dx + dy * dy > radiusSquared) continue;
        changedLevel =
            DamageTextBox(text, x, &organEdits) || changedLevel;
        startGame = startGame ||
            (MainMenuActive() && text.startGame);
        changedText = true;
    }
    if (changedText) {
        SaveMutations();
        if (startGame) TriggerMainMenuStart();
        for (int edit = 0; edit < organEdits; ++edit)
            UnlockInteriorPortals();
        if (changedLevel)
            pendingLevelSelection = true;
        else if (!MainMenuActive())
            RebuildGameplayTextBoxes();
    }
    // Probe the blast disk, but apply at most one mutable-geometry hit per
    // explosion. This keeps a rocket's wall impact equivalent to one shot and
    // performs only one persistence/cache update.
    const float spacing = std::max(6.0f, radius * 0.25f);
    bool damagedGeometry = false;
    for (float offsetY = -radius; offsetY <= radius; offsetY += spacing)
        for (float offsetX = -radius;
             offsetX <= radius && !damagedGeometry; offsetX += spacing)
            if (offsetX * offsetX + offsetY * offsetY <= radiusSquared)
                damagedGeometry = HitMutableGeometry(
                    {x + offsetX - 1.0f, y + offsetY - 1.0f, 2, 2});
    explosions.push_back({x, y, kExplosionDuration});
}

bool BombObstacle(const Rect& bomb) {
    if (RunWall(bomb) || HitsWall(bomb) || HitsShield(bomb)) return true;
    for (const Spawner& spawner : spawners)
        if (spawner.health > 0 && SpawnerIsSimulated(spawner) &&
            Overlaps(bomb, {spawner.x, spawner.y, 30, 30}))
            return true;
    for (const TextBox& text : textBoxes)
        if (Overlaps(bomb, text.rect)) return true;
    return false;
}

void SpawnBurst(Spawner& spawner) {
    const EnemyType& type = types.at(spawner.enemyType);
    const int burstStage = static_cast<int>(EnemyStage(
        spawner.enemyType, EnemyDifficultyStat::Burst));
    const int burstMin = type.burstMin + burstStage;
    const int burstMax = type.burstMax + burstStage;
    const int childCapacity = type.childCapacity + static_cast<int>(
        EnemyStage(spawner.enemyType, EnemyDifficultyStat::ChildCapacity));
    spawner.maxActiveChildren =
        std::max(1, burstMax) * (std::max(1, childCapacity) + 1);
    const int activeChildren = static_cast<int>(std::count_if(
        enemies.begin(), enemies.end(),
        [&](const Enemy& enemy) {
            return enemy.health > 0 && enemy.spawnerId == spawner.id;
        }));
    const int available =
        std::max(0, spawner.maxActiveChildren - activeChildren);
    const int count = std::min(
        available, RandomInt(burstMin, burstMax));
    if (count <= 0) return;
    const int health = type.maxHealth + static_cast<int>(EnemyStage(
        spawner.enemyType, EnemyDifficultyStat::Health));
    for (int i = 0; i < count; ++i) {
        const float angle = RandomFloat(0, kPi * 2);
        const float distance = 38.0f + (i % 3) * 12.0f;
        Rect candidate{
            spawner.x + 15 + std::cos(angle) * distance,
            spawner.y + 15 + std::sin(angle) * distance, 1, 1};
        Enemy preview{
            spawner.room, spawner.enemyType,
            candidate.x, candidate.y, health, health,
            kEnemyFadeDuration};
        preview.spawnerId = spawner.id;
        const Rect previewRect = EnemyRect(preview);
        preview.facing = std::atan2(
            playerY + kPlayerSize * 0.5f - CenterY(previewRect),
            playerX + kPlayerSize * 0.5f - CenterX(previewRect)) +
            kPi * 0.5f;
        if (EnemyVisualFitsNetwork(preview, candidate.x, candidate.y))
            enemies.push_back(preview);
    }
}

std::vector<int> RoomDistancesFrom(int targetRoom) {
    std::vector<int> distances(rooms.size(), -1);
    if (targetRoom < 0 ||
        targetRoom >= static_cast<int>(rooms.size()))
        return distances;
    std::queue<int> pending;
    distances[targetRoom] = 0;
    pending.push(targetRoom);
    const int dr[4]{-1, 1, 0, 0};
    const int dc[4]{0, 0, -1, 1};
    while (!pending.empty()) {
        const int current = pending.front();
        pending.pop();
        for (int direction = 0; direction < 4; ++direction) {
            const int next = RoomIndexAt(
                rooms[current].row + dr[direction],
                rooms[current].column + dc[direction]);
            if (next >= 0 && RoomsConnected(current, next) &&
                distances[next] < 0) {
                distances[next] = distances[current] + 1;
                pending.push(next);
            }
        }
    }
    return distances;
}

int NextRoomToward(int room, const std::vector<int>& distances) {
    if (room < 0 || room >= static_cast<int>(rooms.size())) return -1;
    int best = room;
    const int dr[4]{-1, 1, 0, 0};
    const int dc[4]{0, 0, -1, 1};
    for (int direction = 0; direction < 4; ++direction) {
        const int next = RoomIndexAt(
            rooms[room].row + dr[direction],
            rooms[room].column + dc[direction]);
        if (next >= 0 && RoomsConnected(room, next) &&
            distances[next] >= 0 &&
            (distances[best] < 0 || distances[next] < distances[best]))
            best = next;
    }
    return best;
}

void RoomExitTarget(
    const Enemy& enemy, int nextRoom, float& targetX, float& targetY) {
    if (enemy.room < 0 || nextRoom < 0 ||
        enemy.room >= static_cast<int>(rooms.size()) ||
        nextRoom >= static_cast<int>(rooms.size()))
        return;
    const Room& current = rooms[enemy.room];
    const Room& next = rooms[nextRoom];
    const Rect enemyRect = EnemyRect(enemy);
    targetX = RoomX(current) + interior.roomSize * 0.5f;
    targetY = RoomY(current) + interior.roomSize * 0.5f;
    if (next.column > current.column)
        targetX = RoomX(next) + kWall + enemyRect.width * 0.5f;
    else if (next.column < current.column)
        targetX = RoomX(current) - kWall - enemyRect.width * 0.5f;
    else if (next.row > current.row)
        targetY = RoomY(next) + kWall + enemyRect.height * 0.5f;
    else if (next.row < current.row)
        targetY = RoomY(current) - kWall - enemyRect.height * 0.5f;
}

float EnemyMovementSpeed(const Enemy& enemy) {
    const EnemyType& type = types.at(enemy.type);
    return (type.speed + static_cast<float>(EnemyStage(
        enemy.type, EnemyDifficultyStat::Speed))) *
        10.0f * interior.speedUnit;
}

void FacePoint(Enemy& enemy, float x, float y) {
    const Rect rect = EnemyRect(enemy);
    enemy.facing = std::atan2(
        y - CenterY(rect), x - CenterX(rect)) + kPi * 0.5f;
}

void MoveEnemyToward(
    Enemy& enemy, float targetX, float targetY, float speed, float dt) {
    const Rect current = EnemyRect(enemy);
    const float dx = targetX - CenterX(current);
    const float dy = targetY - CenterY(current);
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance <= 0.01f) return;
    const float step = std::min(distance, speed * dt);
    const float moveX = dx / distance * step;
    const float moveY = dy / distance * step;
    const Rect nextX = EnemyRect(enemy);
    const bool fitsX = RunArenaMode()
        ? ArenaAllowsPlayer(runArena, {
              nextX.x + moveX, nextX.y, nextX.width, nextX.height})
        : EnemyVisualFitsNetwork(enemy, enemy.x + moveX, enemy.y);
    if (fitsX)
        enemy.x += moveX;
    const Rect nextY = EnemyRect(enemy);
    const bool fitsY = RunArenaMode()
        ? ArenaAllowsPlayer(runArena, {
              nextY.x, nextY.y + moveY, nextY.width, nextY.height})
        : EnemyVisualFitsNetwork(enemy, enemy.x, enemy.y + moveY);
    if (fitsY)
        enemy.y += moveY;
}

void MoveEnemyAway(
    Enemy& enemy, float targetX, float targetY, float speed, float dt) {
    const Rect current = EnemyRect(enemy);
    MoveEnemyToward(
        enemy, CenterX(current) * 2.0f - targetX,
        CenterY(current) * 2.0f - targetY, speed, dt);
}

void BeginRecovery(Enemy& enemy, const EnemyType& type) {
    enemy.phase = EnemyPhase::Recover;
    enemy.phaseTimer = type.attackCooldown;
    enemy.attackRemaining = 0;
}

void FireRail(Enemy& enemy, const EnemyType& type) {
    const Rect source = EnemyRect(enemy);
    const float startX = CenterX(source);
    const float startY = CenterY(source);
    float dx = enemy.targetX - startX;
    float dy = enemy.targetY - startY;
    const float directionLength = std::sqrt(dx * dx + dy * dy);
    if (directionLength <= 0.01f) return;
    dx /= directionLength;
    dy /= directionLength;
    const float width = 6.0f;
    const float maximumLength = type.attackDistance;
    const Rect player{playerX, playerY, kPlayerSize, kPlayerSize};
    float railLength = 0;
    bool hitPlayer = false;
    for (float distance = 3.0f;
         distance <= maximumLength; distance += 3.0f) {
        const Rect probe{
            startX + dx * distance - width * 0.5f,
            startY + dy * distance - width * 0.5f, width, width};
        if (RunWall(probe) ||
            (!RunArenaMode() && HitsWall(probe)) ||
            HitsShield(probe))
            break;
        railLength = distance;
        if (!hitPlayer && Overlaps(probe, player)) {
            DamagePlayer(type.contactDamage + static_cast<int>(
                EnemyStage(enemy.type, EnemyDifficultyStat::Damage)));
            hitPlayer = true;
        }
    }
    enemyRails.push_back(
        {startX, startY, dx, dy, railLength, width,
         kRailFlashDuration});
    PlaySoundEffect(Sound::RailgunShot);
}

void UpdateShooter(
    Enemy& enemy, const EnemyType& type, int activeRoom,
    const std::vector<int>& roomDistances, float dt) {
    if (enemy.phase == EnemyPhase::Recover) {
        enemy.phaseTimer -= dt;
        if (enemy.phaseTimer <= 0) enemy.phase = EnemyPhase::Approach;
        return;
    }
    if (enemy.phase == EnemyPhase::Windup) {
        if (enemy.phaseTimer > type.aimLockSeconds) {
            enemy.targetX = playerX + kPlayerSize * 0.5f;
            enemy.targetY = playerY + kPlayerSize * 0.5f;
        }
        FacePoint(enemy, enemy.targetX, enemy.targetY);
        constexpr float aimTickSeconds = 0.2f;
        const int previousTick =
            static_cast<int>(std::ceil(enemy.phaseTimer / aimTickSeconds));
        enemy.phaseTimer -= dt;
        const int currentTick = static_cast<int>(
            std::ceil(std::max(0.0f, enemy.phaseTimer) / aimTickSeconds));
        if (currentTick < previousTick && enemy.phaseTimer > 0)
            PlaySoundEffect(Sound::AimTick);
        if (enemy.phaseTimer <= 0) {
            FireRail(enemy, type);
            BeginRecovery(enemy, type);
        }
        return;
    }

    float targetX = playerX + kPlayerSize * 0.5f;
    float targetY = playerY + kPlayerSize * 0.5f;
    if (!RunArenaMode() && enemy.room != activeRoom) {
        const int nextRoom =
            NextRoomToward(enemy.room, roomDistances);
        if (nextRoom >= 0) {
            RoomExitTarget(enemy, nextRoom, targetX, targetY);
        }
        FacePoint(enemy, targetX, targetY);
        MoveEnemyToward(
            enemy, targetX, targetY, EnemyMovementSpeed(enemy), dt);
        return;
    }

    const Rect current = EnemyRect(enemy);
    const float dx = targetX - CenterX(current);
    const float dy = targetY - CenterY(current);
    const float distance = std::sqrt(dx * dx + dy * dy);
    FacePoint(enemy, targetX, targetY);
    constexpr float rangeSlack = 30.0f;
    if (distance > type.preferredDistance + rangeSlack)
        MoveEnemyToward(
            enemy, targetX, targetY, EnemyMovementSpeed(enemy), dt);
    else if (distance < type.preferredDistance - rangeSlack)
        MoveEnemyAway(
            enemy, targetX, targetY, EnemyMovementSpeed(enemy), dt);
    else {
        enemy.phase = EnemyPhase::Windup;
        enemy.phaseTimer = type.windupSeconds;
        enemy.targetX = targetX;
        enemy.targetY = targetY;
        PlaySoundEffect(Sound::AimTick);
    }
}

void UpdateCharger(
    Enemy& enemy, const EnemyType& type, int activeRoom,
    const std::vector<int>& roomDistances, float dt) {
    if (enemy.phase == EnemyPhase::Recover) {
        enemy.phaseTimer -= dt;
        if (enemy.phaseTimer <= 0) enemy.phase = EnemyPhase::Approach;
        return;
    }
    if (enemy.phase == EnemyPhase::Windup) {
        FacePoint(enemy, enemy.targetX, enemy.targetY);
        enemy.phaseTimer -= dt;
        if (enemy.phaseTimer <= 0) {
            const Rect current = EnemyRect(enemy);
            float dx = enemy.targetX - CenterX(current);
            float dy = enemy.targetY - CenterY(current);
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length <= 0.01f) {
                BeginRecovery(enemy, type);
                return;
            }
            enemy.attackX = dx / length;
            enemy.attackY = dy / length;
            enemy.attackRemaining = type.attackDistance;
            enemy.phase = EnemyPhase::Attack;
            PlaySoundEffect(Sound::ChargerGo);
        }
        return;
    }
    if (enemy.phase == EnemyPhase::Attack) {
        const float travel = std::min(
            enemy.attackRemaining, type.attackSpeed * dt);
        const int steps =
            std::max(1, static_cast<int>(std::ceil(travel / 4.0f)));
        const float step = travel / steps;
        for (int index = 0; index < steps; ++index) {
            const float nextX = enemy.x + enemy.attackX * step;
            const float nextY = enemy.y + enemy.attackY * step;
            if (!EnemyVisualFitsNetwork(enemy, nextX, nextY)) {
                BeginRecovery(enemy, type);
                return;
            }
            enemy.x = nextX;
            enemy.y = nextY;
            enemy.attackRemaining -= step;
        }
        if (enemy.attackRemaining <= 0) BeginRecovery(enemy, type);
        return;
    }

    float targetX = playerX + kPlayerSize * 0.5f;
    float targetY = playerY + kPlayerSize * 0.5f;
    if (!RunArenaMode() && enemy.room != activeRoom) {
        const int nextRoom =
            NextRoomToward(enemy.room, roomDistances);
        if (nextRoom >= 0) {
            RoomExitTarget(enemy, nextRoom, targetX, targetY);
        }
    }
    const Rect current = EnemyRect(enemy);
    const float dx = targetX - CenterX(current);
    const float dy = targetY - CenterY(current);
    const float distance = std::sqrt(dx * dx + dy * dy);
    FacePoint(enemy, targetX, targetY);
    if (enemy.room == activeRoom && distance <= type.attackRange) {
        enemy.phase = EnemyPhase::Windup;
        enemy.phaseTimer = type.windupSeconds;
        enemy.targetX = targetX;
        enemy.targetY = targetY;
        PlaySoundEffect(Sound::ChargerChargeUp);
    } else {
        MoveEnemyToward(
            enemy, targetX, targetY, EnemyMovementSpeed(enemy), dt);
    }
}

const ArenaLevel& ActiveRunArena() { return runArena; }

bool DebugRoomActive() { return debugRoom; }
bool PostBossTuningRoomActive() { return postBossTuningRoom; }

Rect ExitPortalRect() { return PhysicalExitPortalRect(); }

bool AimDirection(float& dx, float& dy) {
    const float cx = playerX + kPlayerSize * 0.5f;
    const float cy = playerY + kPlayerSize * 0.5f;
    dx = AimWorldX() - cx;
    dy = AimWorldY() - cy;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.01f) return false;
    dx /= length;
    dy /= length;
    return true;
}

void LockRailAim() {
    float dx = 0, dy = 0;
    if (AimDirection(dx, dy)) {
        run.railAimX = dx;
        run.railAimY = dy;
    }
}

void EmitProjectileWeapon(
    const WeaponStats& weapon, float baseDx, float baseDy) {
    const float direction = std::atan2(baseDy, baseDx);
    const float cx = playerX + kPlayerSize * 0.5f;
    const float cy = playerY + kPlayerSize * 0.5f;
    for (int index = 0; index < weapon.count; ++index) {
        const float offset = weapon.count == 1 ? 0.0f :
            (index - (weapon.count - 1) * 0.5f) * weapon.spread;
        for (int duplicate = 0;
             duplicate < weapon.projectilesPerShot; ++duplicate) {
            Projectile projectile{
                cx, cy, std::cos(direction + offset) * weapon.speed,
                std::sin(direction + offset) * weapon.speed, 0,
                weapon.homing, weapon.explosive, weapon.damage};
            projectile.maxDistance = weapon.range;
            projectile.explosionRadius = weapon.radius;
            projectile.width = weapon.width;
            projectile.activationDelay = duplicate * 0.045f;
            projectile.homingLateralOffset = offset * 180.0f;
            projectile.boomerang = weapon.boomerang;
            projectile.boomerangReleaseSpeed = weapon.speed;
            projectiles.push_back(projectile);
        }
    }
}

void EmitBombWeapon(
    const WeaponStats& weapon, float baseDx, float baseDy) {
    const float direction = std::atan2(baseDy, baseDx);
    const float cx = playerX + kPlayerSize * 0.5f;
    const float cy = playerY + kPlayerSize * 0.5f;
    for (int index = 0; index < weapon.count; ++index) {
        const float offset = weapon.count == 1 ? 0.0f :
            (index - (weapon.count - 1) * 0.5f) * weapon.spread;
        for (int duplicate = 0;
             duplicate < weapon.projectilesPerShot; ++duplicate) {
            Bomb bomb{
                cx, cy, std::cos(direction + offset) * weapon.speed,
                std::sin(direction + offset) * weapon.speed, weapon.range,
                weapon.homing, weapon.damage, weapon.radius};
            bomb.homingLateralOffset = offset * 180.0f;
            bomb.activationDelay = duplicate * 0.045f;
            bomb.contact = weapon.contact;
            bombs.push_back(bomb);
        }
    }
}

void ShootToward(int mouseX, int mouseY) {
    (void)mouseX;
    (void)mouseY;
    if (playerHealth <= 0 || run.mapActive ||
        (currentMap != "interior" && currentMap != "audio" &&
         currentMap != "glyph"))
        return;
    float dx = 0, dy = 0;
    if (!AimDirection(dx, dy)) return;
    const bool allPrimaries = playerInteriorState.permanent[4];
    if ((allPrimaries && run.primaryWeapons[
            static_cast<int>(PrimaryWeapon::Standard)]) ||
        (!allPrimaries && run.primaryWeapon == PrimaryWeapon::Standard))
        EmitProjectileWeapon(ResolvePlayerWeapon("standard"), dx, dy);
    if ((allPrimaries && run.primaryWeapons[
            static_cast<int>(PrimaryWeapon::Boomerang)]) ||
        (!allPrimaries && run.primaryWeapon == PrimaryWeapon::Boomerang))
        EmitProjectileWeapon(ResolvePlayerWeapon("boomerang"), dx, dy);
    PlaySoundEffect(Sound::LaserShoot);
}

void LaunchBomb(int mouseX, int mouseY) {
    (void)mouseX;
    (void)mouseY;
    if (bombCooldown > 0 || playerHealth <= 0 || run.mapActive ||
        (currentMap != "interior" && currentMap != "glyph"))
        return;
    float dx = 0, dy = 0;
    if (!AimDirection(dx, dy)) return;
    const WeaponStats weapon = ResolvePlayerWeapon("bomb");
    EmitBombWeapon(weapon, dx, dy);
    bombCooldown = weapon.cadence;
}

void AcquireRailTarget(float startX, float startY, float& dx, float& dy) {
    float targetX = 0, targetY = 0;
    if (!AcquireHomingTarget(startX, startY, targetX, targetY))
        return;
    const float candidateX = targetX - startX;
    const float candidateY = targetY - startY;
    const float length = std::sqrt(
        candidateX * candidateX + candidateY * candidateY);
    if (length > 0.01f) {
        const float targetDx = candidateX / length;
        const float targetDy = candidateY / length;
        // Rail homing may assist an aimed beam, but never redirect it toward
        // a target far from the cursor direction.
        if (dx * targetDx + dy * targetDy < 0.8f) return;
        dx = targetDx;
        dy = targetDy;
    }
}

bool FirePlayerRail() {
    if (playerHealth <= 0 || run.mapActive) return false;
    const WeaponStats weapon = ResolvePlayerWeapon("railgun");
    const float startX = playerX + kPlayerSize * 0.5f;
    const float startY = playerY + kPlayerSize * 0.5f;
    const float baseDirection = std::atan2(run.railAimY, run.railAimX);
    bool emitted = false;
    for (int beam = 0; beam < weapon.count; ++beam) {
        const float offset = weapon.count == 1 ? 0.0f :
            (beam - (weapon.count - 1) * 0.5f) * weapon.spread;
        for (int duplicate = 0;
             duplicate < weapon.projectilesPerShot; ++duplicate) {
        float dx = std::cos(baseDirection + offset);
        float dy = std::sin(baseDirection + offset);
        if (weapon.homing) AcquireRailTarget(startX, startY, dx, dy);
        std::vector<bool> hitEnemies(enemies.size(), false);
        std::vector<bool> hitSpawners(spawners.size(), false);
        std::vector<bool> hitTurrets(run.boss.turrets.size(), false);
        bool hitBossBody = false;
        bool hitSpecial = false;
        bool hitShop = false;
        float railLength = 0;
        for (float distance = kPlayerSize * 0.5f + 1.0f;
             distance <= weapon.range; distance += 3.0f) {
            const float x = startX + dx * distance;
            const float y = startY + dy * distance;
            const Rect wallProbe{x - 1, y - 1, 2, 2};
            if (RunWall(wallProbe) ||
                (!RunArenaMode() && HitsWall(wallProbe))) {
                break;
            }
            const Rect hitProbe{
                x - weapon.width * 0.5f, y - weapon.width * 0.5f,
                weapon.width, weapon.width};
            railLength = distance;
            Projectile railProbe{
                hitProbe.x, hitProbe.y, 0, 0, 0, false, false,
                weapon.damage};
            railProbe.width = weapon.width;
            if (HitText(railProbe)) break;
            if (currentMap == "audio" && HitAudioGeometry(hitProbe))
                break;
            if (currentMap == "glyph" && HitGlyphGeometry(hitProbe)) {
                SaveMutations();
                break;
            }
            if (HitShield(hitProbe, weapon.damage)) break;
            if (!hitSpecial) hitSpecial = HitSpecialControl(hitProbe);
            if (!hitShop) hitShop = HitShopOffer(hitProbe);
            for (std::size_t index = 0;
                 index < run.boss.turrets.size(); ++index)
                if (!hitTurrets[index] && run.boss.turrets[index].alive &&
                    Overlaps(
                        hitProbe,
                        BossTurretTargetRect(run.boss.turrets[index]))) {
                    HitBossTurretTarget(hitProbe, weapon.damage);
                    hitTurrets[index] = true;
                }
            if (!hitBossBody && HitBoss(hitProbe, weapon.damage))
                hitBossBody = true;
            for (std::size_t index = 0; index < enemies.size(); ++index)
                if (!hitEnemies[index] && enemies[index].health > 0 &&
                    EnemyVisualOverlaps(enemies[index], hitProbe)) {
                    enemies[index].health -= weapon.damage;
                    hitEnemies[index] = true;
                }
            for (std::size_t index = 0; index < spawners.size(); ++index)
                if (!hitSpawners[index] && spawners[index].health > 0 &&
                    Overlaps(
                        hitProbe,
                        {spawners[index].x, spawners[index].y, 30, 30})) {
                    spawners[index].health -= weapon.damage;
                    PlaySoundEffect(spawners[index].health <= 0
                        ? Sound::SpawnerDeath : Sound::SpawnerHit);
                    hitSpawners[index] = true;
                }
        }
        if (railLength > 0) {
            enemyRails.push_back(
                {startX, startY, dx, dy, railLength, weapon.width,
                 kRailFlashDuration});
            emitted = true;
        }
        }
    }
    if (emitted) PlaySoundEffect(Sound::RailgunShot);
    return emitted;
}

void BeginPrimaryFire(int mouseX, int mouseY) {
    aimX = mouseX;
    aimY = mouseY;
    shooting = true;
    if (run.primaryWeapon == PrimaryWeapon::Railgun ||
        (playerInteriorState.permanent[4] && run.primaryWeapons[
            static_cast<int>(PrimaryWeapon::Railgun)])) {
        const float chargeTime = ResolvePlayerWeapon("railgun").cadence;
        if (run.primaryCharge < chargeTime) run.primaryCharge = 0;
        LockRailAim();
        PlaySoundEffect(Sound::ChargerChargeUp);
    }
    if (run.primaryWeapon == PrimaryWeapon::Standard ||
        run.primaryWeapon == PrimaryWeapon::Boomerang ||
        (playerInteriorState.permanent[4] && (run.primaryWeapons[
            static_cast<int>(PrimaryWeapon::Standard)] ||
            run.primaryWeapons[static_cast<int>(PrimaryWeapon::Boomerang)]))) {
        if (shotCooldown <= 0.0f) {
            ShootToward(mouseX, mouseY);
            shotCooldown = EffectiveShotInterval();
        }
    }
}

void ReleasePrimaryFire(int mouseX, int mouseY) {
    aimX = mouseX;
    aimY = mouseY;
    shooting = false;
    const bool railEnabled = run.primaryWeapon == PrimaryWeapon::Railgun ||
        (playerInteriorState.permanent[4] && run.primaryWeapons[
            static_cast<int>(PrimaryWeapon::Railgun)]);
    if (railEnabled &&
        run.primaryCharge >= ResolvePlayerWeapon("railgun").cadence) {
        LockRailAim();
        if (FirePlayerRail()) run.primaryCharge = 0;
    } else {
        run.primaryCharge = 0;
    }
}

void FireSecondary(int mouseX, int mouseY) {
    (void)mouseX;
    (void)mouseY;
    if (bombCooldown > 0 || playerHealth <= 0 || run.mapActive ||
        (currentMap != "interior" && currentMap != "glyph"))
        return;
    const float dx =
        AimWorldX() - (playerX + kPlayerSize * 0.5f);
    const float dy =
        AimWorldY() - (playerY + kPlayerSize * 0.5f);
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.01f) return;
    float cooldown = 0;
    const bool allSecondaries = playerInteriorState.permanent[5];
    const bool bomb = (allSecondaries && run.secondaryWeapons[
        static_cast<int>(SecondaryWeapon::Bomb)]) ||
        (!allSecondaries && run.secondaryWeapon == SecondaryWeapon::Bomb);
    const bool contact = (allSecondaries && run.secondaryWeapons[
        static_cast<int>(SecondaryWeapon::ContactBomb)]) ||
        (!allSecondaries &&
         run.secondaryWeapon == SecondaryWeapon::ContactBomb);
    const bool rocket = (allSecondaries && run.secondaryWeapons[
        static_cast<int>(SecondaryWeapon::HomingRocket)]) ||
        (!allSecondaries &&
         run.secondaryWeapon == SecondaryWeapon::HomingRocket);
    if (bomb) {
        const WeaponStats weapon = ResolvePlayerWeapon("bomb");
        EmitBombWeapon(weapon, dx / length, dy / length);
        cooldown = std::max(cooldown, weapon.cadence);
    }
    if (contact) {
        const WeaponStats weapon = ResolvePlayerWeapon("contact_bomb");
        EmitBombWeapon(weapon, dx / length, dy / length);
        cooldown = std::max(cooldown, weapon.cadence);
    }
    if (rocket) {
        const WeaponStats weapon = ResolvePlayerWeapon("homing_rocket");
        EmitProjectileWeapon(weapon, dx / length, dy / length);
        cooldown = std::max(cooldown, weapon.cadence);
    }
    bombCooldown = cooldown;
}

Rect EnemyRect(const Enemy& enemy) {
    const int scale = EnemyScale(enemy);
    const EnemyType& type = types.at(enemy.type);
    std::size_t columns = 0;
    for (const auto& row : type.sprite)
        columns = std::max(columns, row.size());
    return {
        enemy.x, enemy.y, static_cast<float>(columns * scale),
        static_cast<float>(type.sprite.size() * scale)};
}

int EnemyScale(const Enemy& enemy) {
    return std::max(
        1, types.at(enemy.type).pixelScale +
            static_cast<int>(EnemyStage(
                enemy.type, EnemyDifficultyStat::Size)));
}

bool EnemyVisualOverlaps(const Enemy& enemy, const Rect& target) {
    const int scale = EnemyScale(enemy);
    const EnemyType& type = types.at(enemy.type);
    for (std::size_t row = 0; row < type.sprite.size(); ++row)
        for (std::size_t column = 0;
             column < type.sprite[row].size(); ++column) {
            if (!type.sprite[row][column].occupied) continue;
            const Rect pixel{
                enemy.x + static_cast<float>(column * scale),
                enemy.y + static_cast<float>(row * scale),
                static_cast<float>(scale), static_cast<float>(scale)};
            if (Overlaps(pixel, target)) return true;
        }
    return false;
}

bool EnemyVisualFitsNetwork(const Enemy& enemy, float x, float y) {
    const int scale = EnemyScale(enemy);
    const EnemyType& type = types.at(enemy.type);
    for (std::size_t row = 0; row < type.sprite.size(); ++row)
        for (std::size_t column = 0;
             column < type.sprite[row].size(); ++column) {
            if (!type.sprite[row][column].occupied) continue;
            const Rect pixel{
                x + static_cast<float>(column * scale),
                y + static_cast<float>(row * scale),
                static_cast<float>(scale), static_cast<float>(scale)};
            if (RunArenaMode()) {
                if (!ArenaAllowsPlayer(runArena, pixel)) return false;
            } else if (!InRoomNetwork(pixel) || HitsShield(pixel)) {
                return false;
            }
        }
    return true;
}

bool EnemyVisualWithinRadius(
    const Enemy& enemy, float x, float y, float radius) {
    const float radiusSquared = radius * radius;
    const int scale = EnemyScale(enemy);
    const EnemyType& type = types.at(enemy.type);
    for (std::size_t row = 0; row < type.sprite.size(); ++row)
        for (std::size_t column = 0;
             column < type.sprite[row].size(); ++column) {
            if (!type.sprite[row][column].occupied) continue;
            const Rect pixel{
                enemy.x + static_cast<float>(column * scale),
                enemy.y + static_cast<float>(row * scale),
                static_cast<float>(scale), static_cast<float>(scale)};
            const float dx =
                x - std::clamp(x, pixel.x, pixel.x + pixel.width);
            const float dy =
                y - std::clamp(y, pixel.y, pixel.y + pixel.height);
            if (dx * dx + dy * dy <= radiusSquared) return true;
        }
    return false;
}

// Returns true when a map/node transition happened and the frame's
// remaining Update() work should be skipped.
bool UpdateMovementAndPortals(float dt) {
    float moveX = static_cast<float>(keys[1]) -
                  static_cast<float>(keys[0]);
    float moveY = static_cast<float>(keys[3]) -
                  static_cast<float>(keys[2]);
    const float movementLength =
        std::sqrt(moveX * moveX + moveY * moveY);
    if (movementLength > 0) {
        moveX /= movementLength;
        moveY /= movementLength;
    }
    float moveSpeed =
        kPlayerSpeed + 12.0f * UpgradeRank(UpgradeType::MoveSpeed);
    if (run.mapActive) {
        playerX = std::clamp(
            playerX + moveX * moveSpeed * dt,
            0.0f, kRunMapWidth - kPlayerSize);
        playerY = std::clamp(
            playerY + moveY * moveSpeed * dt,
            0.0f, kRunMapHeight - kPlayerSize);
        const RunNode* current = CurrentRunNode();
        if (!current) return true;
        const Rect player{playerX, playerY, kPlayerSize, kPlayerSize};
        for (RunNodeId next : current->next) {
            const RunMapVertex* vertex = GetRunMapVertex(run, next);
            if (vertex && Overlaps(player, RunMapVertexRect(*vertex))) {
                EnterRunNode(next);
                return true;
            }
        }
        return true;
    }
    Rect nextPlayerX{
        playerX + moveX * moveSpeed * dt, playerY,
        kPlayerSize, kPlayerSize};
    if ((RunArenaMode() && ArenaAllowsPlayer(runArena, nextPlayerX)) ||
        (!RunArenaMode() && InRoomNetwork(nextPlayerX) &&
         !HitsShield(nextPlayerX)))
        playerX = nextPlayerX.x;
    Rect nextPlayerY{
        playerX, playerY + moveY * moveSpeed * dt,
        kPlayerSize, kPlayerSize};
    if ((RunArenaMode() && ArenaAllowsPlayer(runArena, nextPlayerY)) ||
        (!RunArenaMode() && InRoomNetwork(nextPlayerY) &&
         !HitsShield(nextPlayerY)))
        playerY = nextPlayerY.y;

    if (MainMenuActive()) {
        if (MainMenuPortalActive() &&
            Overlaps(
                {playerX, playerY, kPlayerSize, kPlayerSize},
                MainMenuPortalRect())) {
            EnterRunNode(run.startNode);
            PlaySoundEffect(Sound::Teleport);
            return true;
        }
    } else if (RunMode() || debugRoom || postBossTuningRoom) {
        RunNode* node = CurrentRunNode();
        if (postBossTuningRoom) {
            if (Overlaps(
                    {playerX, playerY, kPlayerSize, kPlayerSize},
                    PhysicalExitPortalRect())) {
                EnterMainMenu();
                PlaySoundEffect(Sound::Teleport);
                return true;
            }
        } else if (debugRoom) {
            if (Overlaps(
                    {playerX, playerY, kPlayerSize, kPlayerSize},
                    PhysicalExitPortalRect())) {
                EnterRunNode(run.currentNode);
                return true;
            }
        } else if (node)
            for (RunPortal& portal : node->portals) {
                if (!portal.active) continue;
                const Rect trigger =
                    portal.interiorTrigger.width > 0
                        ? portal.interiorTrigger : PhysicalExitPortalRect();
                const bool touching = Overlaps(
                    {playerX, playerY, kPlayerSize, kPlayerSize}, trigger);
                if (!portal.armed) {
                    if (!touching) portal.armed = true;
                    continue;
                }
                if (touching) {
                    if (portal.postBossTuning) {
                        EnterPostBossTuningRoom();
                        PlaySoundEffect(Sound::Teleport);
                        return true;
                    }
                    if (portal.destination != kInvalidRunNode) {
                        EnterRunNode(portal.destination);
                        PlaySoundEffect(Sound::Teleport);
                        return true;
                    }
                }
            }
    }
    return false;
}

void UpdatePlayerWeapons(float dt) {
    shotCooldown = std::max(0.0f, shotCooldown - dt);
    if (shooting && (run.primaryWeapon == PrimaryWeapon::Railgun ||
                     (playerInteriorState.permanent[4] &&
                      run.primaryWeapons[
                          static_cast<int>(PrimaryWeapon::Railgun)]))) {
        run.primaryCharge += dt;
        const float chargeTime =
            ResolvePlayerWeapon("railgun").cadence;
        if (run.primaryCharge >= chargeTime) {
            LockRailAim();
            if (FirePlayerRail()) {
                run.primaryCharge -= chargeTime;
                PlaySoundEffect(Sound::ChargerChargeUp);
            }
        }
    }
    if (shooting && (run.primaryWeapon == PrimaryWeapon::Standard ||
                     run.primaryWeapon == PrimaryWeapon::Boomerang ||
                     (playerInteriorState.permanent[4] &&
                      (run.primaryWeapons[
                          static_cast<int>(PrimaryWeapon::Standard)] ||
                       run.primaryWeapons[
                          static_cast<int>(PrimaryWeapon::Boomerang)])))) {
        while (shotCooldown <= 0) {
            ShootToward(aimX, aimY);
            shotCooldown += EffectiveShotInterval();
        }
    }
}

void UpdateSpawners(float dt, int activeRoom) {
    if (activeRoom != lastPlayerRoom) {
        const bool initialRoom = lastPlayerRoom < 0;
        for (Spawner& spawner : spawners)
            if (spawner.health > 0 && spawner.room == activeRoom) {
                if (initialRoom)
                    ResetSpawnerTimer(spawner, 3.0f);
                else
                    ResetSpawnerTimer(
                        spawner, std::min(spawner.timer, 0.5f));
            }
        lastPlayerRoom = activeRoom;
    }
    // Spawner timers pause outside the room occupied by the player.
    for (Spawner& spawner : spawners)
        if (spawner.health > 0 && spawner.room == activeRoom &&
            (!debugRoom || debugSpawnerOn[
                static_cast<std::size_t>(&spawner - spawners.data())])) {
            spawner.timer -= dt;
            if (spawner.timer <= 0) {
                SpawnBurst(spawner);
                const EnemyType& type = types.at(spawner.enemyType);
                const float spawnSpeedValue = std::max(
                    1.0f, type.spawnSpeed +
                        static_cast<float>(EnemyStage(
                            spawner.enemyType,
                            EnemyDifficultyStat::SpawnSpeed)));
                const float spawnRate =
                    1.0f + (spawnSpeedValue - 1.0f) * 0.1f;
                ResetSpawnerTimer(
                    spawner, RandomFloat(
                        interior.secondsMin, interior.secondsMax) *
                        1.0f / spawnRate);
            }
        }
}

void UpdateWaveProgress(float dt) {
    if (RunArenaMode() && !debugRoom) {
        AwardSpawnerDeaths();
        RunNode* node = CurrentRunNode();
        if (node && node->type == RunNodeType::EnemyArena &&
            !node->completed &&
            std::all_of(
                spawners.begin(), spawners.end(),
                [](const Spawner& value) {
                    return value.health <= 0;
                })) {
            node->waveCooldown += dt;
            if (node->waveCooldown >= rogueliteTuning.waveCooldown) {
                node->waves[node->activeWave].completed = true;
                ++node->activeWave;
                node->waveCooldown = 0;
                if (node->activeWave < node->waves.size()) {
                    CreateWaveSpawners(*node, node->activeWave);
                } else {
                    node->completed = true;
                    BuildArenaChoicePortals(*node);
                    RunNode* current = CurrentRunNode();
                    if (!current) return;
                    RebuildRunArena(*current);
                    RebuildGameplayTextBoxes();
                }
            }
        }
    }

    if (PlayerInteriorMode()) {
        RunNode* node = CurrentRunNode();
        if (node && node->playerInteriorWave &&
            std::all_of(spawners.begin(), spawners.end(),
                [](const Spawner& value) { return value.health <= 0; }) &&
            std::none_of(enemies.begin(), enemies.end(),
                [](const Enemy& value) { return value.health > 0; })) {
            node->playerInteriorWave = false;
            node->completed = true;
            shieldBlocks.clear();
            const Room& completedRoom = rooms[node->playerInteriorRoom];
            for (RunPortal& portal : node->portals) {
                portal.active = true;
                portal.armed = false;
                portal.interiorTrigger = {
                    RoomX(completedRoom) + interior.roomSize * 0.5f - 36.0f,
                    RoomY(completedRoom) + interior.roomSize * 0.5f - 36.0f,
                    72, 72};
            }
            BuildInteriorArenaDestinations(*node);
            RebuildGameplayTextBoxes();
        }
    }
}

void UpdatePlayerBuffs(float dt) {
    if (playerInteriorState.permanent[0] &&
        playerHealth < EffectivePlayerMaxHealth()) {
        run.regenerationTimer += dt;
        if (run.regenerationTimer >=
            std::max(0.1f, playerInteriorState.values[3] / 10.0f)) {
            ++playerHealth;
            run.regenerationTimer = 0;
        }
    } else {
        run.regenerationTimer = 0;
    }

    run.multishotRemaining =
        std::max(0.0f, run.multishotRemaining - dt);
    run.homingRemaining = std::max(0.0f, run.homingRemaining - dt);
    run.autoRocketRemaining =
        std::max(0.0f, run.autoRocketRemaining - dt);
    run.autoRocketCooldown -= dt;
    if ((run.autoRocketRemaining > 0 ||
         playerInteriorState.permanent[3]) &&
        run.autoRocketCooldown <= 0 && !enemies.empty()) {
        const WeaponStats weapon = ResolvePlayerWeapon("auto_rocket");
        EmitProjectileWeapon(weapon, 1, 0);
        run.autoRocketCooldown = weapon.cadence;
    }
}

void UpdatePickups(float dt) {
    if (debugRoom) {
        const Rect player{playerX, playerY, kPlayerSize, kPlayerSize};
        static constexpr std::array<PickupType, 3> pickups{{
            PickupType::Multishot, PickupType::Homing,
            PickupType::AutoRocket}};
        for (std::size_t index = 0; index < pickups.size(); ++index) {
            debugPickupTimers[index] = std::max(
                0.0f, debugPickupTimers[index] - dt);
            const Rect target{
                480.0f + static_cast<float>(index) * 260.0f, 430.0f,
                18, 18};
            if (debugPickupTimers[index] > 0 || !Overlaps(player, target))
                continue;
            debugPickupTimers[index] = 2.0f;
            if (pickups[index] == PickupType::Multishot)
                run.multishotRemaining = rogueliteTuning.powerupSeconds;
            else if (pickups[index] == PickupType::Homing)
                run.homingRemaining = rogueliteTuning.powerupSeconds;
            else {
                run.autoRocketRemaining = rogueliteTuning.powerupSeconds;
                run.autoRocketCooldown = 0;
            }
        }
    } else if (RunNode* node = CurrentRunNode()) {
        const Rect player{playerX, playerY, kPlayerSize, kPlayerSize};
        for (RunPickup& pickup : node->pickups) {
            if (pickup.collected ||
                !Overlaps(player, {pickup.x, pickup.y, 18, 18}))
                continue;
            pickup.collected = true;
            textBoxes.erase(
                std::remove_if(
                    textBoxes.begin(), textBoxes.end(),
                    [&](const TextBox& box) {
                        return Overlaps(
                            box.rect,
                            {pickup.x, pickup.y, 18, 18});
                    }),
                textBoxes.end());
            if (pickup.type == PickupType::Health)
                playerHealth = std::min(
                    EffectivePlayerMaxHealth(), playerHealth + 1);
            else if (pickup.type == PickupType::Multishot)
                run.multishotRemaining = rogueliteTuning.powerupSeconds;
            else if (pickup.type == PickupType::Homing)
                run.homingRemaining = rogueliteTuning.powerupSeconds;
            else if (pickup.type == PickupType::AutoRocket) {
                run.autoRocketRemaining = rogueliteTuning.powerupSeconds;
                run.autoRocketCooldown = 0;
            }
            PlaySoundEffect(Sound::PowerUp);
        }
    }
}

void ResolveEnemyCrowding() {
    // Smaller-than-enemy cells keep the broad phase local while the shrunken
    // footprints intentionally allow a little visual overlap.
    constexpr float cellSize = 24.0f;
    constexpr float footprintScale = 0.65f;
    constexpr float maximumPush = 6.0f;
    std::unordered_map<std::int64_t, std::vector<std::size_t>> cells;
    auto cellKey = [](int x, int y) {
        return static_cast<std::int64_t>(
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
            static_cast<std::uint32_t>(y));
    };
    auto footprint = [&](const Enemy& enemy) {
        const Rect rect = EnemyRect(enemy);
        const float width = rect.width * footprintScale;
        const float height = rect.height * footprintScale;
        return Rect{
            CenterX(rect) - width * 0.5f,
            CenterY(rect) - height * 0.5f, width, height};
    };
    for (std::size_t index = 0; index < enemies.size(); ++index) {
        if (enemies[index].health <= 0 ||
            !EnemyIsSimulated(enemies[index]))
            continue;
        const Rect rect = footprint(enemies[index]);
        const int firstX = static_cast<int>(std::floor(rect.x / cellSize));
        const int firstY = static_cast<int>(std::floor(rect.y / cellSize));
        const int lastX = static_cast<int>(
            std::floor((rect.x + rect.width) / cellSize));
        const int lastY = static_cast<int>(
            std::floor((rect.y + rect.height) / cellSize));
        for (int y = firstY; y <= lastY; ++y)
            for (int x = firstX; x <= lastX; ++x)
                cells[cellKey(x, y)].push_back(index);
    }
    std::unordered_set<std::uint64_t> testedPairs;
    for (const auto& [cell, indices] : cells) {
        (void)cell;
        for (std::size_t first = 0; first < indices.size(); ++first)
            for (std::size_t second = first + 1;
                 second < indices.size(); ++second) {
                const std::size_t a = indices[first], b = indices[second];
                const std::uint64_t pair = (static_cast<std::uint64_t>(
                    std::min(a, b)) << 32) | std::max(a, b);
                if (!testedPairs.insert(pair).second) continue;
                const Rect firstRect = footprint(enemies[a]);
                const Rect secondRect = footprint(enemies[b]);
                if (!Overlaps(firstRect, secondRect)) continue;
                const float overlapX = std::min(
                    firstRect.x + firstRect.width,
                    secondRect.x + secondRect.width) -
                    std::max(firstRect.x, secondRect.x);
                const float overlapY = std::min(
                    firstRect.y + firstRect.height,
                    secondRect.y + secondRect.height) -
                    std::max(firstRect.y, secondRect.y);
                const float direction = CenterX(firstRect) < CenterX(secondRect)
                    ? -1.0f : 1.0f;
                float pushX = 0, pushY = 0;
                if (overlapX < overlapY)
                    pushX = direction * std::min(
                        maximumPush, overlapX * 0.5f);
                else
                    pushY = (CenterY(firstRect) < CenterY(secondRect)
                        ? -1.0f : 1.0f) * std::min(
                            maximumPush, overlapY * 0.5f);
                if (EnemyVisualFitsNetwork(
                        enemies[a], enemies[a].x + pushX,
                        enemies[a].y + pushY)) {
                    enemies[a].x += pushX;
                    enemies[a].y += pushY;
                }
                if (EnemyVisualFitsNetwork(
                        enemies[b], enemies[b].x - pushX,
                        enemies[b].y - pushY)) {
                    enemies[b].x -= pushX;
                    enemies[b].y -= pushY;
                }
            }
    }
}

void UpdateEnemies(float dt, int activeRoom) {
    const std::vector<int> roomDistances =
        RoomDistancesFrom(activeRoom);
    for (Enemy& enemy : enemies) {
        if (enemy.health <= 0) continue;
        const Rect simulationRect = EnemyRect(enemy);
        if (!WithinSimulationRange(
                CenterX(simulationRect), CenterY(simulationRect)))
            continue;
        if (enemy.activationRemaining > 0) {
            enemy.activationRemaining =
                std::max(0.0f, enemy.activationRemaining - dt);
            continue;
        }
        const EnemyType& enemyType = types.at(enemy.type);
        if (enemy.type == "shooter") {
            UpdateShooter(
                enemy, enemyType, activeRoom, roomDistances, dt);
        } else if (enemy.type == "charger") {
            UpdateCharger(
                enemy, enemyType, activeRoom, roomDistances, dt);
        } else {
            float targetX = playerX + kPlayerSize * 0.5f;
            float targetY = playerY + kPlayerSize * 0.5f;
            if (!RunArenaMode() && enemy.room != activeRoom) {
                const int nextRoom =
                    NextRoomToward(enemy.room, roomDistances);
                if (nextRoom >= 0) {
                    RoomExitTarget(enemy, nextRoom, targetX, targetY);
                }
            }
            FacePoint(enemy, targetX, targetY);
            MoveEnemyToward(
                enemy, targetX, targetY,
                EnemyMovementSpeed(enemy), dt);
        }
        if (!RunArenaMode()) {
            const Rect moved = EnemyRect(enemy);
            const int movedRoom =
                RoomAtWorld(CenterX(moved), CenterY(moved));
            if (movedRoom >= 0) enemy.room = movedRoom;
        }
    }
    ResolveEnemyCrowding();
}

void UpdateProjectilesAndBombs(float dt) {
    for (Projectile& projectile : projectiles) {
        if (projectile.activationDelay > 0) {
            projectile.activationDelay =
                std::max(0.0f, projectile.activationDelay - dt);
            continue;
        }
        if (projectile.boomerang) {
            const float speed = std::sqrt(
                projectile.vx * projectile.vx + projectile.vy * projectile.vy);
            const float deceleration =
                projectile.boomerangReleaseSpeed *
                projectile.boomerangReleaseSpeed /
                std::max(1.0f, projectile.maxDistance * 2.0f) *
                ResolvePlayerWeapon("boomerang").decelerationScale;
            if (!projectile.returning) {
                if (speed <= 1.0f) {
                    projectile.returning = true;
                    projectile.boomerangHitEnemies.clear();
                    projectile.boomerangHitSpawners.clear();
                    projectile.boomerangHitTextBoxes.clear();
                    projectile.boomerangHitSpecialControl = false;
                    projectile.boomerangHitShop = false;
                } else {
                    const float nextSpeed =
                        std::max(0.0f, speed - deceleration * dt);
                    projectile.vx = projectile.vx / speed * nextSpeed;
                    projectile.vy = projectile.vy / speed * nextSpeed;
                }
            }
            if (projectile.homing && !projectile.returning)
                HomeProjectile(projectile);
            if (projectile.returning) {
                const float dx = playerX + kPlayerSize * 0.5f - projectile.x;
                const float dy = playerY + kPlayerSize * 0.5f - projectile.y;
                const float length = std::sqrt(dx * dx + dy * dy);
                if (length < kPlayerSize) {
                    projectile.distance = -1;
                    continue;
                }
                projectile.boomerangReturnSpeed = std::min(
                    projectile.boomerangReleaseSpeed,
                    projectile.boomerangReturnSpeed + deceleration * dt);
                if (length <= projectile.boomerangReturnSpeed * dt +
                    kPlayerSize * 0.5f) {
                    projectile.distance = -1;
                    continue;
                }
                projectile.vx = dx / length *
                    projectile.boomerangReturnSpeed;
                projectile.vy = dy / length *
                    projectile.boomerangReturnSpeed;
            }
            projectile.x += projectile.vx * dt;
            projectile.y += projectile.vy * dt;
            projectile.distance += speed * dt;
            const Rect shot{
                projectile.x, projectile.y,
                projectile.width, projectile.width};
            for (std::size_t index = 0; index < enemies.size(); ++index)
                if (enemies[index].health > 0 &&
                    EnemyIsSimulated(enemies[index]) &&
                    std::find(
                        projectile.boomerangHitEnemies.begin(),
                        projectile.boomerangHitEnemies.end(),
                        static_cast<int>(index)) ==
                        projectile.boomerangHitEnemies.end() &&
                    EnemyVisualOverlaps(enemies[index], shot)) {
                    enemies[index].health -= projectile.damage;
                    projectile.boomerangHitEnemies.push_back(
                        static_cast<int>(index));
                }
            for (std::size_t index = 0; index < spawners.size(); ++index)
                if (spawners[index].health > 0 &&
                    SpawnerIsSimulated(spawners[index]) &&
                    std::find(
                        projectile.boomerangHitSpawners.begin(),
                        projectile.boomerangHitSpawners.end(),
                        static_cast<int>(index)) ==
                        projectile.boomerangHitSpawners.end() &&
                    Overlaps(shot, {spawners[index].x, spawners[index].y, 30, 30})) {
                    spawners[index].health -= projectile.damage;
                    PlaySoundEffect(spawners[index].health <= 0
                        ? Sound::SpawnerDeath : Sound::SpawnerHit);
                    projectile.boomerangHitSpawners.push_back(
                        static_cast<int>(index));
                }
            for (std::size_t index = 0; index < textBoxes.size(); ++index) {
                if (!Overlaps(shot, textBoxes[index].rect) ||
                    std::find(
                        projectile.boomerangHitTextBoxes.begin(),
                        projectile.boomerangHitTextBoxes.end(),
                        static_cast<int>(index)) !=
                        projectile.boomerangHitTextBoxes.end())
                    continue;
                int organEdits = 0;
                const bool changedLevel = DamageTextBox(
                    textBoxes[index], CenterX(shot), &organEdits);
                const bool startGame =
                    MainMenuActive() && textBoxes[index].startGame;
                projectile.boomerangHitTextBoxes.push_back(
                    static_cast<int>(index));
                SaveMutations();
                if (startGame) TriggerMainMenuStart();
                for (int edit = 0; edit < organEdits; ++edit)
                    UnlockInteriorPortals();
                if (changedLevel)
                    pendingLevelSelection = true;
                else if (!MainMenuActive())
                    RebuildGameplayTextBoxes();
                break;
            }
            if (!projectile.boomerangHitSpecialControl)
                projectile.boomerangHitSpecialControl =
                    HitSpecialControl(shot);
            if (!projectile.boomerangHitShop)
                projectile.boomerangHitShop = HitShopOffer(shot);
            if (HitMutableGeometry(shot) ||
                RunWall(shot) || (!RunArenaMode() && HitsWall(shot)))
                projectile.distance = -1;
            continue;
        }
        HomeProjectile(projectile);
        const float speed =
            std::sqrt(projectile.vx * projectile.vx +
                      projectile.vy * projectile.vy);
        const int steps = std::max(
            1, static_cast<int>(std::ceil(speed * dt / 6.0f)));
        for (int step = 0;
             step < steps && projectile.distance >= 0; ++step) {
            const float dx = projectile.vx * dt / steps;
            const float dy = projectile.vy * dt / steps;
            projectile.x += dx;
            projectile.y += dy;
            projectile.distance += std::sqrt(dx * dx + dy * dy);
            const Rect shot{
                projectile.x, projectile.y,
                projectile.width, projectile.width};
            const bool hit = MainMenuActive()
                ? (HitText(projectile) || RunWall(shot) ||
                   (!RunArenaMode() && HitsWall(shot)))
                : HitSpecialControl(shot) ||
                HitShopOffer(shot) ||
                HitBossTurretTarget(shot, projectile.damage) ||
                HitBoss(shot, projectile.damage) ||
                HitShield(shot, projectile.damage) ||
                HitText(projectile) ||
                HitSpawner(shot, projectile.damage) ||
                HitEnemy(shot, projectile.damage) ||
                HitMutableGeometry(shot) ||
                RunWall(shot) ||
                (!RunArenaMode() && HitsWall(shot));
            if (hit) {
                if (projectile.rocket)
                    DetonateBomb(
                        projectile.x, projectile.y, projectile.damage,
                        projectile.explosionRadius);
                projectile.distance = -1;
            }
        }
    }
    projectiles.erase(
        std::remove_if(
            projectiles.begin(), projectiles.end(),
            [](const Projectile& projectile) {
                return projectile.distance < 0 ||
                       (!projectile.boomerang &&
                        projectile.distance > projectile.maxDistance);
            }),
        projectiles.end());

    for (Bomb& bomb : bombs) {
        if (bomb.activationDelay > 0) {
            bomb.activationDelay = std::max(
                0.0f, bomb.activationDelay - dt);
            continue;
        }
        HomeBomb(bomb);
        const float travel =
            std::min(
                bomb.distanceRemaining,
                std::sqrt(bomb.vx * bomb.vx + bomb.vy * bomb.vy) * dt);
        const int steps = std::max(
            1, static_cast<int>(std::ceil(travel / 4.0f)));
        const float stepDistance = travel / steps;
        for (int step = 0;
             step < steps && bomb.distanceRemaining > 0; ++step) {
            const float speed =
                std::sqrt(bomb.vx * bomb.vx + bomb.vy * bomb.vy);
            const float dx = bomb.vx / speed * stepDistance;
            const float dy = bomb.vy / speed * stepDistance;
            Rect nextX{bomb.x + dx - 5, bomb.y - 5, 10, 10};
            Rect nextY{bomb.x - 5, bomb.y + dy - 5, 10, 10};
            const bool hitX = BombObstacle(nextX);
            const bool hitY = BombObstacle(nextY);
            const Rect nextBomb{
                bomb.x + dx - 5, bomb.y + dy - 5, 10, 10};
            const bool hitEnemy = bomb.contact && std::any_of(
                enemies.begin(), enemies.end(),
                [&](const Enemy& enemy) {
                    return enemy.health > 0 && EnemyIsSimulated(enemy) &&
                        EnemyVisualOverlaps(enemy, nextBomb);
                });
            if (bomb.contact && (hitX || hitY || hitEnemy)) {
                bomb.x += dx;
                bomb.y += dy;
                bomb.distanceRemaining = 0;
                break;
            }
            if (hitX)
                bomb.vx = -bomb.vx;
            else
                bomb.x += dx;
            if (hitY)
                bomb.vy = -bomb.vy;
            else
                bomb.y += dy;
            if (!hitX && !hitY) {
                Rect diagonal{
                    bomb.x - 5, bomb.y - 5, 10, 10};
                if (BombObstacle(diagonal)) {
                    bomb.vx = -bomb.vx;
                    bomb.vy = -bomb.vy;
                    bomb.x -= dx;
                    bomb.y -= dy;
                }
            }
            bomb.distanceRemaining -= stepDistance;
        }
    }
    for (const Bomb& bomb : bombs)
        if (bomb.distanceRemaining <= 0)
            DetonateBomb(bomb.x, bomb.y, bomb.damage, bomb.radius);
    bombs.erase(
        std::remove_if(
            bombs.begin(), bombs.end(),
            [](const Bomb& bomb) {
                return bomb.distanceRemaining <= 0;
            }),
        bombs.end());

    for (Explosion& explosion : explosions)
        explosion.timeRemaining -= dt;
    explosions.erase(
        std::remove_if(
            explosions.begin(), explosions.end(),
            [](const Explosion& explosion) {
                return explosion.timeRemaining <= 0;
            }),
        explosions.end());
    for (EnemyRail& rail : enemyRails)
        rail.timeRemaining -= dt;
    enemyRails.erase(
        std::remove_if(
            enemyRails.begin(), enemyRails.end(),
            [](const EnemyRail& rail) {
                return rail.timeRemaining <= 0;
            }),
        enemyRails.end());
}

void FinalizeFrame(int activeRoom) {
    if (!debugRoom) if (RunNode* node = CurrentRunNode()) {
        const bool rewardsEnabled =
            node->type != RunNodeType::PlayerInterior;
        for (const Enemy& enemy : enemies) {
            if (enemy.health > 0) continue;
            const std::uint64_t sequence = run.deathSequence++;
            const std::uint64_t drop = DeriveRunSeed(
                run.globalSeed, 0x44524f50ULL, sequence);
            if (rewardsEnabled &&
                drop % rogueliteTuning.healthPickupDropChance == 0) {
                node->pickups.push_back({
                    PickupType::Health,
                    UpgradeType::MaxHealth, 1,
                    enemy.x, enemy.y, false});
            } else if (rewardsEnabled &&
                       drop % rogueliteTuning.powerupDropChance == 0) {
                static const PickupType pickupTypes[]{
                    PickupType::Multishot, PickupType::Homing,
                    PickupType::AutoRocket};
                node->pickups.push_back({
                    pickupTypes[(drop >> 8) % 3],
                    UpgradeType::MaxHealth, 1,
                    enemy.x, enemy.y, false});
            }
        }
    }
    enemies.erase(
        std::remove_if(
            enemies.begin(), enemies.end(),
            [&](Enemy& enemy) {
                if (enemy.health <= 0) return true;
                if (enemy.activationRemaining <= 0 &&
                    enemy.room == activeRoom &&
                    EnemyVisualOverlaps(
                        enemy, {playerX, playerY,
                                kPlayerSize, kPlayerSize})) {
                    const EnemyType& type = types.at(enemy.type);
                    if (enemy.type == "shooter") return false;
                    if (enemy.type == "charger") {
                        if (enemy.phase != EnemyPhase::Attack)
                            return false;
                        if (DamagePlayer(
                                type.contactDamage + static_cast<int>(
                                    EnemyStage(
                                        enemy.type,
                                        EnemyDifficultyStat::Damage))))
                            BeginRecovery(enemy, type);
                        return false;
                    }
                    return DamagePlayer(
                        type.contactDamage + static_cast<int>(
                            EnemyStage(
                                enemy.type,
                                EnemyDifficultyStat::Damage)));
                }
                return false;
            }),
        enemies.end());
    if (!debugRoom) AwardSpawnerDeaths();
}

void Update(float dt) {
    playerInvincibility = std::max(0.0f, playerInvincibility - dt);
    if (currentMap == "audio") {
        UpdateAudioMap(dt);
        return;
    }
    if (currentMap == "glyph") {
        UpdateGlyphMap(dt);
        return;
    }
    if (currentMap != "interior") return;
    bombCooldown = std::max(0.0f, bombCooldown - dt);
    if (UpdateMovementAndPortals(dt)) return;
    if (MainMenuActive()) {
        UpdatePlayerWeapons(dt);
        UpdateProjectilesAndBombs(dt);
        UpdateMainMenu(dt);
        return;
    }
    UpdateShopPurchases(dt);
    UpdateDebugInteractions(dt);
    UpdatePlayerWeapons(dt);
    UpdatePlayerInteriorAlteration(dt);
    const int activeRoom = RunArenaMode() ? 0 : CurrentRoom();
    UpdateSpawners(dt, activeRoom);
    UpdateWaveProgress(dt);
    UpdateBossFight(dt);
    UpdatePlayerBuffs(dt);
    UpdatePickups(dt);
    UpdateEnemies(dt, activeRoom);
    UpdateProjectilesAndBombs(dt);
    FinalizeFrame(activeRoom);
    if (playerHealth <= 0) {
        if (run.extraLifeAvailable) {
            run.extraLifeAvailable = false;
            playerHealth = EffectivePlayerMaxHealth();
            playerInvincibility = EffectiveInvincibilityDuration();
            PlaySoundEffect(Sound::PowerUp);
            return;
        }
        EnterMainMenu();
        return;
    }
    if (pendingLevelSelection) {
        pendingLevelSelection = false;
        SelectCurrentLevel();
    }
}

}  // namespace game

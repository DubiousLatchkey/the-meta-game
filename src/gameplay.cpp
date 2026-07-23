#include "gameplay.h"

#include <algorithm>
#include <cmath>
#include <queue>

#include "audio.h"
#include "text_renderer.h"
#include "world.h"

namespace game {
namespace {

bool DamageTextBox(TextBox& box, float impactX) {
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
        const int previousValue = organ.value;
        if (organ.id == "spawn")
            organ.value =
                std::max(0, organ.value - organ.decrement);
        else if (organ.id == "shield")
            organ.value =
                organ.value <= 0 ? organ.maximum : organ.value - 1;
        else
            organ.value =
                organ.value <= 1 ? organ.maximum : organ.value - 1;
        if (organ.id == "shield" && organ.value < previousValue)
            for (ShieldBlock& shield : shieldBlocks)
                shield.health = std::min(shield.health, organ.value);
        UpdateValueWord(organ);
        if (organ.id == "spawn") UpdateSpawnerTypes();
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
        projectile.x, projectile.y, kProjectileSize, kProjectileSize};
    for (TextBox& box : textBoxes) {
        if (!Overlaps(shot, box.rect)) continue;
        const bool changedLevel =
            DamageTextBox(box, CenterX(shot));
        SaveMutations();
        if (changedLevel)
            SelectCurrentLevel();
        else
            BuildWorldTextBoxes();
        return true;
    }
    return false;
}

bool HitShield(const Rect& shot, int damage) {
    for (ShieldBlock& shield : shieldBlocks)
        if (shield.health > 0 && Overlaps(shot, shield.rect)) {
            shield.health = std::max(0, shield.health - damage);
            PlaySoundEffect(Sound::HitEnemy);
            return true;
        }
    return false;
}

bool HitSpawner(const Rect& shot, int damage) {
    for (Spawner& spawner : spawners)
        if (spawner.health > 0 &&
            Overlaps(shot, {spawner.x, spawner.y, 30, 30})) {
            spawner.health -= damage;
            return true;
        }
    return false;
}

bool HitEnemy(const Rect& shot, int damage) {
    for (Enemy& enemy : enemies)
        if (enemy.health > 0 && EnemyVisualOverlaps(enemy, shot)) {
            enemy.health -= damage;
            PlaySoundEffect(Sound::HitEnemy);
            return true;
        }
    return false;
}

bool HitRoomWall(const Rect& shot) {
    const int row = static_cast<int>(
        std::floor(CenterY(shot) / interior.roomSize));
    const int column = static_cast<int>(
        std::floor(CenterX(shot) / interior.roomSize));
    int room = RoomIndexAt(row, column);
    bool hit = false;
    for (const WallRect& wall : BuildWalls())
        if (wall.room == room && Overlaps(shot, wall.rect)) {
            hit = true;
            break;
        }
    if (!hit && !HitsWall(shot, &room)) return false;
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

void DetonateBomb(float x, float y) {
    PlaySoundEffect(Sound::Explosion);
    for (Enemy& enemy : enemies) {
        if (EnemyVisualWithinRadius(enemy, x, y, kBombRadius))
            enemy.health -= 2;
    }
    const float radiusSquared = kBombRadius * kBombRadius;
    for (Spawner& spawner : spawners) {
        const Rect rect{spawner.x, spawner.y, 30, 30};
        const float dx = x - std::clamp(x, rect.x, rect.x + rect.width);
        const float dy = y - std::clamp(y, rect.y, rect.y + rect.height);
        if (spawner.health > 0 &&
            dx * dx + dy * dy <= radiusSquared)
            spawner.health -= 2;
    }
    for (ShieldBlock& shield : shieldBlocks) {
        const float dx = x - std::clamp(
            x, shield.rect.x, shield.rect.x + shield.rect.width);
        const float dy = y - std::clamp(
            y, shield.rect.y, shield.rect.y + shield.rect.height);
        if (shield.health > 0 &&
            dx * dx + dy * dy <= radiusSquared)
            shield.health = std::max(0, shield.health - 2);
    }
    bool changedText = false;
    bool changedLevel = false;
    for (TextBox& text : textBoxes) {
        const float dx = x - std::clamp(
            x, text.rect.x, text.rect.x + text.rect.width);
        const float dy = y - std::clamp(
            y, text.rect.y, text.rect.y + text.rect.height);
        if (dx * dx + dy * dy > radiusSquared) continue;
        changedLevel = DamageTextBox(text, x) || changedLevel;
        changedText = true;
    }
    if (changedText) {
        SaveMutations();
        if (changedLevel)
            SelectCurrentLevel();
        else
            BuildWorldTextBoxes();
    }
    explosions.push_back({x, y, kExplosionDuration});
}

bool BombObstacle(const Rect& bomb) {
    if (HitsWall(bomb) || HitsShield(bomb)) return true;
    for (const Spawner& spawner : spawners)
        if (spawner.health > 0 &&
            Overlaps(bomb, {spawner.x, spawner.y, 30, 30}))
            return true;
    for (const TextBox& text : textBoxes)
        if (Overlaps(bomb, text.rect)) return true;
    return false;
}

void SpawnBurst(Spawner& spawner) {
    const EnemyType& type = types.at(spawner.enemyType);
    const int count = RandomInt(type.burstMin, type.burstMax);
    const int healthOrgan = OrganIndex("health");
    const int health = spawner.enemyType == interior.archetype &&
            healthOrgan >= 0
        ? std::max(1, organs[healthOrgan].value)
        : type.maxHealth;
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
    if (targetRoom < 0) return distances;
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
            if (next >= 0 && distances[next] < 0) {
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
        if (next >= 0 && distances[next] >= 0 &&
            (distances[best] < 0 || distances[next] < distances[best]))
            best = next;
    }
    return best;
}

float EnemyMovementSpeed(const Enemy& enemy) {
    const EnemyType& type = types.at(enemy.type);
    const int speedOrgan = OrganIndex("speed");
    const int speedMaximum = speedOrgan >= 0
        ? std::max(1, organs[speedOrgan].maximum) : 1;
    const float normalSpeed =
        type.speed * speedMaximum * interior.speedUnit;
    if (enemy.type != interior.archetype || speedOrgan < 0)
        return normalSpeed;
    const float speedSetting = speedMaximum > 1
        ? static_cast<float>(
              std::clamp(organs[speedOrgan].value, 1, speedMaximum) - 1) /
              (speedMaximum - 1)
        : 1.0f;
    return normalSpeed * (0.5f + speedSetting * 0.5f);
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
    if (EnemyVisualFitsNetwork(enemy, enemy.x + moveX, enemy.y))
        enemy.x += moveX;
    if (EnemyVisualFitsNetwork(enemy, enemy.x, enemy.y + moveY))
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
        if (HitsWall(probe) || HitsShield(probe)) break;
        railLength = distance;
        if (!hitPlayer && Overlaps(probe, player)) {
            playerHealth = std::max(
                0, playerHealth - type.contactDamage);
            PlaySoundEffect(Sound::HitHurt);
            hitPlayer = true;
        }
    }
    enemyRails.push_back(
        {startX, startY, dx, dy, railLength, width,
         kRailFlashDuration});
    PlaySoundEffect(Sound::LaserShoot);
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
        enemy.phaseTimer -= dt;
        if (enemy.phaseTimer <= 0) {
            FireRail(enemy, type);
            BeginRecovery(enemy, type);
        }
        return;
    }

    float targetX = playerX + kPlayerSize * 0.5f;
    float targetY = playerY + kPlayerSize * 0.5f;
    if (enemy.room != activeRoom) {
        const int nextRoom =
            NextRoomToward(enemy.room, roomDistances);
        if (nextRoom >= 0) {
            targetX =
                RoomX(rooms[nextRoom]) + interior.roomSize * 0.5f;
            targetY =
                RoomY(rooms[nextRoom]) + interior.roomSize * 0.5f;
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
    if (enemy.room != activeRoom) {
        const int nextRoom =
            NextRoomToward(enemy.room, roomDistances);
        if (nextRoom >= 0) {
            targetX =
                RoomX(rooms[nextRoom]) + interior.roomSize * 0.5f;
            targetY =
                RoomY(rooms[nextRoom]) + interior.roomSize * 0.5f;
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
    } else {
        MoveEnemyToward(
            enemy, targetX, targetY, EnemyMovementSpeed(enemy), dt);
    }
}

}  // namespace

void ShootToward(int mouseX, int mouseY) {
    (void)mouseX;
    (void)mouseY;
    if (playerHealth <= 0 || currentMap != "interior") return;
    const float cx = playerX + kPlayerSize * 0.5f;
    const float cy = playerY + kPlayerSize * 0.5f;
    const float dx = AimWorldX() - cx;
    const float dy = AimWorldY() - cy;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length > 0.01f) {
        projectiles.push_back(
            {cx, cy, dx / length * kProjectileSpeed,
             dy / length * kProjectileSpeed, 0});
        PlaySoundEffect(Sound::LaserShoot);
    }
}

void LaunchBomb(int mouseX, int mouseY) {
    (void)mouseX;
    (void)mouseY;
    if (bombCooldown > 0 || playerHealth <= 0 ||
        currentMap != "interior")
        return;
    const float cx = playerX + kPlayerSize * 0.5f;
    const float cy = playerY + kPlayerSize * 0.5f;
    const float dx = AimWorldX() - cx;
    const float dy = AimWorldY() - cy;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length > 0.01f) {
        bombs.push_back(
            {cx, cy, dx / length * kBombSpeed,
             dy / length * kBombSpeed, kBombRange});
        bombCooldown = kBombCooldownDuration;
    }
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
    if (enemy.type == interior.archetype)
        return std::max(1, organs[0].value);
    return std::max(1, types.at(enemy.type).pixelScale);
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
            if (!InRoomNetwork(pixel) || HitsShield(pixel)) return false;
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

void Update(float dt) {
    if (currentMap != "interior") return;
    bombCooldown = std::max(0.0f, bombCooldown - dt);
    if (playerHealth < kPlayerMaxHealth) {
        healthRegenTimer -= dt;
        if (healthRegenTimer <= 0) {
            playerHealth = std::min(
                kPlayerMaxHealth, playerHealth + 1);
            healthRegenTimer = kHealthRegenSeconds;
        }
    } else {
        healthRegenTimer = kHealthRegenSeconds;
    }
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
    Rect nextPlayerX{
        playerX + moveX * kPlayerSpeed * dt, playerY,
        kPlayerSize, kPlayerSize};
    if (InRoomNetwork(nextPlayerX) && !HitsShield(nextPlayerX))
        playerX = nextPlayerX.x;
    Rect nextPlayerY{
        playerX, playerY + moveY * kPlayerSpeed * dt,
        kPlayerSize, kPlayerSize};
    if (InRoomNetwork(nextPlayerY) && !HitsShield(nextPlayerY))
        playerY = nextPlayerY.y;

    if (shooting) {
        shotCooldown -= dt;
        while (shotCooldown <= 0) {
            ShootToward(aimX, aimY);
            shotCooldown += kShotInterval;
        }
    }

    const int activeRoom = CurrentRoom();
    if (activeRoom != lastPlayerRoom) {
        const bool initialRoom = lastPlayerRoom < 0;
        for (Spawner& spawner : spawners)
            if (spawner.health > 0 && spawner.room == activeRoom) {
                if (initialRoom)
                    spawner.timer = 3.0f;
                else
                    spawner.timer = std::min(spawner.timer, 0.5f);
            }
        lastPlayerRoom = activeRoom;
    }
    // Spawner timers pause outside the room occupied by the player.
    for (Spawner& spawner : spawners)
        if (spawner.health > 0 && spawner.room == activeRoom) {
            spawner.timer -= dt;
            if (spawner.timer <= 0) {
                SpawnBurst(spawner);
                spawner.timer = RandomFloat(
                    interior.secondsMin, interior.secondsMax);
            }
        }

    const std::vector<int> roomDistances =
        RoomDistancesFrom(activeRoom);
    for (Enemy& enemy : enemies) {
        if (enemy.health <= 0) continue;
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
            if (enemy.room != activeRoom) {
                const int nextRoom =
                    NextRoomToward(enemy.room, roomDistances);
                if (nextRoom >= 0) {
                    targetX =
                        RoomX(rooms[nextRoom]) +
                        interior.roomSize * 0.5f;
                    targetY =
                        RoomY(rooms[nextRoom]) +
                        interior.roomSize * 0.5f;
                }
            }
            FacePoint(enemy, targetX, targetY);
            MoveEnemyToward(
                enemy, targetX, targetY,
                EnemyMovementSpeed(enemy), dt);
        }
        const Rect moved = EnemyRect(enemy);
        const int movedColumn = static_cast<int>(
            std::floor(CenterX(moved) / interior.roomSize));
        const int movedRow = static_cast<int>(
            std::floor(CenterY(moved) / interior.roomSize));
        const int movedRoom = RoomIndexAt(movedRow, movedColumn);
        if (movedRoom >= 0) enemy.room = movedRoom;
    }

    for (Projectile& projectile : projectiles) {
        const int steps = std::max(
            1, static_cast<int>(
                   std::ceil(kProjectileSpeed * dt / 6.0f)));
        for (int step = 0;
             step < steps && projectile.distance >= 0; ++step) {
            const float dx = projectile.vx * dt / steps;
            const float dy = projectile.vy * dt / steps;
            projectile.x += dx;
            projectile.y += dy;
            projectile.distance += std::sqrt(dx * dx + dy * dy);
            const Rect shot{
                projectile.x, projectile.y,
                kProjectileSize, kProjectileSize};
            if (HitShield(shot, 1) || HitText(projectile) ||
                HitSpawner(shot, 1) ||
                HitEnemy(shot, 1) || HitRoomWall(shot))
                projectile.distance = -1;
        }
    }
    projectiles.erase(
        std::remove_if(
            projectiles.begin(), projectiles.end(),
            [](const Projectile& projectile) {
                return projectile.distance < 0 ||
                       projectile.distance > 1200;
            }),
        projectiles.end());

    for (Bomb& bomb : bombs) {
        const float travel =
            std::min(bomb.distanceRemaining, kBombSpeed * dt);
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
            DetonateBomb(bomb.x, bomb.y);
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
                        playerHealth = std::max(
                            0, playerHealth - type.contactDamage);
                        PlaySoundEffect(Sound::HitHurt);
                        BeginRecovery(enemy, type);
                        return false;
                    }
                    playerHealth = std::max(
                        0, playerHealth - type.contactDamage);
                    PlaySoundEffect(Sound::HitHurt);
                    return true;
                }
                return false;
            }),
        enemies.end());
}

}  // namespace game

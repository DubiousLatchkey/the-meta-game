#include "state.h"

#include <algorithm>

namespace game {

BackBuffer buffer;
std::filesystem::path goldenDirectory;
std::filesystem::path gameDirectory;
std::map<std::string, EnemyType> types;
std::map<std::string, WallAsset> wallAssets;
std::map<std::string, WeaponStats> playerWeapons;
std::vector<Word> words;
std::map<std::string, int> wordIds;
std::map<std::string, std::vector<int>> phrases;
std::vector<Organ> organs;
Interior interior;
PlayerInteriorState playerInteriorState;
std::array<int, 9> playerInteriorDefaults{{5, 5, 100, 51, 3, 1, 1001, 1, 1}};
float playerMoveImprovementPerStep = 0.10f;
float playerFireImprovementPerStep = 0.10f;
std::vector<Room> rooms;
std::map<std::pair<int, int>, int> roomAt;
std::vector<Spawner> spawners;
std::vector<Enemy> enemies;
std::vector<Projectile> projectiles;
std::vector<Bomb> bombs;
std::vector<Explosion> explosions;
std::vector<EnemyRail> enemyRails;
std::vector<TextBox> textBoxes;
std::vector<ShieldBlock> shieldBlocks;
int levelNumber = 10;
int levelValueWord = -1;
std::vector<int> levelLabel;
std::map<int, std::string> levelMaps;
std::map<int, LevelRegion> levelRegions;
std::string currentMap = "interior";
float playerX = 0, playerY = 0;
int playerHealth = kPlayerMaxHealth;
float playerInvincibility = 0;
bool keys[4]{};
bool shooting = false;
int aimX = 0, aimY = 0;
float shotCooldown = 0;
float bombCooldown = 0;
int lastPlayerRoom = -1;
std::mt19937 random;
std::uint64_t nextSpawnerId = 1;
RunData run;
RogueliteTuning rogueliteTuning;
BossTuning bossTuning;

bool Overlaps(const Rect& a, const Rect& b) {
    return a.x < b.x + b.width && a.x + a.width > b.x &&
           a.y < b.y + b.height && a.y + a.height > b.y;
}

float CenterX(const Rect& rect) { return rect.x + rect.width * 0.5f; }
float CenterY(const Rect& rect) { return rect.y + rect.height * 0.5f; }
float RoomX(const Room& room) {
    float origin = 0;
    for (const auto& [number, region] : levelRegions) {
        (void)number;
        if (region.map == "interior") {
            origin = region.x;
            break;
        }
    }
    return origin +
        room.column * static_cast<float>(interior.roomSize);
}
float RoomY(const Room& room) {
    float origin = 0;
    for (const auto& [number, region] : levelRegions) {
        (void)number;
        if (region.map == "interior") {
            origin = region.y;
            break;
        }
    }
    return origin +
        room.row * static_cast<float>(interior.roomSize);
}
float CameraX() {
    return playerX + kPlayerSize * 0.5f - buffer.width * 0.5f;
}
float CameraY() {
    return playerY + kPlayerSize * 0.5f - buffer.height * 0.5f;
}
float AimWorldX() { return CameraX() + aimX; }
float AimWorldY() { return CameraY() + aimY; }
float SimulationRadius() {
    return kSimulationScreens * static_cast<float>(
        std::max(buffer.width, buffer.height));
}
bool WithinSimulationRange(float x, float y) {
    const float dx = x - (playerX + kPlayerSize * 0.5f);
    const float dy = y - (playerY + kPlayerSize * 0.5f);
    const float radius = SimulationRadius();
    return dx * dx + dy * dy <= radius * radius;
}
const LevelRegion* CurrentLevelRegion() {
    const auto found = levelRegions.find(levelNumber);
    return found == levelRegions.end() ? nullptr : &found->second;
}
float RandomFloat(float minimum, float maximum) {
    return std::uniform_real_distribution<float>(minimum, maximum)(random);
}
int RandomInt(int minimum, int maximum) {
    return std::uniform_int_distribution<int>(minimum, maximum)(random);
}

}  // namespace game

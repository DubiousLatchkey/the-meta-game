#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace game {

inline constexpr int kInitialWidth = 1000;
inline constexpr int kInitialHeight = 720;
inline constexpr int kPlayerSize = 14;
inline constexpr int kPlayerMaxHealth = 3;
inline constexpr float kHealthRegenSeconds = 7.0f;
inline constexpr int kProjectileSize = 5;
inline constexpr float kPlayerSpeed = 260.0f;
inline constexpr float kProjectileSpeed = 620.0f;
inline constexpr float kShotInterval = 0.16f;
inline constexpr int kWallChannelDamage = 5;
inline constexpr float kBombSpeed = 360.0f;
inline constexpr float kBombRange = 192.0f;
inline constexpr float kBombRadius = 52.5f;
inline constexpr float kBombCooldownDuration = 3.0f;
inline constexpr float kExplosionDuration = 0.22f;
inline constexpr float kEnemyFadeDuration = 0.125f;
inline constexpr float kRailFlashDuration = 0.12f;
inline constexpr float kExitWidth = 150.0f;
inline constexpr float kWall = 20.0f;
inline constexpr float kPi = 3.1415926535f;

struct Pixel {
    bool occupied = false;
    std::array<int, 3> rgb{};
};
struct EnemyType {
    std::string id;
    int maxHealth = 1;
    float speed = 1.0f;
    int contactDamage = 1;
    int pixelScale = 7;
    int burstMin = 1;
    int burstMax = 1;
    int spawnWeight = 0;
    float preferredDistance = 0;
    float attackRange = 0;
    float windupSeconds = 0;
    float aimLockSeconds = 0;
    float attackCooldown = 0;
    float attackDistance = 0;
    float attackSpeed = 0;
    std::vector<std::vector<Pixel>> sprite;
};
struct Word {
    std::string id;
    std::vector<std::uint8_t> bytes;
};
struct Organ {
    std::string id;
    std::vector<int> label;
    int valueWord = -1;
    int value = 1;
    int maximum = 1;
    int decrement = 1;
    int room = -1;
};
struct Interior {
    std::string archetype;
    std::string enemy;
    std::string alternateEnemy;
    int roomSize = 640;
    int seed = 7331;
    int spawnersMin = 1, spawnersMax = 4;
    int burstMin = 4, burstMax = 6;
    int alternateBurstMin = 4, alternateBurstMax = 6;
    float secondsMin = 3.0f, secondsMax = 6.0f;
    float speedUnit = 4.0f;
};
struct Room {
    int row = 0, column = 0;
    int pixelRow = 0, pixelColumn = 0;
    int distance = 0;
};
struct Spawner {
    int room = -1;
    std::string enemyType;
    int typeRoll = 0;
    int alternateRoll = 0;
    bool guaranteedArchetype = false;
    float x = 0, y = 0;
    int health = 5;
    float timer = 2.0f;
};
enum class EnemyPhase {
    Approach,
    Windup,
    Attack,
    Recover,
};
struct Enemy {
    int room = -1;
    std::string type;
    float x = 0, y = 0;
    int health = 1;
    int maxHealth = 1;
    float activationRemaining = 0;
    float facing = 0;
    EnemyPhase phase = EnemyPhase::Approach;
    float phaseTimer = 0;
    float targetX = 0, targetY = 0;
    float attackX = 0, attackY = 0;
    float attackRemaining = 0;
};
struct Projectile {
    float x, y, vx, vy, distance;
};
struct Bomb {
    float x, y, vx, vy, distanceRemaining;
};
struct Explosion {
    float x, y, timeRemaining;
};
struct EnemyRail {
    float x, y, dx, dy, length, width, timeRemaining;
};
struct Rect {
    float x, y, width, height;
};
struct WallRect {
    Rect rect;
    int room = -1;
};
struct TextBox {
    Rect rect;
    int word = -1;
    int organ = -1;
    bool value = false;
    bool levelValue = false;
};
struct ShieldBlock {
    Rect rect;
    int organ = -1;
    int health = 10;
};
struct BackBuffer {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HBITMAP previousBitmap = nullptr;
    std::uint32_t* pixels = nullptr;
    int width = 0, height = 0;
};

extern BackBuffer buffer;
extern std::filesystem::path goldenDirectory;
extern std::filesystem::path gameDirectory;
extern std::map<std::string, EnemyType> types;
extern std::vector<Word> words;
extern std::map<std::string, int> wordIds;
extern std::map<std::string, std::vector<int>> phrases;
extern std::vector<Organ> organs;
extern Interior interior;
extern std::vector<Room> rooms;
extern std::map<std::pair<int, int>, int> roomAt;
extern std::vector<Spawner> spawners;
extern std::vector<Enemy> enemies;
extern std::vector<Projectile> projectiles;
extern std::vector<Bomb> bombs;
extern std::vector<Explosion> explosions;
extern std::vector<EnemyRail> enemyRails;
extern std::vector<TextBox> textBoxes;
extern std::vector<ShieldBlock> shieldBlocks;
extern int levelNumber;
extern int levelValueWord;
extern std::vector<int> levelLabel;
extern std::map<int, std::string> levelMaps;
extern std::string currentMap;
extern float playerX, playerY;
extern int playerHealth;
extern bool keys[4];
extern bool shooting;
extern int aimX, aimY;
extern float shotCooldown;
extern float bombCooldown;
extern float healthRegenTimer;
extern int lastPlayerRoom;
extern std::mt19937 random;

bool Overlaps(const Rect& a, const Rect& b);
float CenterX(const Rect& rect);
float CenterY(const Rect& rect);
float RoomX(const Room& room);
float RoomY(const Room& room);
float CameraX();
float CameraY();
float AimWorldX();
float AimWorldY();
float RandomFloat(float minimum, float maximum);
int RandomInt(int minimum, int maximum);

}  // namespace game

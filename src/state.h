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
inline constexpr int kTargetFrameRate = 120;
inline constexpr int kPlayerSize = 3;
inline constexpr int kPlayerRenderSize = 14;
inline constexpr int kPlayerMaxHealth = 3;
inline constexpr int kProjectileSize = 5;
inline constexpr float kPlayerSpeed = 260.0f;
inline constexpr int kWallChannelDamage = 5;
inline constexpr float kBombRadius = 52.5f;
inline constexpr float kExplosionDuration = 0.22f;
inline constexpr float kEnemyFadeDuration = 0.125f;
inline constexpr float kRailFlashDuration = 0.12f;
inline constexpr float kSimulationScreens = 5.0f;
inline constexpr int kAudioSampleDamage = 5;
inline constexpr float kExitWidth = 150.0f;
inline constexpr float kWall = 20.0f;
inline constexpr float kPi = 3.1415926535f;
inline constexpr float kRunArenaWidth = 1500.0f;
inline constexpr float kRunArenaHeight = 1080.0f;
inline constexpr float kRunMapWidth = 3600.0f;
inline constexpr float kRunMapHeight = 1800.0f;
inline constexpr float kPlayerAlterationDisplaySeconds = 1.0f;
inline constexpr float kPlayerAlterationFadeSeconds = 0.65f;

struct Pixel {
    bool occupied = false;
    std::array<int, 3> rgb{};
};
struct WallAsset {
    std::string id;
    std::vector<std::vector<Pixel>> sprite;
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
struct LevelRegion {
    int number = 0;
    std::string map;
    float x = 0, y = 0;
    float width = 1000, height = 720;
    float spawnX = 100, spawnY = 100;
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
    float spawnDelay = 2.0f;
    bool rewardClaimed = false;
    std::uint32_t wave = 0;
    std::uint64_t id = 0;
    int maxActiveChildren = 0;
};

inline void ResetSpawnerTimer(Spawner& spawner, float seconds) {
    spawner.timer = seconds;
    spawner.spawnDelay = seconds;
}
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
    std::uint64_t spawnerId = 0;
};
struct Projectile {
    float x, y, vx, vy, distance;
    bool homing = false;
    bool rocket = false;
    int damage = 1;
    float maxDistance = 1200.0f;
    float explosionRadius = 52.5f;
    float width = 5.0f;
    float homingLateralOffset = 0.0f;
    bool boomerang = false;
    bool returning = false;
    float boomerangReleaseSpeed = 0.0f;
    float boomerangReturnSpeed = 0.0f;
    std::vector<int> boomerangHitEnemies;
    std::vector<int> boomerangHitSpawners;
    std::vector<int> boomerangHitTextBoxes;
    bool boomerangHitSpecialControl = false;
    bool boomerangHitShop = false;
};
struct EnemyProjectile {
    float x = 0, y = 0, vx = 0, vy = 0;
    float trackRemaining = 0;
    bool rocket = false;
    int damage = 1;
};
enum class BossQuadrant {
    NorthWest = 0,
    NorthEast = 1,
    SouthWest = 2,
    SouthEast = 3,
    Count = 4,
};
enum class BossTurretKind {
    Burst,
    Rocket,
};
struct BossTurret {
    BossQuadrant quadrant = BossQuadrant::NorthWest;
    BossTurretKind kind = BossTurretKind::Burst;
    float mountAngle = 0;
    float aim = 0;
    float sweepMin = 0;
    float sweepMax = 0;
    float sweepSpeed = 1.0f;
    float sweepDirection = 1.0f;
    float fireCooldown = 0;
    float burstRemaining = 0;
    float burstGap = 0;
    bool alive = true;
    int targetRoom = -1;
    int health = 8;
};
struct BossFightState {
    bool active = false;
    float centerX = 0;
    float centerY = 0;
    float radiusX = 280.0f;
    float radiusY = 140.0f;
    int health = 80;
    int maxHealth = 80;
    float contactCooldown = 0;
    std::vector<BossTurret> turrets;
    std::vector<EnemyProjectile> projectiles;
};
struct BossTuning {
    int health = 80;
    float radiusX = 280.0f;
    float radiusY = 140.0f;
    int burstTurretsPerQuadrant = 2;
    int rocketTurretsPerQuadrant = 1;
    float sweepDegrees = 70.0f;
    float sweepSpeed = 1.2f;
    float burstCooldown = 1.8f;
    int burstCount = 4;
    float burstInterval = 0.12f;
    float bulletSpeed = 280.0f;
    float rocketSpeed = 220.0f;
    float rocketTrackSeconds = 1.8f;
    float rocketCooldown = 3.2f;
    int turretTargetHealth = 8;
    int contactDamage = 1;
    int interiorRoomSize = 720;
};
struct WeaponStats {
    float cadence = 0.16f;
    // Scales shared cadence upgrades into this weapon's real-time cadence.
    float cadenceEffectScale = 1.0f;
    float decelerationScale = 1.0f;
    float speed = 620.0f;
    float range = 1200.0f;
    float spread = 0.14f;
    float width = 5.0f;
    float radius = 0.0f;
    int count = 1;
    int projectilesPerShot = 1;
    int damage = 1;
    bool homing = false;
    bool explosive = false;
    bool contact = false;
    bool boomerang = false;
};
struct Bomb {
    float x, y, vx, vy, distanceRemaining;
    bool homing = false;
    int damage = 2;
    float radius = 52.5f;
    float homingLateralOffset = 0.0f;
    bool contact = false;
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
    bool menuTitle = false;
    bool startGame = false;
};
struct ShieldBlock {
    Rect rect;
    int organ = -1;
    int health = 10;
};

using RunNodeId = std::uint32_t;
inline constexpr RunNodeId kInvalidRunNode = UINT32_MAX;

enum class RunStatus {
    NotStarted,
    Active,
    Won,
    Lost,
    Abandoned,
};
enum class RunNodeType {
    EnemyArena,
    Interior,
    PlayerInterior,
    BossInterior,
    Shop,
    Boss,
};
enum class EnemyDifficultyStat {
    Size,
    Speed,
    Health,
    Burst,
    Damage,
    SpawnerHealth,
};
struct EnemyDifficultyStages {
    std::uint32_t size = 0;
    std::uint32_t speed = 0;
    std::uint32_t health = 0;
    std::uint32_t burst = 0;
    std::uint32_t damage = 0;
    std::uint32_t spawnerHealth = 0;
};
enum class PortalDirection {
    North,
    East,
    South,
    West,
};
enum class PickupType {
    Health,
    Currency,
    Upgrade,
    Multishot,
    Homing,
    AutoRocket,
};
enum class UpgradeType {
    MaxHealth,
    MoveSpeed,
    FireRate,
    ProjectileDamage,
    BombCooldown,
    Invincibility,
    ExtraProjectile,
};
enum class PrimaryWeapon {
    Standard,
    Railgun,
    Boomerang,
};
enum class SecondaryWeapon {
    Bomb,
    ContactBomb,
    HomingRocket,
};
enum class PlayerAlteration {
    MoveSpeed,
    FireInterval,
    ExtraProjectile,
    Regeneration,
    InfiniteMultishot,
    InfiniteHoming,
    InfiniteAutoRocket,
    DualPrimary,
    DualSecondary,
    ExtraProjectileLimiter,
    SecondMultishot,
    Count,
};
struct PlayerInteriorState {
    std::array<std::uint32_t, 3> repeatableRanks{};
    std::array<bool, 8> permanent{};
    std::array<bool, 12> brokenDoorways{};
    std::array<int, 9> values{{5, 5, 100, 51, 3, 1, 1001, 1, 1}};
};
enum class ShopOfferKind {
    Upgrade,
    PrimaryWeapon,
    SecondaryWeapon,
};
struct RunWave {
    std::uint32_t index = 0;
    std::uint32_t enemyBudget = 0;
    std::uint64_t seed = 0;
    bool completed = false;
};
struct RunPortal {
    RunNodeId destination = kInvalidRunNode;
    PortalDirection direction = PortalDirection::North;
    float center = 0;
    float width = 0;
    bool active = false;
    bool armed = false;
    Rect interiorTrigger{};
};
struct RunPickup {
    PickupType type = PickupType::Currency;
    UpgradeType upgrade = UpgradeType::MaxHealth;
    std::uint32_t amount = 0;
    float x = 0, y = 0;
    bool collected = false;
};
struct RunUpgrade {
    UpgradeType type = UpgradeType::MaxHealth;
    std::uint32_t rank = 0;
};
struct ShopOffer {
    std::uint64_t id = 0;
    ShopOfferKind kind = ShopOfferKind::Upgrade;
    UpgradeType upgrade = UpgradeType::MaxHealth;
    PrimaryWeapon primaryWeapon = PrimaryWeapon::Standard;
    SecondaryWeapon secondaryWeapon = SecondaryWeapon::Bomb;
    std::uint32_t price = 0;
    bool purchased = false;
    float purchaseTimer = 0;
};
struct RunNode {
    RunNodeId id = kInvalidRunNode;
    RunNodeType type = RunNodeType::EnemyArena;
    std::uint32_t depth = 0;
    std::uint64_t seed = 0;
    std::vector<RunNodeId> next;
    std::vector<RunWave> waves;
    std::vector<RunPortal> portals;
    std::vector<RunPickup> pickups;
    std::vector<ShopOffer> shopOffers;
    RunNodeId shopReturnDestination = kInvalidRunNode;
    bool entered = false;
    bool completed = false;
    std::uint32_t activeWave = 0;
    float waveCooldown = 0;
    std::string arenaArchetype;
    bool hardArena = false;
    EnemyDifficultyStat downside = EnemyDifficultyStat::Size;
    bool downsideApplied = false;
    int interiorEntryRoom = -1;
    int playerInteriorSpawnRoom = -1;
    int playerInteriorAlteration = -1;
    int playerInteriorAlterationRoom = -1;
    float playerInteriorAlterationTimer = 0;
    int playerInteriorRoom = -1;
    bool playerInteriorWave = false;
    BossQuadrant bossQuadrant = BossQuadrant::NorthWest;
};
struct RunMapVertex {
    RunNodeId node = kInvalidRunNode;
    float x = 0, y = 0;
};
struct RunData {
    std::uint64_t globalSeed = 0;
    RunStatus status = RunStatus::NotStarted;
    RunNodeId startNode = kInvalidRunNode;
    RunNodeId currentNode = kInvalidRunNode;
    std::vector<RunNode> nodes;
    std::vector<RunMapVertex> mapVertices;
    bool mapActive = false;
    std::vector<RunUpgrade> upgrades;
    std::map<std::string, EnemyDifficultyStages> enemyDifficulty;
    std::uint32_t arenasWithoutEnemyInterior = 0;
    std::uint32_t currency = 0;
    std::uint64_t deathSequence = 0;
    float multishotRemaining = 0;
    float homingRemaining = 0;
    float autoRocketRemaining = 0;
    float autoRocketCooldown = 0;
    PrimaryWeapon primaryWeapon = PrimaryWeapon::Standard;
    SecondaryWeapon secondaryWeapon = SecondaryWeapon::Bomb;
    std::array<bool, 3> primaryWeapons{{true, false, false}};
    std::array<bool, 3> secondaryWeapons{{true, false, false}};
    float primaryCharge = 0;
    float railAimX = 1;
    float railAimY = 0;
    float regenerationTimer = 0;
    std::array<bool, 4> clearedBossQuadrants{};
    BossFightState boss;
};
struct RogueliteTuning {
    std::uint32_t runDepths = 10;
    std::uint32_t branchNodesMin = 3;
    std::uint32_t branchNodesMax = 4;
    std::uint32_t extraBranchEdges = 2;
    std::uint32_t fullAudioWallChancePercent = 50;
    std::uint32_t healthPickupDropChance = 10;
    std::uint32_t powerupDropChance = 20;
    std::uint32_t shopPrice = 5;
    float shopPurchaseSeconds = 1.0f;
    std::uint32_t arenaWaves = 3;
    std::uint32_t interiorWaves = 2;
    float waveCooldown = 1.5f;
    float powerupSeconds = 10.0f;
    float portalWidth = 110.0f;
    float playerInvincibilitySeconds = 0.5f;
    std::uint32_t playerInteriorRoomSize = 1500;
    std::uint32_t playerInteriorWaveSpawners = 8;
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
extern std::map<std::string, WallAsset> wallAssets;
extern std::map<std::string, WeaponStats> playerWeapons;
extern std::vector<Word> words;
extern std::map<std::string, int> wordIds;
extern std::map<std::string, std::vector<int>> phrases;
extern std::vector<Organ> organs;
extern Interior interior;
extern PlayerInteriorState playerInteriorState;
extern std::array<int, 9> playerInteriorDefaults;
extern float playerMoveImprovementPerStep;
extern float playerFireImprovementPerStep;
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
extern std::map<int, LevelRegion> levelRegions;
extern std::string currentMap;
extern float playerX, playerY;
extern int playerHealth;
extern float playerInvincibility;
extern bool keys[4];
extern bool shooting;
extern int aimX, aimY;
extern float shotCooldown;
extern float bombCooldown;
extern int lastPlayerRoom;
extern std::mt19937 random;
extern std::uint64_t nextSpawnerId;
extern RunData run;
extern RogueliteTuning rogueliteTuning;
extern BossTuning bossTuning;

bool Overlaps(const Rect& a, const Rect& b);
float CenterX(const Rect& rect);
float CenterY(const Rect& rect);
float RoomX(const Room& room);
float RoomY(const Room& room);
float CameraX();
float CameraY();
float AimWorldX();
float AimWorldY();
float SimulationRadius();
bool WithinSimulationRange(float x, float y);
const LevelRegion* CurrentLevelRegion();
float RandomFloat(float minimum, float maximum);
int RandomInt(int minimum, int maximum);

}  // namespace game

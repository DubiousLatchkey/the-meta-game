#include "state.h"

#include <algorithm>

namespace game {

BackBuffer buffer;
std::filesystem::path goldenDirectory;
std::filesystem::path gameDirectory;
std::map<std::string, EnemyType> types;
std::vector<Word> words;
std::map<std::string, int> wordIds;
std::map<std::string, std::vector<int>> phrases;
std::vector<Organ> organs;
Interior interior;
std::vector<Room> rooms;
std::map<std::pair<int, int>, int> roomAt;
std::vector<Spawner> spawners;
std::vector<Enemy> enemies;
std::vector<Projectile> projectiles;
std::vector<Bomb> bombs;
std::vector<Explosion> explosions;
std::vector<TextBox> textBoxes;
std::vector<ShieldBlock> shieldBlocks;
float playerX = 0, playerY = 0;
int playerHealth = kPlayerMaxHealth;
bool keys[4]{};
bool shooting = false;
int aimX = 0, aimY = 0;
float shotCooldown = 0;
float bombCooldown = 0;
float healthRegenTimer = kHealthRegenSeconds;
int lastPlayerRoom = -1;
std::mt19937 random;

bool Overlaps(const Rect& a, const Rect& b) {
    return a.x < b.x + b.width && a.x + a.width > b.x &&
           a.y < b.y + b.height && a.y + a.height > b.y;
}

float CenterX(const Rect& rect) { return rect.x + rect.width * 0.5f; }
float CenterY(const Rect& rect) { return rect.y + rect.height * 0.5f; }
float RoomX(const Room& room) {
    return room.column * static_cast<float>(interior.roomSize);
}
float RoomY(const Room& room) {
    return room.row * static_cast<float>(interior.roomSize);
}
float CameraX() {
    return playerX + kPlayerSize * 0.5f - buffer.width * 0.5f;
}
float CameraY() {
    return playerY + kPlayerSize * 0.5f - buffer.height * 0.5f;
}
float AimWorldX() { return CameraX() + aimX; }
float AimWorldY() { return CameraY() + aimY; }
float RandomFloat(float minimum, float maximum) {
    return std::uniform_real_distribution<float>(minimum, maximum)(random);
}
int RandomInt(int minimum, int maximum) {
    return std::uniform_int_distribution<int>(minimum, maximum)(random);
}

}  // namespace game

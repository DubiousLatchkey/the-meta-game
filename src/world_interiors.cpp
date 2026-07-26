#include "world.h"

#include <algorithm>
#include <array>
#include <queue>

#include "world_internal.h"

namespace game {

void BuildInteriorWorld(std::uint64_t seed) {
    GenerateRooms(seed == 0
        ? static_cast<std::uint64_t>(interior.seed) : seed);
    BuildWorldTextBoxes();
    BuildShields();
}

void BuildPlayerInteriorWorld(std::uint64_t, int& spawnRoom) {
    rooms.clear();
    roomAt.clear();
    roomConnections.clear();
    textBoxes.clear();
    spawners.clear();
    shieldBlocks.clear();
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column) {
            const int index = static_cast<int>(rooms.size());
            rooms.push_back({row, column, row, column, 0});
            roomAt[{row, column}] = index;
        }
    // Every physical doorway exists in the fixed facility. Unbroken ones are
    // protected by persistent shields; broken doorways remain open on future
    // visits.
    for (int doorway = 0; doorway < 12; ++doorway) {
        OpenPlayerInteriorDoorway(doorway);
        if (!playerInteriorState.brokenDoorways[doorway])
            shieldBlocks.push_back(
                {PlayerInteriorDoorwayRect(doorway), doorway, 10});
    }
    spawnRoom = RoomIndexAt(1, 1);
    std::queue<int> pending;
    rooms[spawnRoom].distance = 0;
    std::array<bool, 9> visited{};
    visited[spawnRoom] = true;
    pending.push(spawnRoom);
    while (!pending.empty()) {
        const int current = pending.front();
        pending.pop();
        const int dr[]{-1, 1, 0, 0}, dc[]{0, 0, -1, 1};
        for (int direction = 0; direction < 4; ++direction) {
            const int next = RoomIndexAt(
                rooms[current].row + dr[direction],
                rooms[current].column + dc[direction]);
            if (next >= 0 && !visited[next]) {
                visited[next] = true;
                rooms[next].distance = rooms[current].distance + 1;
                pending.push(next);
            }
        }
    }
    const Room& room = rooms[spawnRoom];
    playerX = RoomX(room) + interior.roomSize * 0.5f -
        kPlayerSize * 0.5f;
    playerY = RoomY(room) + interior.roomSize * 0.5f -
        kPlayerSize * 0.5f;
}

void BossSpriteMidpoint(int& midRow, int& midCol) {
    const EnemyType& giant = types.at("boss");
    midRow = static_cast<int>(giant.sprite.size()) / 2;
    midCol = 0;
    for (const auto& row : giant.sprite)
        midCol = std::max(midCol, static_cast<int>(row.size()));
    midCol /= 2;
}

BossQuadrant BossQuadrantForCell(
    int row, int column, int midRow, int midCol) {
    const bool north = row < midRow;
    const bool west = column < midCol;
    if (north && west) return BossQuadrant::NorthWest;
    if (north && !west) return BossQuadrant::NorthEast;
    if (!north && west) return BossQuadrant::SouthWest;
    return BossQuadrant::SouthEast;
}

void BuildBossInteriorWorld(
    std::uint64_t seed, BossQuadrant quadrant, int& spawnRoom) {
    rooms.clear();
    roomAt.clear();
    roomConnections.clear();
    textBoxes.clear();
    spawners.clear();
    shieldBlocks.clear();
    interior.archetype = "boss";
    interior.enemy = "circle";
    interior.roomSize = bossTuning.interiorRoomSize;
    const EnemyType& giant = types.at("boss");
    int midRow = 0, midCol = 0;
    BossSpriteMidpoint(midRow, midCol);
    for (int row = 0; row < static_cast<int>(giant.sprite.size()); ++row)
        for (int column = 0;
             column < static_cast<int>(giant.sprite[row].size()); ++column)
            if (giant.sprite[row][column].occupied &&
                BossQuadrantForCell(row, column, midRow, midCol) ==
                    quadrant) {
                const int index = static_cast<int>(rooms.size());
                rooms.push_back({row, column, row, column, 0});
                roomAt[{row, column}] = index;
            }
    std::vector<int> quadrantRooms;
    for (int index = 0; index < static_cast<int>(rooms.size()); ++index)
        quadrantRooms.push_back(index);
    for (Room& room : rooms) room.distance = -1;
    std::vector<int> turretRooms;
    const int turretCount = bossTuning.burstTurretsPerQuadrant +
        bossTuning.rocketTurretsPerQuadrant;
    if (!BuildRoomBloomGraph(seed, turretCount, spawnRoom, turretRooms))
        spawnRoom = quadrantRooms.empty() ? 0 : quadrantRooms.front();
    std::queue<int> pending;
    std::vector<int> distances(rooms.size(), -1);
    distances[spawnRoom] = 0;
    pending.push(spawnRoom);
    while (!pending.empty()) {
        const int current = pending.front();
        pending.pop();
        rooms[current].distance = distances[current];
        static constexpr int odr[]{-1, 1, 0, 0};
        static constexpr int odc[]{0, 0, -1, 1};
        for (int direction = 0; direction < 4; ++direction) {
            const int next = RoomIndexAt(
                rooms[current].row + odr[direction],
                rooms[current].column + odc[direction]);
            if (next >= 0 && RoomsConnected(current, next) &&
                distances[next] < 0) {
                distances[next] = distances[current] + 1;
                pending.push(next);
            }
        }
    }
    const Room& start = rooms[spawnRoom];
    playerX = RoomX(start) + interior.roomSize * 0.5f -
        kPlayerSize * 0.5f;
    playerY = RoomY(start) + interior.roomSize * 0.5f -
        kPlayerSize * 0.5f;
    random.seed(static_cast<unsigned>(seed ^ (seed >> 32)));
    for (int roomIndex : quadrantRooms) {
        if (distances[roomIndex] < 0) continue;
        const Room& room = rooms[roomIndex];
        const int count =
            RandomInt(interior.spawnersMin, interior.spawnersMax);
        for (int i = 0; i < count; ++i) {
            float x = RoomX(room) +
                RandomFloat(90, interior.roomSize - 120.0f);
            float y = RoomY(room) +
                RandomFloat(100, interior.roomSize - 120.0f);
            Spawner spawner;
            spawner.room = roomIndex;
            spawner.guaranteedArchetype = i == 0;
            spawner.enemyType = interior.enemy;
            spawner.x = x;
            spawner.y = y;
            spawner.health = 5;
            ResetSpawnerTimer(
                spawner, RandomFloat(
                    interior.secondsMin, interior.secondsMax));
            spawner.id = nextSpawnerId++;
            spawners.push_back(std::move(spawner));
        }
    }
}

void GenerateInteriorSpawners(std::uint64_t seed) {
    GenerateSpawners(seed);
}

std::pair<int, int> PlayerInteriorDoorwayRooms(int doorway) {
    if (doorway < 0 || doorway >= 12) return {-1, -1};
    if (doorway < 6) {
        const int row = doorway / 2;
        const int column = doorway % 2;
        return {RoomIndexAt(row, column), RoomIndexAt(row, column + 1)};
    }
    const int offset = doorway - 6;
    const int row = offset / 3;
    const int column = offset % 3;
    return {RoomIndexAt(row, column), RoomIndexAt(row + 1, column)};
}

Rect PlayerInteriorDoorwayRect(int doorway) {
    const auto [first, second] = PlayerInteriorDoorwayRooms(doorway);
    if (first < 0 || second < 0) return {};
    const Room& room = rooms[first];
    const float edge = (interior.roomSize - kExitWidth) * 0.5f;
    if (doorway < 6)
        return {RoomX(room) + interior.roomSize - kWall,
                RoomY(room) + edge, kWall * 2.0f, kExitWidth};
    return {RoomX(room) + edge,
            RoomY(room) + interior.roomSize - kWall,
            kExitWidth, kWall * 2.0f};
}

void OpenPlayerInteriorDoorway(int doorway) {
    const auto [first, second] = PlayerInteriorDoorwayRooms(doorway);
    if (first >= 0 && second >= 0)
        roomConnections.insert(RoomEdge(first, second));
}

}  // namespace game

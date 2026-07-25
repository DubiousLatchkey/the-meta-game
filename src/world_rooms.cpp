#include "world.h"

#include <algorithm>
#include <array>
#include <climits>
#include <numeric>
#include <queue>

#include "world_internal.h"

namespace game {

std::set<std::pair<int, int>> roomConnections;

std::pair<int, int> RoomEdge(int first, int second) {
    return first < second
        ? std::make_pair(first, second)
        : std::make_pair(second, first);
}

std::uint64_t ConnectionSeed(
    std::uint64_t seed, int first, int second) {
    std::uint64_t value = seed ^
        (static_cast<std::uint64_t>(first + 1) * 0x9e3779b97f4a7c15ULL) ^
        (static_cast<std::uint64_t>(second + 1) * 0xbf58476d1ce4e5b9ULL);
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

namespace {

struct BloomFrontier {
    int room = -1;
    int length = 0;
    std::vector<std::pair<int, int>> rollback;
};

bool BuildBloomGraph(
    std::uint64_t seed, int organCount, int& spawn,
    std::vector<int>& organRooms) {
    constexpr int kPathChance = 78;
    constexpr int kBranchChance = 24;
    constexpr int kBranchOrganChance = 55;
    constexpr int kMaximumPathLength = 2;
    if (rooms.empty()) return false;
    roomConnections.clear();
    organRooms.clear();
    static constexpr int dr[]{-1, 1, 0, 0};
    static constexpr int dc[]{0, 0, -1, 1};
    const auto neighborCount = [&](int room) {
        int count = 0;
        for (int direction = 0; direction < 4; ++direction) {
            const int next = RoomIndexAt(
                rooms[room].row + dr[direction],
                rooms[room].column + dc[direction]);
            if (next >= 0) ++count;
        }
        return count;
    };
    // Prefer a seeded spawn that can open exactly one exit.
    spawn = static_cast<int>(
        ConnectionSeed(seed, 0, static_cast<int>(rooms.size())) %
        rooms.size());
    for (int offset = 0; offset < static_cast<int>(rooms.size()); ++offset) {
        const int candidate =
            (spawn + offset) % static_cast<int>(rooms.size());
        if (neighborCount(candidate) > 0) {
            spawn = candidate;
            break;
        }
    }
    if (neighborCount(spawn) == 0) return false;
    std::vector<bool> visited(rooms.size(), false);
    visited[spawn] = true;
    std::vector<BloomFrontier> pending{{spawn, 0, {}}};
    int decision = 0;
    while (!pending.empty() &&
           static_cast<int>(organRooms.size()) < organCount) {
        BloomFrontier path = std::move(pending.back());
        pending.pop_back();
        if (path.length >= kMaximumPathLength && path.room != spawn) {
            organRooms.push_back(path.room);
            continue;
        }
        std::vector<int> candidates;
        const Room& room = rooms[path.room];
        for (int direction = 0; direction < 4; ++direction) {
            const int next = RoomIndexAt(
                room.row + dr[direction], room.column + dc[direction]);
            if (next >= 0 && !visited[next]) candidates.push_back(next);
        }
        std::sort(candidates.begin(), candidates.end(), [&](int first, int second) {
            return ConnectionSeed(seed, path.room, first + decision) <
                ConnectionSeed(seed, path.room, second + decision);
        });
        std::vector<int> opened;
        if (path.room == spawn) {
            // The entrance is always a single corridor; never a fork.
            if (!candidates.empty()) opened.push_back(candidates.front());
        } else {
            for (int next : candidates) {
                const std::uint64_t roll =
                    ConnectionSeed(seed, path.room + decision++, next);
                if (static_cast<int>(roll % 100) >= kPathChance) continue;
                if (!opened.empty() &&
                    static_cast<int>((roll >> 16) % 100) >= kBranchChance)
                    continue;
                opened.push_back(next);
            }
        }
        if (opened.empty()) {
            if (path.room != spawn) organRooms.push_back(path.room);
            continue;
        }
        const bool branch = opened.size() > 1;
        // Forks are organ-eligible so players can claim a reward without
        // walking every leaf. Dead ends remain eligible below.
        if (branch &&
            static_cast<int>(
                ConnectionSeed(seed, path.room, decision++) % 100) <
                kBranchOrganChance)
            organRooms.push_back(path.room);
        for (int next : opened) {
            visited[next] = true;
            const auto edge = RoomEdge(path.room, next);
            roomConnections.insert(edge);
            BloomFrontier child;
            child.room = next;
            child.length = branch ? 1 : path.length + 1;
            if (!branch) child.rollback = path.rollback;
            child.rollback.push_back(edge);
            pending.push_back(std::move(child));
        }
        if (static_cast<int>(organRooms.size()) >= organCount) break;
    }
    if (static_cast<int>(organRooms.size()) < organCount) return false;
    // Once the last organ lands, retract unfinished branches to their last
    // committed junction so leftover corridors become sealed rooms.
    for (const BloomFrontier& path : pending)
        for (const auto& edge : path.rollback)
            roomConnections.erase(edge);
    return true;
}

void AddSplitHorizontal(
    std::vector<WallRect>& walls, int room, float x, float y, bool exit) {
    const float size = static_cast<float>(interior.roomSize);
    if (!exit) walls.push_back({{x, y, size, kWall}, room});
    else {
        const float edge = (size - kExitWidth) * 0.5f;
        walls.push_back({{x, y, edge, kWall}, room});
        walls.push_back(
            {{x + edge + kExitWidth, y, edge, kWall}, room});
    }
}

void AddSplitVertical(
    std::vector<WallRect>& walls, int room, float x, float y, bool exit) {
    const float size = static_cast<float>(interior.roomSize);
    if (!exit) walls.push_back({{x, y, kWall, size}, room});
    else {
        const float edge = (size - kExitWidth) * 0.5f;
        walls.push_back({{x, y, kWall, edge}, room});
        walls.push_back(
            {{x, y + edge + kExitWidth, kWall, edge}, room});
    }
}

}  // namespace

void GenerateRooms(std::uint64_t seed) {
    rooms.clear();
    roomAt.clear();
    roomConnections.clear();
    EnemyType& giant = types.at(interior.archetype);
    for (int row = 0; row < static_cast<int>(giant.sprite.size()); ++row)
        for (int column = 0;
             column < static_cast<int>(giant.sprite[row].size()); ++column)
            if (giant.sprite[row][column].occupied) {
                const int index = static_cast<int>(rooms.size());
                rooms.push_back({row, column, row, column, 0});
                roomAt[{row, column}] = index;
            }

    struct CandidateConnection {
        int first = 0, second = 0;
    };
    std::vector<CandidateConnection> candidates;
    const int connectionDr[2]{1, 0};
    const int connectionDc[2]{0, 1};
    for (int index = 0; index < static_cast<int>(rooms.size()); ++index)
        for (int direction = 0; direction < 2; ++direction) {
            const int neighbor = RoomIndexAt(
                rooms[index].row + connectionDr[direction],
                rooms[index].column + connectionDc[direction]);
            if (neighbor >= 0)
                candidates.push_back({index, neighbor});
        }

    int start = 0;
    for (int i = 1; i < static_cast<int>(rooms.size()); ++i)
        if (rooms[i].column < rooms[start].column ||
            (rooms[i].column == rooms[start].column &&
             std::abs(rooms[i].row - 3) < std::abs(rooms[start].row - 3)))
            start = i;

    std::vector<int> bloomOrgans;
    bool generated = false;
    for (int attempt = 0; attempt < 256 && !generated; ++attempt)
        generated = BuildBloomGraph(
            ConnectionSeed(seed, attempt, static_cast<int>(rooms.size())),
            static_cast<int>(organs.size()), start, bloomOrgans);
    if (generated) {
        for (int index = 0; index < static_cast<int>(organs.size()); ++index) {
            organs[index].room = bloomOrgans[index];
            UpdateValueWord(organs[index]);
        }
        std::vector<int> distances(rooms.size(), -1);
        std::queue<int> bloomPending;
        distances[start] = 0;
        bloomPending.push(start);
        static constexpr int bloomDr[]{-1, 1, 0, 0};
        static constexpr int bloomDc[]{0, 0, -1, 1};
        while (!bloomPending.empty()) {
            const int current = bloomPending.front();
            bloomPending.pop();
            for (int direction = 0; direction < 4; ++direction) {
                const int next = RoomIndexAt(
                    rooms[current].row + bloomDr[direction],
                    rooms[current].column + bloomDc[direction]);
                if (next >= 0 && RoomsConnected(current, next) &&
                    distances[next] < 0) {
                    distances[next] = distances[current] + 1;
                    bloomPending.push(next);
                }
            }
        }
        for (int room = 0; room < static_cast<int>(rooms.size()); ++room)
            rooms[room].distance = distances[room];
        const Room& startRoom = rooms[start];
        playerX = RoomX(startRoom) + interior.roomSize * 0.5f -
            kPlayerSize * 0.5f;
        playerY = RoomY(startRoom) + interior.roomSize * 0.5f -
            kPlayerSize * 0.5f;
        return;
    }

    // Build many deterministic spanning trees and keep the clearest one. Its
    // diameter is the main path; every side path must rejoin it within two
    // rooms. Among those trees, prefer fewer junctions and fewer branches.
    std::vector<int> fullDistances(rooms.size(), -1);
    std::queue<int> pending;
    fullDistances[start] = 0;
    pending.push(start);
    const int dr[4]{-1, 1, 0, 0};
    const int dc[4]{0, 0, -1, 1};
    while (!pending.empty()) {
        const int current = pending.front();
        pending.pop();
        for (int direction = 0; direction < 4; ++direction) {
            const int next = RoomIndexAt(
                rooms[current].row + dr[direction],
                rooms[current].column + dc[direction]);
            if (next >= 0 && fullDistances[next] < 0) {
                fullDistances[next] = fullDistances[current] + 1;
                pending.push(next);
            }
        }
    }

    std::set<std::pair<int, int>> bestConnections;
    std::vector<int> bestBackbone;
    int bestJunctions = INT_MAX;
    int bestLeaves = INT_MAX;
    int bestBranchDistance = INT_MAX;
    int bestBackboneLength = -1;
    std::uint64_t bestTie = UINT64_MAX;
    for (int attempt = 0; attempt < 512; ++attempt) {
        std::vector<CandidateConnection> ordered = candidates;
        const std::uint64_t attemptSeed =
            ConnectionSeed(seed, attempt, static_cast<int>(rooms.size()));
        std::sort(ordered.begin(), ordered.end(),
            [&](const CandidateConnection& first,
                const CandidateConnection& second) {
                return ConnectionSeed(
                           attemptSeed, first.first, first.second) <
                    ConnectionSeed(
                           attemptSeed, second.first, second.second);
            });
        std::vector<int> parent(rooms.size());
        for (int room = 0; room < static_cast<int>(rooms.size()); ++room)
            parent[room] = room;
        const auto root = [&](int room) {
            while (parent[room] != room) {
                parent[room] = parent[parent[room]];
                room = parent[room];
            }
            return room;
        };
        std::set<std::pair<int, int>> connections;
        for (const CandidateConnection& edge : ordered) {
            const int firstRoot = root(edge.first);
            const int secondRoot = root(edge.second);
            if (firstRoot == secondRoot) continue;
            parent[firstRoot] = secondRoot;
            connections.insert(RoomEdge(edge.first, edge.second));
        }
        std::vector<int> degree(rooms.size(), 0);
        for (const auto& edge : connections) {
            ++degree[edge.first];
            ++degree[edge.second];
        }
        if (degree[start] < 2) continue;
        const auto farthestFrom = [&](int source, std::vector<int>* parents) {
            std::vector<int> distance(rooms.size(), -1);
            if (parents) parents->assign(rooms.size(), -1);
            std::queue<int> queue;
            distance[source] = 0;
            queue.push(source);
            int farthest = source;
            while (!queue.empty()) {
                const int current = queue.front();
                queue.pop();
                if (distance[current] > distance[farthest])
                    farthest = current;
                const Room& value = rooms[current];
                for (int direction = 0; direction < 4; ++direction) {
                    const int next = RoomIndexAt(
                        value.row + dr[direction],
                        value.column + dc[direction]);
                    if (next >= 0 && distance[next] < 0 &&
                        connections.count(RoomEdge(current, next))) {
                        distance[next] = distance[current] + 1;
                        if (parents) (*parents)[next] = current;
                        queue.push(next);
                    }
                }
            }
            return farthest;
        };
        const int diameterStart = farthestFrom(start, nullptr);
        std::vector<int> pathParent;
        const int diameterEnd = farthestFrom(diameterStart, &pathParent);
        std::vector<int> backbone;
        for (int room = diameterEnd; room >= 0;
             room = pathParent[room]) {
            backbone.push_back(room);
            if (room == diameterStart) break;
        }
        std::vector<int> branchDistance(rooms.size(), -1);
        std::queue<int> branchQueue;
        for (int room : backbone) {
            branchDistance[room] = 0;
            branchQueue.push(room);
        }
        while (!branchQueue.empty()) {
            const int current = branchQueue.front();
            branchQueue.pop();
            const Room& value = rooms[current];
            for (int direction = 0; direction < 4; ++direction) {
                const int next = RoomIndexAt(
                    value.row + dr[direction],
                    value.column + dc[direction]);
                if (next >= 0 && branchDistance[next] < 0 &&
                    connections.count(RoomEdge(current, next))) {
                    branchDistance[next] = branchDistance[current] + 1;
                    branchQueue.push(next);
                }
            }
        }
        const int maximumBranch = *std::max_element(
            branchDistance.begin(), branchDistance.end());
        if (maximumBranch > 2) continue;
        const int junctions = static_cast<int>(std::count_if(
            degree.begin(), degree.end(),
            [](int value) { return value > 2; }));
        const int leaves = static_cast<int>(std::count(
            degree.begin(), degree.end(), 1));
        const int totalBranchDistance = std::accumulate(
            branchDistance.begin(), branchDistance.end(), 0);
        const std::uint64_t tie =
            ConnectionSeed(seed, attempt, diameterEnd);
        if (junctions < bestJunctions ||
            (junctions == bestJunctions && leaves < bestLeaves) ||
            (junctions == bestJunctions && leaves == bestLeaves &&
             totalBranchDistance < bestBranchDistance) ||
            (junctions == bestJunctions && leaves == bestLeaves &&
             totalBranchDistance == bestBranchDistance &&
             static_cast<int>(backbone.size()) > bestBackboneLength) ||
            (junctions == bestJunctions && leaves == bestLeaves &&
             totalBranchDistance == bestBranchDistance &&
             static_cast<int>(backbone.size()) == bestBackboneLength &&
             tie < bestTie)) {
            bestJunctions = junctions;
            bestLeaves = leaves;
            bestBranchDistance = totalBranchDistance;
            bestBackboneLength = static_cast<int>(backbone.size());
            bestTie = tie;
            bestConnections = std::move(connections);
            bestBackbone = std::move(backbone);
        }
    }

    if (!bestConnections.empty()) {
        roomConnections = std::move(bestConnections);
        std::vector<int> organRooms;
        std::vector<int> degree(rooms.size(), 0);
        for (const auto& edge : roomConnections) {
            ++degree[edge.first];
            ++degree[edge.second];
        }
        for (int room = 0; room < static_cast<int>(rooms.size()); ++room)
            if (degree[room] == 1 && room != start)
                organRooms.push_back(room);
        std::sort(organRooms.begin(), organRooms.end(), [&](int first, int second) {
            return ConnectionSeed(seed, first, 0) <
                ConnectionSeed(seed, second, 0);
        });
        for (std::size_t fraction = 1;
             organRooms.size() < organs.size() &&
             fraction <= organs.size(); ++fraction) {
            const int room = bestBackbone[
                (bestBackbone.size() - 1) * fraction /
                (organs.size() + 1)];
            if (room != start &&
                std::find(organRooms.begin(), organRooms.end(), room) ==
                    organRooms.end())
                organRooms.push_back(room);
        }
        for (int index = 0; index < static_cast<int>(organs.size()); ++index) {
            organs[index].room = organRooms[
                std::min(index, static_cast<int>(organRooms.size()) - 1)];
            UpdateValueWord(organs[index]);
        }
    } else {
        // Corrupt or unusually narrow custom sprites still get a connected
        // interior rather than preventing the world from loading.
        for (const CandidateConnection& edge : candidates)
            roomConnections.insert(RoomEdge(edge.first, edge.second));
        std::vector<int> order(rooms.size());
        for (int index = 0; index < static_cast<int>(rooms.size()); ++index)
            order[index] = index;
        std::sort(order.begin(), order.end(), [&](int first, int second) {
            return fullDistances[first] > fullDistances[second];
        });
        for (int index = 0; index < static_cast<int>(organs.size()); ++index) {
            organs[index].room = order[
                std::min(index, static_cast<int>(order.size()) - 1)];
            UpdateValueWord(organs[index]);
        }
    }

    std::vector<int> distances(rooms.size(), -1);
    distances[start] = 0;
    pending.push(start);
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
    for (int index = 0; index < static_cast<int>(rooms.size()); ++index)
        rooms[index].distance = distances[index];
    const Room& startRoom = rooms[start];
    playerX = RoomX(startRoom) + interior.roomSize * 0.5f -
              kPlayerSize * 0.5f;
    playerY = RoomY(startRoom) + interior.roomSize * 0.5f -
              kPlayerSize * 0.5f;
}

void GenerateSpawners(std::uint64_t seed) {
    spawners.clear();
    random.seed(static_cast<unsigned>(seed ^ (seed >> 32)));
    for (int roomIndex = 0;
         roomIndex < static_cast<int>(rooms.size()); ++roomIndex) {
        const Room& room = rooms[roomIndex];
        if (room.distance < 0) continue;
        const int count =
            RandomInt(interior.spawnersMin, interior.spawnersMax);
        for (int i = 0; i < count; ++i) {
            float x = 0, y = 0;
            for (int attempt = 0; attempt < 30; ++attempt) {
                x = RoomX(room) +
                    RandomFloat(90, interior.roomSize - 120.0f);
                y = RoomY(room) +
                    RandomFloat(100, interior.roomSize - 120.0f);
                Rect candidate{x, y, 30, 30};
                bool clear = true;
                bool organRoom = false;
                for (const Organ& organ : organs)
                    if (organ.room == roomIndex) organRoom = true;
                if (organRoom) {
                    Rect center{
                        RoomX(room) + interior.roomSize * 0.3f,
                        RoomY(room) + interior.roomSize * 0.37f,
                        interior.roomSize * 0.4f,
                        interior.roomSize * 0.25f};
                    clear = !Overlaps(candidate, center);
                }
                for (const Spawner& spawner : spawners)
                    if (spawner.room == roomIndex &&
                        Overlaps(
                            candidate,
                            {spawner.x - 20, spawner.y - 20, 70, 70}))
                        clear = false;
                if (HitsShield(candidate)) clear = false;
                if (clear) break;
            }
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

void ResetPlay() {
    enemies.clear();
    projectiles.clear();
    bombs.clear();
    explosions.clear();
    enemyRails.clear();
    playerHealth = kPlayerMaxHealth;
    playerInvincibility = 0;
    bombCooldown = 0;
    lastPlayerRoom = -1;
    SelectCurrentLevel();
    if (currentMap != "interior") return;
    GenerateRooms(static_cast<std::uint64_t>(interior.seed));
    BuildWorldTextBoxes();
    BuildShields();
    GenerateSpawners(interior.seed);
}

int RoomIndexAt(int row, int column) {
    const auto found = roomAt.find({row, column});
    return found == roomAt.end() ? -1 : found->second;
}

bool RoomsConnected(int first, int second) {
    return first >= 0 && second >= 0 &&
        roomConnections.count(RoomEdge(first, second)) != 0;
}

int RoomAtWorld(float x, float y) {
    float originX = 0;
    float originY = 0;
    for (const auto& [number, region] : levelRegions) {
        (void)number;
        if (region.map == "interior") {
            originX = region.x;
            originY = region.y;
            break;
        }
    }
    const int column = static_cast<int>(
        std::floor((x - originX) / interior.roomSize));
    const int row = static_cast<int>(
        std::floor((y - originY) / interior.roomSize));
    return RoomIndexAt(row, column);
}

int CurrentRoom() {
    return RoomAtWorld(
        playerX + kPlayerSize * 0.5f,
        playerY + kPlayerSize * 0.5f);
}

std::vector<WallRect> BuildWalls() {
    std::vector<WallRect> walls;
    for (int index = 0; index < static_cast<int>(rooms.size()); ++index) {
        const Room& room = rooms[index];
        const float x = RoomX(room), y = RoomY(room);
        AddSplitHorizontal(
            walls, index, x, y,
            RoomsConnected(
                index, RoomIndexAt(room.row - 1, room.column)));
        AddSplitHorizontal(
            walls, index, x, y + interior.roomSize - kWall,
            RoomsConnected(
                index, RoomIndexAt(room.row + 1, room.column)));
        AddSplitVertical(
            walls, index, x, y,
            RoomsConnected(
                index, RoomIndexAt(room.row, room.column - 1)));
        AddSplitVertical(
            walls, index, x + interior.roomSize - kWall, y,
            RoomsConnected(
                index, RoomIndexAt(room.row, room.column + 1)));
    }
    return walls;
}

bool HitsWall(const Rect& rectangle, int* room) {
    for (const WallRect& wall : BuildWalls())
        if (Overlaps(rectangle, wall.rect)) {
            if (room) *room = wall.room;
            return true;
        }
    return false;
}

bool InRoomNetwork(const Rect& rectangle) {
    return RoomAtWorld(
               CenterX(rectangle), CenterY(rectangle)) >= 0 &&
           !HitsWall(rectangle);
}

}  // namespace game

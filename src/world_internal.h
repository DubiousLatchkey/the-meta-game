#pragma once

// Shared declarations for world_*.cpp translation units only.
// Nothing outside the world split should include this header.

#include <cstdint>
#include <set>
#include <utility>

#include "state.h"

namespace game {

// Room connection graph, shared by room generation (world_rooms.cpp)
// and interior world building (world_interiors.cpp).
extern std::set<std::pair<int, int>> roomConnections;
std::pair<int, int> RoomEdge(int first, int second);
std::uint64_t ConnectionSeed(std::uint64_t seed, int first, int second);

// world_rooms.cpp, called from world_interiors.cpp and world_lua.cpp.
void GenerateRooms(std::uint64_t seed);
bool BuildRoomBloomGraph(
    std::uint64_t seed, int targetCount, int& spawnRoom,
    std::vector<int>& targetRooms);
void GenerateSpawners(std::uint64_t seed);
void ResetPlay();

}  // namespace game

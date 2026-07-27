#pragma once

#include <cstddef>
#include <filesystem>

#include "state.h"

namespace game {

bool ReloadWorld(bool reset);
bool LoadGoldenWorldForTools();
bool GenerateInteriorGraphGallery(
    const std::filesystem::path& outputDirectory, int count);
bool GenerateBossInteriorGraphGallery(
    const std::filesystem::path& outputDirectory, int count);
bool GenerateInteriorGraphTuner(
    const std::filesystem::path& outputFile);
void MarkMutationsDirty();
void UpdateMutationPersistence(float dt);
bool FlushMutations();
bool DecrementWorldConstant(std::size_t index);
// Restores only Word bytes captured from world.lua before ApplyMutations().
void ResetWordMutations();
void BuildInteriorWorld(std::uint64_t seed = 0);
void BuildPlayerInteriorWorld(std::uint64_t seed, int& spawnRoom);
void BuildBossInteriorWorld(
    std::uint64_t seed, BossQuadrant quadrant, int& spawnRoom);
BossQuadrant BossQuadrantForCell(
    int row, int column, int midRow, int midCol);
void BossSpriteMidpoint(int& midRow, int& midCol);
std::pair<int, int> PlayerInteriorDoorwayRooms(int doorway);
Rect PlayerInteriorDoorwayRect(int doorway);
void OpenPlayerInteriorDoorway(int doorway);
void GenerateInteriorSpawners(std::uint64_t seed);

int RoomIndexAt(int row, int column);
bool RoomsConnected(int first, int second);
int RoomAtWorld(float x, float y);
int CurrentRoom();
void InvalidateWallCache();
const std::vector<WallRect>& BuildWalls();
bool HitsWall(const Rect& rectangle, int* room = nullptr);
bool InRoomNetwork(const Rect& rectangle);

int PhraseWidth(const std::vector<int>& phrase);
void BuildWorldTextBoxes();
void BuildShields();
bool HitsShield(const Rect& rectangle);
int OrganIndex(const std::string& id);
void UpdateSpawnerTypes();
void SelectCurrentLevel();
void UpdateValueWord(Organ& organ);
bool SpawnPercentageZero();

}  // namespace game

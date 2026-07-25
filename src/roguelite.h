#pragma once

#include <cstdint>
#include <vector>

#include "state.h"

namespace game {

// Derives an independent, repeatable seed without relying on engine state.
std::uint64_t DeriveRunSeed(
    std::uint64_t globalSeed, std::uint64_t domain,
    std::uint64_t index = 0);

// Replaces the current run with a deterministic, fully connected run graph.
void StartRun(std::uint64_t globalSeed);
void ResetRun();

RunNode* GetRunNode(RunNodeId id);
const RunNode* GetRunNode(const RunData& data, RunNodeId id);
RunNode* CurrentRunNode();
const std::vector<RunNodeId> RunNodesAtDepth(std::uint32_t depth);
const RunMapVertex* GetRunMapVertex(const RunData& data, RunNodeId id);
Rect RunMapVertexRect(const RunMapVertex& vertex);
bool SetCurrentRunNode(RunNodeId id);
bool IsRunGraphValid(const RunData& data);
EnemyDifficultyStages& DifficultyStages(const std::string& archetype);
const EnemyDifficultyStages& DifficultyStages(
    const RunData& data, const std::string& archetype);
std::uint32_t DifficultyStage(
    const RunData& data, const std::string& archetype,
    EnemyDifficultyStat stat);
const char* DifficultyStatName(EnemyDifficultyStat stat);

}  // namespace game

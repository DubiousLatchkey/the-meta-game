#include "roguelite.h"

#include <algorithm>
#include <queue>

namespace game {
namespace {

std::string InteriorArchetype(std::uint64_t seed) {
    static const char* ids[]{"circle", "triangle", "charger", "shooter"};
    return ids[DeriveRunSeed(seed, 0x494e544552494f52ULL) % 4];
}

std::string ArenaArchetype(std::uint64_t seed) {
    static const char* ids[]{"circle", "triangle", "charger", "shooter"};
    return ids[DeriveRunSeed(seed, 0x41524348ULL) % 4];
}

std::uint64_t Mix64(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

std::uint32_t Bounded(std::uint64_t seed, std::uint32_t bound) {
    return bound == 0 ? 0 : static_cast<std::uint32_t>(seed % bound);
}

RunNodeType ChooseNodeType(
    std::uint64_t seed, bool mayBeShop) {
    const std::uint32_t roll = Bounded(seed, 100);
    if (mayBeShop && roll < 12) return RunNodeType::Shop;
    if (roll >= 12 && roll < 24) return RunNodeType::PlayerInterior;
    if (roll >= 24 && roll < 40) return RunNodeType::Interior;
    return RunNodeType::EnemyArena;
}

void AddEdge(RunNode& from, RunNode& to) {
    if (std::find(from.next.begin(), from.next.end(), to.id) ==
        from.next.end())
        from.next.push_back(to.id);
}

}  // namespace

std::uint64_t DeriveRunSeed(
    std::uint64_t globalSeed, std::uint64_t domain,
    std::uint64_t index) {
    return Mix64(globalSeed ^ Mix64(domain) ^ Mix64(index));
}

void StartRun(std::uint64_t globalSeed) {
    ResetRun();
    run.globalSeed = globalSeed;
    run.status = RunStatus::Active;

    const std::uint32_t depthCount = rogueliteTuning.runDepths;
    std::vector<std::vector<RunNodeId>> layers(depthCount);
    for (std::uint32_t depth = 0; depth < depthCount; ++depth) {
        const bool endpoint = depth == 0 || depth + 1 == depthCount;
        const bool penultimate = depth + 2 == depthCount;
        const std::uint32_t count = endpoint ? 1 :
            penultimate
                ? 1 + Bounded(
                    DeriveRunSeed(globalSeed, 0x424f535151ULL, depth), 2)
                : rogueliteTuning.branchNodesMin + Bounded(
                    DeriveRunSeed(globalSeed, 0x4c41594552ULL, depth),
                    rogueliteTuning.branchNodesMax -
                        rogueliteTuning.branchNodesMin + 1);
        std::array<bool, 4> usedQuadrant{};
        for (std::uint32_t slot = 0; slot < count; ++slot) {
            RunNode node;
            node.id = static_cast<RunNodeId>(run.nodes.size());
            node.depth = depth;
            node.seed = DeriveRunSeed(globalSeed, depth, slot);
            if (depth + 1 == depthCount) {
                node.type = RunNodeType::Boss;
            } else if (depth == 0) {
                node.type = RunNodeType::EnemyArena;
            } else if (penultimate) {
                node.type = RunNodeType::BossInterior;
                const std::uint32_t start = Bounded(
                    DeriveRunSeed(node.seed, 0x51554144ULL), 4);
                for (std::uint32_t offset = 0; offset < 4; ++offset) {
                    const std::uint32_t index = (start + offset) % 4;
                    if (!usedQuadrant[index]) {
                        usedQuadrant[index] = true;
                        node.bossQuadrant =
                            static_cast<BossQuadrant>(index);
                        break;
                    }
                }
            } else {
                bool mayBeShop = true;
                for (RunNodeId parent : layers[depth - 1])
                    if (run.nodes[parent].type == RunNodeType::Shop)
                        mayBeShop = false;
                node.type = ChooseNodeType(
                    DeriveRunSeed(node.seed, 0x54595045ULL), mayBeShop);
            }
            if (node.type == RunNodeType::Interior)
                node.arenaArchetype = InteriorArchetype(node.seed);
            else if (node.type == RunNodeType::EnemyArena) {
                node.arenaArchetype = ArenaArchetype(node.seed);
                node.downside = static_cast<EnemyDifficultyStat>(
                    Bounded(
                        DeriveRunSeed(node.seed, 0x444f574e53494445ULL),
                        4));
                node.hardArena = depth != 0 &&
                    DeriveRunSeed(node.seed, 0x48415244ULL) % 4 == 0;
            }
            layers[depth].push_back(node.id);
            run.nodes.push_back(std::move(node));
        }
    }

    for (std::uint32_t depth = 0; depth + 1 < depthCount; ++depth) {
        auto& current = layers[depth];
        auto& next = layers[depth + 1];
        for (std::size_t index = 0; index < current.size(); ++index) {
            AddEdge(
                run.nodes[current[index]],
                run.nodes[next[index % next.size()]]);
        }
        // Add only the edges required to keep every node reachable. Every
        // current node already has an exit, so this produces sparse branches
        // and merges without dead ends.
        for (std::size_t index = 0; index < next.size(); ++index) {
            bool linked = false;
            for (RunNodeId parent : current)
                linked = linked ||
                    std::find(
                        run.nodes[parent].next.begin(),
                        run.nodes[parent].next.end(), next[index]) !=
                    run.nodes[parent].next.end();
            if (!linked)
                AddEdge(
                    run.nodes[current[index % current.size()]],
                    run.nodes[next[index]]);
        }
        // Give every path up to three distinct forward choices.  This is
        // deterministic and preserves the horizontal DAG while making route
        // selection materially more branching than reachability alone.
        if (next.size() > 1)
            for (std::size_t index = 0; index < current.size(); ++index) {
                RunNode& node = run.nodes[current[index]];
                const std::size_t desired = std::min<std::size_t>(
                    next.size(), 1 + rogueliteTuning.extraBranchEdges);
                const std::size_t start = Bounded(
                    DeriveRunSeed(
                        globalSeed, 0x4558545241454447ULL,
                        depth * 16 + index),
                    static_cast<std::uint32_t>(next.size()));
                for (std::size_t offset = 0;
                     offset < next.size() && node.next.size() < desired;
                     ++offset)
                    AddEdge(
                        node, run.nodes[
                            next[(start + offset) % next.size()]]);
            }
    }

    run.startNode = layers.front().front();
    run.currentNode = run.startNode;
    run.nodes[run.startNode].entered = true;
    constexpr float marginX = 240.0f;
    constexpr float marginY = 220.0f;
    const float depthSpacing = depthCount > 1
        ? (kRunMapWidth - marginX * 2.0f) / (depthCount - 1) : 0.0f;
    for (std::uint32_t depth = 0; depth < depthCount; ++depth) {
        const auto& layer = layers[depth];
        const float verticalSpacing =
            (kRunMapHeight - marginY * 2.0f) / (layer.size() + 1);
        for (std::size_t slot = 0; slot < layer.size(); ++slot)
            run.mapVertices.push_back({
                layer[slot], marginX + depth * depthSpacing,
                marginY + (slot + 1) * verticalSpacing});
    }
}

void ResetRun() { run = RunData{}; }

EnemyDifficultyStages& DifficultyStages(const std::string& archetype) {
    return run.enemyDifficulty[archetype];
}

const EnemyDifficultyStages& DifficultyStages(
    const RunData& data, const std::string& archetype) {
    static const EnemyDifficultyStages base{};
    const auto found = data.enemyDifficulty.find(archetype);
    return found == data.enemyDifficulty.end() ? base : found->second;
}

std::uint32_t DifficultyStage(
    const RunData& data, const std::string& archetype,
    EnemyDifficultyStat stat) {
    const EnemyDifficultyStages& stages =
        DifficultyStages(data, archetype);
    switch (stat) {
        case EnemyDifficultyStat::Size: return stages.size;
        case EnemyDifficultyStat::Speed: return stages.speed;
        case EnemyDifficultyStat::Health: return stages.health;
        case EnemyDifficultyStat::Burst: return stages.burst;
    }
    return 0;
}

const char* DifficultyStatName(EnemyDifficultyStat stat) {
    switch (stat) {
        case EnemyDifficultyStat::Size: return "SIZE";
        case EnemyDifficultyStat::Speed: return "SPEED";
        case EnemyDifficultyStat::Health: return "HEALTH";
        case EnemyDifficultyStat::Burst: return "SPAWN HERD MODIFIER";
    }
    return "STAT";
}

RunNode* GetRunNode(RunNodeId id) {
    return id < run.nodes.size() ? &run.nodes[id] : nullptr;
}

const RunNode* GetRunNode(const RunData& data, RunNodeId id) {
    return id < data.nodes.size() ? &data.nodes[id] : nullptr;
}

RunNode* CurrentRunNode() { return GetRunNode(run.currentNode); }

const std::vector<RunNodeId> RunNodesAtDepth(std::uint32_t depth) {
    std::vector<RunNodeId> result;
    for (const RunNode& node : run.nodes)
        if (node.depth == depth) result.push_back(node.id);
    return result;
}

const RunMapVertex* GetRunMapVertex(const RunData& data, RunNodeId id) {
    const auto found = std::find_if(
        data.mapVertices.begin(), data.mapVertices.end(),
        [id](const RunMapVertex& vertex) { return vertex.node == id; });
    return found == data.mapVertices.end() ? nullptr : &*found;
}

Rect RunMapVertexRect(const RunMapVertex& vertex) {
    return {vertex.x - 70.0f, vertex.y - 42.0f, 140.0f, 84.0f};
}

bool SetCurrentRunNode(RunNodeId id) {
    RunNode* node = GetRunNode(id);
    if (!node || run.status != RunStatus::Active) return false;
    if (run.currentNode != kInvalidRunNode &&
        run.currentNode != id) {
        const RunNode* current = GetRunNode(run.currentNode);
        if (!current ||
            std::find(current->next.begin(), current->next.end(), id) ==
                current->next.end())
            return false;
    }
    run.currentNode = id;
    node->entered = true;
    return true;
}

bool IsRunGraphValid(const RunData& data) {
    if (data.nodes.empty() || data.startNode >= data.nodes.size())
        return false;
    const std::uint32_t terminalDepth = std::max_element(
        data.nodes.begin(), data.nodes.end(),
        [](const RunNode& first, const RunNode& second) {
            return first.depth < second.depth;
        })->depth;
    if (terminalDepth < 2 || data.nodes[data.startNode].depth != 0)
        return false;
    std::vector<bool> reached(data.nodes.size(), false);
    std::queue<RunNodeId> pending;
    reached[data.startNode] = true;
    pending.push(data.startNode);
    while (!pending.empty()) {
        const RunNode& node = data.nodes[pending.front()];
        pending.pop();
        for (RunNodeId next : node.next) {
            if (next >= data.nodes.size() ||
                data.nodes[next].depth <= node.depth)
                return false;
            if (!reached[next]) {
                reached[next] = true;
                pending.push(next);
            }
            if (node.type == RunNodeType::Shop &&
                data.nodes[next].type == RunNodeType::Shop)
                return false;
        }
    }
    if (std::find(reached.begin(), reached.end(), false) != reached.end())
        return false;
    const auto bossCount = std::count_if(
        data.nodes.begin(), data.nodes.end(),
        [](const RunNode& node) {
            return node.type == RunNodeType::Boss;
        });
    if (bossCount != 1) return false;
    const RunNodeId boss = static_cast<RunNodeId>(std::distance(
        data.nodes.begin(), std::find_if(
            data.nodes.begin(), data.nodes.end(),
            [](const RunNode& node) {
                return node.type == RunNodeType::Boss;
            })));
    if (data.nodes[boss].depth != terminalDepth)
        return false;
    std::array<bool, 4> bossInteriorQuadrants{};
    std::size_t bossInteriorCount = 0;
    for (const RunNode& node : data.nodes) {
        if (node.depth != terminalDepth - 1) continue;
        if (node.type != RunNodeType::BossInterior) return false;
        const std::size_t quadrant =
            static_cast<std::size_t>(node.bossQuadrant);
        if (quadrant >= bossInteriorQuadrants.size() ||
            bossInteriorQuadrants[quadrant])
            return false;
        bossInteriorQuadrants[quadrant] = true;
        ++bossInteriorCount;
    }
    if (bossInteriorCount < 1 || bossInteriorCount > 2)
        return false;
    std::vector<bool> reachesBoss(data.nodes.size(), false);
    reachesBoss[boss] = true;
    for (std::size_t index = data.nodes.size(); index-- > 0;) {
        const RunNode& node = data.nodes[index];
        for (RunNodeId next : node.next)
            reachesBoss[index] = reachesBoss[index] || reachesBoss[next];
    }
    return std::find(
        reachesBoss.begin(), reachesBoss.end(), false) == reachesBoss.end();
}

}  // namespace game

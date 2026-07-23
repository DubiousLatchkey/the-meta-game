#include "world.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <queue>
#include <string>
#include <system_error>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include "text_renderer.h"

namespace game {
namespace {

std::string LuaError(lua_State* lua) {
    const char* text = lua_tostring(lua, -1);
    return text ? text : "unknown Lua error";
}

int IntegerField(lua_State* lua, int table, const char* name, int fallback) {
    table = lua_absindex(lua, table);
    lua_getfield(lua, table, name);
    const int result = lua_isinteger(lua, -1)
        ? static_cast<int>(lua_tointeger(lua, -1)) : fallback;
    lua_pop(lua, 1);
    return result;
}

float NumberField(lua_State* lua, int table, const char* name, float fallback) {
    table = lua_absindex(lua, table);
    lua_getfield(lua, table, name);
    const float result = lua_isnumber(lua, -1)
        ? static_cast<float>(lua_tonumber(lua, -1)) : fallback;
    lua_pop(lua, 1);
    return result;
}

std::string StringField(lua_State* lua, int table, const char* name) {
    table = lua_absindex(lua, table);
    lua_getfield(lua, table, name);
    const char* value = lua_tostring(lua, -1);
    std::string result = value ? value : "";
    lua_pop(lua, 1);
    return result;
}

std::vector<int> ReadWordList(
    lua_State* lua, int table, const std::map<const void*, int>& pointers) {
    std::vector<int> result;
    table = lua_absindex(lua, table);
    const int count = static_cast<int>(lua_rawlen(lua, table));
    for (int i = 1; i <= count; ++i) {
        lua_rawgeti(lua, table, i);
        const auto found = pointers.find(lua_topointer(lua, -1));
        if (found != pointers.end()) result.push_back(found->second);
        lua_pop(lua, 1);
    }
    return result;
}

bool LoadWorldScript(const std::filesystem::path& path) {
    lua_State* lua = luaL_newstate();
    if (!lua) return false;
    luaL_requiref(lua, "_G", luaopen_base, 1);
    lua_pop(lua, 1);
    lua_pushnil(lua); lua_setglobal(lua, "dofile");
    lua_pushnil(lua); lua_setglobal(lua, "loadfile");
    const std::string filename = path.string();
    if (luaL_loadfile(lua, filename.c_str()) != LUA_OK ||
        lua_pcall(lua, 0, 1, 0) != LUA_OK || !lua_istable(lua, -1)) {
        LuaError(lua);
        lua_close(lua);
        return false;
    }

    const int root = lua_absindex(lua, -1);
    std::vector<Word> loadedWords;
    std::map<std::string, int> loadedWordIds;
    std::map<const void*, int> wordPointers;
    lua_getfield(lua, root, "words");
    if (!lua_istable(lua, -1)) { lua_close(lua); return false; }
    lua_pushnil(lua);
    while (lua_next(lua, -2) != 0) {
        if (lua_type(lua, -2) == LUA_TSTRING && lua_istable(lua, -1)) {
            Word word;
            word.id = lua_tostring(lua, -2);
            const std::string value = StringField(lua, -1, "text");
            word.bytes.assign(value.begin(), value.end());
            const int index = static_cast<int>(loadedWords.size());
            loadedWordIds[word.id] = index;
            wordPointers[lua_topointer(lua, -1)] = index;
            loadedWords.push_back(std::move(word));
        }
        lua_pop(lua, 1);
    }
    lua_pop(lua, 1);
    if (loadedWords.empty()) { lua_close(lua); return false; }

    std::map<std::string, std::vector<int>> loadedPhrases;
    lua_getfield(lua, root, "text");
    if (lua_istable(lua, -1)) {
        lua_pushnil(lua);
        while (lua_next(lua, -2) != 0) {
            if (lua_type(lua, -2) == LUA_TSTRING && lua_istable(lua, -1))
                loadedPhrases[lua_tostring(lua, -2)] =
                    ReadWordList(lua, -1, wordPointers);
            lua_pop(lua, 1);
        }
    }
    lua_pop(lua, 1);

    std::map<std::string, EnemyType> loadedTypes;
    lua_getfield(lua, root, "enemies");
    if (!lua_istable(lua, -1)) { lua_close(lua); return false; }
    lua_pushnil(lua);
    while (lua_next(lua, -2) != 0) {
        if (lua_type(lua, -2) == LUA_TSTRING && lua_istable(lua, -1)) {
            EnemyType type;
            type.id = lua_tostring(lua, -2);
            type.maxHealth =
                std::max(1, IntegerField(lua, -1, "health", 1));
            type.speed =
                std::max(0.0f, NumberField(lua, -1, "speed", 1.0f));
            type.contactDamage =
                std::max(0, IntegerField(lua, -1, "contact_damage", 1));
            type.pixelScale =
                std::clamp(IntegerField(lua, -1, "pixel_scale", 7), 2, 20);
            lua_getfield(lua, -1, "sprite");
            if (lua_istable(lua, -1)) {
                const int rowCount = static_cast<int>(lua_rawlen(lua, -1));
                for (int row = 1; row <= rowCount; ++row) {
                    lua_rawgeti(lua, -1, row);
                    std::vector<Pixel> rowPixels;
                    if (lua_istable(lua, -1)) {
                        const int columnCount =
                            static_cast<int>(lua_rawlen(lua, -1));
                        for (int column = 1; column <= columnCount; ++column) {
                            lua_rawgeti(lua, -1, column);
                            Pixel pixel;
                            pixel.occupied = lua_istable(lua, -1);
                            if (pixel.occupied) {
                                for (int channel = 0; channel < 3; ++channel) {
                                    lua_rawgeti(lua, -1, channel + 1);
                                    if (lua_isinteger(lua, -1))
                                        pixel.rgb[channel] = std::clamp(
                                            static_cast<int>(
                                                lua_tointeger(lua, -1)),
                                            0, 255);
                                    lua_pop(lua, 1);
                                }
                            }
                            rowPixels.push_back(pixel);
                            lua_pop(lua, 1);
                        }
                    }
                    type.sprite.push_back(std::move(rowPixels));
                    lua_pop(lua, 1);
                }
            }
            lua_pop(lua, 1);
            if (!type.sprite.empty())
                loadedTypes[type.id] = std::move(type);
        }
        lua_pop(lua, 1);
    }
    lua_pop(lua, 1);

    Interior loadedInterior;
    std::vector<Organ> loadedOrgans;
    lua_getfield(lua, root, "interior");
    if (!lua_istable(lua, -1)) { lua_close(lua); return false; }
    loadedInterior.archetype = StringField(lua, -1, "archetype");
    loadedInterior.enemy = loadedInterior.archetype;
    loadedInterior.alternateEnemy =
        StringField(lua, -1, "alternate_enemy");
    loadedInterior.roomSize =
        std::clamp(IntegerField(lua, -1, "room_size", 640), 400, 1000);
    loadedInterior.seed = IntegerField(lua, -1, "seed", 7331);
    loadedInterior.spawnersMin =
        std::clamp(IntegerField(lua, -1, "spawners_min", 1), 1, 4);
    loadedInterior.spawnersMax = std::clamp(
        IntegerField(lua, -1, "spawners_max", 4),
        loadedInterior.spawnersMin, 4);
    loadedInterior.burstMin =
        std::clamp(IntegerField(lua, -1, "burst_min", 4), 1, 10);
    loadedInterior.burstMax = std::clamp(
        IntegerField(lua, -1, "burst_max", 6),
        loadedInterior.burstMin, 12);
    loadedInterior.alternateBurstMin = std::clamp(
        IntegerField(lua, -1, "alternate_burst_min", 4), 1, 10);
    loadedInterior.alternateBurstMax = std::clamp(
        IntegerField(lua, -1, "alternate_burst_max", 6),
        loadedInterior.alternateBurstMin, 12);
    loadedInterior.secondsMin =
        std::max(0.5f, NumberField(lua, -1, "spawn_seconds_min", 3));
    loadedInterior.secondsMax = std::max(
        loadedInterior.secondsMin,
        NumberField(lua, -1, "spawn_seconds_max", 6));
    loadedInterior.speedUnit =
        std::max(0.0f, NumberField(lua, -1, "speed_unit", 4.0f));
    lua_getfield(lua, -1, "organs");
    const std::array<const char*, 5> organNames{
        {"size", "speed", "health", "shield", "core"}};
    if (lua_istable(lua, -1)) {
        for (const char* name : organNames) {
            lua_getfield(lua, -1, name);
            if (lua_istable(lua, -1)) {
                Organ organ;
                organ.id = name;
                organ.value =
                    std::max(0, IntegerField(lua, -1, "value", 1));
                organ.maximum = std::max(
                    1, IntegerField(lua, -1, "maximum", organ.value));
                lua_getfield(lua, -1, "label");
                if (lua_istable(lua, -1))
                    organ.label = ReadWordList(lua, -1, wordPointers);
                lua_pop(lua, 1);
                lua_getfield(lua, -1, "value_text");
                if (lua_istable(lua, -1)) {
                    const std::vector<int> valueText =
                        ReadWordList(lua, -1, wordPointers);
                    if (!valueText.empty()) organ.valueWord = valueText.front();
                }
                lua_pop(lua, 1);
                loadedOrgans.push_back(std::move(organ));
            }
            lua_pop(lua, 1);
        }
    }
    lua_pop(lua, 2);
    lua_close(lua);

    if (!loadedTypes.count(loadedInterior.archetype) ||
        !loadedTypes.count(loadedInterior.alternateEnemy) ||
        loadedOrgans.size() != 5) return false;
    words = std::move(loadedWords);
    wordIds = std::move(loadedWordIds);
    phrases = std::move(loadedPhrases);
    types = std::move(loadedTypes);
    interior = std::move(loadedInterior);
    organs = std::move(loadedOrgans);
    return true;
}

bool CopyGoldenWorld(bool overwrite) {
    std::error_code error;
    std::filesystem::create_directories(gameDirectory, error);
    const auto source = goldenDirectory / "world.lua";
    const auto destination = gameDirectory / "world.lua";
    if (!std::filesystem::exists(source)) return false;
    if (overwrite || !std::filesystem::exists(destination))
        std::filesystem::copy_file(
            source, destination,
            std::filesystem::copy_options::overwrite_existing, error);
    return !error;
}

std::string HexWord(const Word& word) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    for (std::uint8_t byte : word.bytes) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 15]);
    }
    return result;
}

void SetWordHex(Word& word, const std::string& hex) {
    auto nibble = [](char value) {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        return 0;
    };
    word.bytes.clear();
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
        word.bytes.push_back(static_cast<std::uint8_t>(
            nibble(hex[i]) * 16 + nibble(hex[i + 1])));
}

void ApplyMutations() {
    std::ifstream input(gameDirectory / "mutations.lua");
    std::string line;
    while (std::getline(input, line)) {
        char id[128]{}, hex[1024]{};
        int row, column, red, green, blue, value;
        if (sscanf_s(
                line.c_str(),
                "set_pixel(\"%127[^\"]\", %d, %d, %d, %d, %d)",
                id, static_cast<unsigned>(_countof(id)), &row, &column,
                &red, &green, &blue) == 6) {
            auto type = types.find(id);
            if (type != types.end() && row > 0 && column > 0 &&
                row <= static_cast<int>(type->second.sprite.size()) &&
                column <=
                    static_cast<int>(type->second.sprite[row - 1].size())) {
                Pixel& pixel = type->second.sprite[row - 1][column - 1];
                pixel.rgb = {
                    std::clamp(red, 0, 255), std::clamp(green, 0, 255),
                    std::clamp(blue, 0, 255)};
            }
        } else if (sscanf_s(
                       line.c_str(),
                       "set_word(\"%127[^\"]\", \"%1023[^\"]\")",
                       id, static_cast<unsigned>(_countof(id)),
                       hex, static_cast<unsigned>(_countof(hex))) == 2) {
            const auto found = wordIds.find(id);
            if (found != wordIds.end()) SetWordHex(words[found->second], hex);
        } else if (sscanf_s(
                       line.c_str(), "set_organ(\"%127[^\"]\", %d)",
                       id, static_cast<unsigned>(_countof(id)), &value) == 2) {
            for (Organ& organ : organs)
                if (organ.id == id)
                    organ.value = std::clamp(value, 0, organ.maximum);
        }
    }
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

void AddPhraseBoxes(
    const std::vector<int>& phrase, float x, float y, int organ = -1) {
    for (int word : phrase) {
        const int width =
            text_renderer::MeasureWidth(words[word].bytes.size());
        textBoxes.push_back(
            {{x, y, static_cast<float>(width),
              static_cast<float>(text_renderer::kGlyphHeight)},
             word, organ, false});
        x += width + text_renderer::kGlyphAdvance;
    }
}

void GenerateRooms() {
    rooms.clear();
    roomAt.clear();
    EnemyType& giant = types.at(interior.archetype);
    for (int row = 0; row < static_cast<int>(giant.sprite.size()); ++row)
        for (int column = 0;
             column < static_cast<int>(giant.sprite[row].size()); ++column)
            if (giant.sprite[row][column].occupied) {
                const int index = static_cast<int>(rooms.size());
                rooms.push_back({row, column, row, column, 0});
                roomAt[{row, column}] = index;
            }

    int start = 0;
    for (int i = 1; i < static_cast<int>(rooms.size()); ++i)
        if (rooms[i].column < rooms[start].column ||
            (rooms[i].column == rooms[start].column &&
             std::abs(rooms[i].row - 3) < std::abs(rooms[start].row - 3)))
            start = i;

    std::vector<int> distances(rooms.size(), -1);
    std::queue<int> pending;
    distances[start] = 0;
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
            if (next >= 0 && distances[next] < 0) {
                distances[next] = distances[current] + 1;
                pending.push(next);
            }
        }
    }

    std::vector<int> order(rooms.size());
    for (int i = 0; i < static_cast<int>(order.size()); ++i) {
        order[i] = i;
        rooms[i].distance = distances[i];
    }
    std::sort(order.begin(), order.end(),
        [&](int a, int b) { return distances[a] < distances[b]; });
    for (int i = 0; i < static_cast<int>(organs.size()); ++i) {
        const float fraction = organs[i].id == "core"
            ? 1.0f
            : static_cast<float>(i + 1) / organs.size();
        int position =
            static_cast<int>((order.size() - 1) * fraction);
        while (position > 0) {
            bool used = false;
            for (int prior = 0; prior < i; ++prior)
                if (organs[prior].room == order[position]) used = true;
            if (!used) break;
            --position;
        }
        organs[i].room = order[position];
        UpdateValueWord(organs[i]);
    }
    const Room& startRoom = rooms[start];
    playerX = RoomX(startRoom) + interior.roomSize * 0.5f -
              kPlayerSize * 0.5f;
    playerY = RoomY(startRoom) + interior.roomSize * 0.5f -
              kPlayerSize * 0.5f;
}

void GenerateSpawners() {
    spawners.clear();
    random.seed(static_cast<unsigned>(interior.seed));
    int spawnerIndex = 0;
    for (int roomIndex = 0;
         roomIndex < static_cast<int>(rooms.size()); ++roomIndex) {
        const Room& room = rooms[roomIndex];
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
            const std::string& enemyType = (spawnerIndex++ % 2 == 0)
                ? interior.enemy : interior.alternateEnemy;
            spawners.push_back(
                {roomIndex, enemyType, x, y, 5,
                 RandomFloat(interior.secondsMin, interior.secondsMax)});
        }
    }
}

void ResetPlay() {
    enemies.clear();
    projectiles.clear();
    bombs.clear();
    explosions.clear();
    playerHealth = kPlayerMaxHealth;
    bombCooldown = 0;
    healthRegenTimer = kHealthRegenSeconds;
    lastPlayerRoom = -1;
    GenerateRooms();
    BuildWorldTextBoxes();
    BuildShields();
    GenerateSpawners();
}

}  // namespace

void SaveMutations() {
    std::ofstream output(gameDirectory / "mutations.lua", std::ios::trunc);
    output << "-- Generated persistent giant-enemy state. Press R to reset.\n";
    output << "local function set_pixel(enemy,row,column,r,g,b) end\n";
    output << "local function set_word(id,hex) end\n";
    output << "local function set_organ(id,value) end\n";
    for (const auto& [id, type] : types)
        for (std::size_t row = 0; row < type.sprite.size(); ++row)
            for (std::size_t column = 0;
                 column < type.sprite[row].size(); ++column) {
                const Pixel& pixel = type.sprite[row][column];
                if (!pixel.occupied) continue;
                output << "set_pixel(\"" << id << "\", " << row + 1
                       << ", " << column + 1 << ", " << pixel.rgb[0]
                       << ", " << pixel.rgb[1] << ", " << pixel.rgb[2]
                       << ")\n";
            }
    for (const Word& word : words)
        output << "set_word(\"" << word.id << "\", \""
               << HexWord(word) << "\")\n";
    for (const Organ& organ : organs)
        output << "set_organ(\"" << organ.id << "\", " << organ.value
               << ")\n";
}

int RoomIndexAt(int row, int column) {
    const auto found = roomAt.find({row, column});
    return found == roomAt.end() ? -1 : found->second;
}

int CurrentRoom() {
    const int column = static_cast<int>(std::floor(
        (playerX + kPlayerSize * 0.5f) / interior.roomSize));
    const int row = static_cast<int>(std::floor(
        (playerY + kPlayerSize * 0.5f) / interior.roomSize));
    return RoomIndexAt(row, column);
}

std::vector<WallRect> BuildWalls() {
    std::vector<WallRect> walls;
    for (int index = 0; index < static_cast<int>(rooms.size()); ++index) {
        const Room& room = rooms[index];
        const float x = RoomX(room), y = RoomY(room);
        AddSplitHorizontal(
            walls, index, x, y,
            RoomIndexAt(room.row - 1, room.column) >= 0);
        AddSplitHorizontal(
            walls, index, x, y + interior.roomSize - kWall,
            RoomIndexAt(room.row + 1, room.column) >= 0);
        AddSplitVertical(
            walls, index, x, y,
            RoomIndexAt(room.row, room.column - 1) >= 0);
        AddSplitVertical(
            walls, index, x + interior.roomSize - kWall, y,
            RoomIndexAt(room.row, room.column + 1) >= 0);
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
    const int column = static_cast<int>(
        std::floor(CenterX(rectangle) / interior.roomSize));
    const int row = static_cast<int>(
        std::floor(CenterY(rectangle) / interior.roomSize));
    return RoomIndexAt(row, column) >= 0 && !HitsWall(rectangle);
}

int PhraseWidth(const std::vector<int>& phrase) {
    int width = 0;
    for (std::size_t i = 0; i < phrase.size(); ++i) {
        if (i) width += text_renderer::kGlyphAdvance;
        width += text_renderer::MeasureWidth(words[phrase[i]].bytes.size());
    }
    return width;
}

void BuildWorldTextBoxes() {
    textBoxes.clear();
    const auto roomPhrase = phrases.find("room");
    if (roomPhrase != phrases.end())
        for (const Room& room : rooms)
            AddPhraseBoxes(
                roomPhrase->second, RoomX(room) + 42, RoomY(room) + 42);
    for (int index = 0; index < static_cast<int>(organs.size()); ++index) {
        const Organ& organ = organs[index];
        if (organ.room < 0) continue;
        const Room& room = rooms[organ.room];
        const float center =
            RoomX(room) + interior.roomSize * 0.5f;
        const int labelWidth = PhraseWidth(organ.label);
        AddPhraseBoxes(
            organ.label, center - labelWidth * 0.5f,
            RoomY(room) + interior.roomSize * 0.45f, index);
        if (organ.valueWord >= 0) {
            const int width = text_renderer::MeasureWidth(
                words[organ.valueWord].bytes.size());
            textBoxes.push_back(
                {{center - width * 0.5f,
                  RoomY(room) + interior.roomSize * 0.53f,
                  static_cast<float>(width),
                  static_cast<float>(text_renderer::kGlyphHeight)},
                 organ.valueWord, index, true});
        }
    }
}

int OrganIndex(const std::string& id) {
    for (int index = 0;
         index < static_cast<int>(organs.size()); ++index)
        if (organs[index].id == id) return index;
    return -1;
}

void BuildShields() {
    shieldBlocks.clear();
    const int shieldOrgan = OrganIndex("shield");
    const int health = shieldOrgan >= 0
        ? std::max(0, organs[shieldOrgan].value) : 0;
    constexpr float padding = 24.0f;
    constexpr float thickness = 14.0f;
    constexpr float blockLength = 32.0f;
    for (int organIndex = 0;
         organIndex < static_cast<int>(organs.size()); ++organIndex) {
        float left = 1.0e30f, top = 1.0e30f;
        float right = -1.0e30f, bottom = -1.0e30f;
        for (const TextBox& box : textBoxes) {
            if (box.organ != organIndex) continue;
            left = std::min(left, box.rect.x);
            top = std::min(top, box.rect.y);
            right = std::max(right, box.rect.x + box.rect.width);
            bottom = std::max(bottom, box.rect.y + box.rect.height);
        }
        if (right <= left || bottom <= top) continue;
        left -= padding;
        right += padding;
        top -= padding;
        bottom += padding;
        for (float x = left; x < right; x += blockLength) {
            const float width = std::min(blockLength, right - x);
            shieldBlocks.push_back(
                {{x, top, width, thickness}, organIndex, health});
            shieldBlocks.push_back(
                {{x, bottom - thickness, width, thickness},
                 organIndex, health});
        }
        for (float y = top + thickness;
             y < bottom - thickness; y += blockLength) {
            const float height =
                std::min(blockLength, bottom - thickness - y);
            shieldBlocks.push_back(
                {{left, y, thickness, height}, organIndex, health});
            shieldBlocks.push_back(
                {{right - thickness, y, thickness, height},
                 organIndex, health});
        }
    }
}

bool HitsShield(const Rect& rectangle) {
    for (const ShieldBlock& shield : shieldBlocks)
        if (shield.health > 0 && Overlaps(rectangle, shield.rect))
            return true;
    return false;
}

void UpdateValueWord(Organ& organ) {
    if (organ.valueWord < 0) return;
    const std::string value = std::to_string(organ.value);
    words[organ.valueWord].bytes.assign(value.begin(), value.end());
}

bool CoreDisabled() {
    const int core = OrganIndex("core");
    return core >= 0 && organs[core].value == 0;
}

bool ReloadWorld(bool reset) {
    if (reset) {
        // Validate the golden world before replacing a working runtime copy.
        // A partially edited golden script must not make the next launch fail.
        if (!LoadWorldScript(goldenDirectory / "world.lua"))
            return false;
        if (!CopyGoldenWorld(true)) return false;
        std::error_code ignored;
        std::filesystem::remove(
            gameDirectory / "mutations.lua", ignored);
        ResetPlay();
        return true;
    }
    if (!CopyGoldenWorld(false)) return false;
    if (!LoadWorldScript(gameDirectory / "world.lua")) {
        if (!LoadWorldScript(goldenDirectory / "world.lua"))
            return false;
        if (!CopyGoldenWorld(true)) return false;
    }
    ApplyMutations();
    ResetPlay();
    return true;
}

}  // namespace game

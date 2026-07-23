#pragma once

#include "state.h"

namespace game {

bool ReloadWorld(bool reset);
void SaveMutations();

int RoomIndexAt(int row, int column);
int CurrentRoom();
std::vector<WallRect> BuildWalls();
bool HitsWall(const Rect& rectangle, int* room = nullptr);
bool InRoomNetwork(const Rect& rectangle);

int PhraseWidth(const std::vector<int>& phrase);
void BuildWorldTextBoxes();
void BuildShields();
bool HitsShield(const Rect& rectangle);
int OrganIndex(const std::string& id);
void UpdateValueWord(Organ& organ);
bool CoreDisabled();

}  // namespace game

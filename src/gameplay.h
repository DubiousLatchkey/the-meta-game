#pragma once

#include "state.h"

namespace game {

void ShootToward(int mouseX, int mouseY);
void LaunchBomb(int mouseX, int mouseY);
Rect EnemyRect(const Enemy& enemy);
int EnemyScale(const Enemy& enemy);
bool EnemyVisualOverlaps(const Enemy& enemy, const Rect& target);
bool EnemyVisualFitsNetwork(const Enemy& enemy, float x, float y);
bool EnemyVisualWithinRadius(const Enemy& enemy, float x, float y, float radius);
void Update(float dt);

}  // namespace game

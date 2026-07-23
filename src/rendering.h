#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace game {

bool InitializeBackBuffer(HWND window);
void ResizeBackBuffer(int width, int height);
void DestroyBackBuffer();
void Render(HWND window);

}  // namespace game

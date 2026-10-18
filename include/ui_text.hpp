#pragma once
#include <string>
struct SDL_Renderer;
struct SDL_Color;

namespace switchiptv {
void draw_text(SDL_Renderer* renderer, int x, int y, const std::string& text, int scale, SDL_Color color);
}

#include "ui_text.hpp"
#include <SDL2/SDL.h>
#include <array>
#include <cctype>
#ifdef __SWITCH__
#include <switch.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <unordered_map>
#endif

namespace switchiptv {
namespace {
using Glyph = std::array<unsigned char, 7>;
Glyph glyph(char c) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    switch (c) {
    case 'A': return {14,17,17,31,17,17,17}; case 'B': return {30,17,17,30,17,17,30};
    case 'C': return {14,17,16,16,16,17,14}; case 'D': return {30,17,17,17,17,17,30};
    case 'E': return {31,16,16,30,16,16,31}; case 'F': return {31,16,16,30,16,16,16};
    case 'G': return {14,17,16,23,17,17,14}; case 'H': return {17,17,17,31,17,17,17};
    case 'I': return {14,4,4,4,4,4,14}; case 'J': return {7,2,2,2,18,18,12};
    case 'K': return {17,18,20,24,20,18,17}; case 'L': return {16,16,16,16,16,16,31};
    case 'M': return {17,27,21,21,17,17,17}; case 'N': return {17,25,21,19,17,17,17};
    case 'O': return {14,17,17,17,17,17,14}; case 'P': return {30,17,17,30,16,16,16};
    case 'Q': return {14,17,17,17,21,18,13}; case 'R': return {30,17,17,30,20,18,17};
    case 'S': return {15,16,16,14,1,1,30}; case 'T': return {31,4,4,4,4,4,4};
    case 'U': return {17,17,17,17,17,17,14}; case 'V': return {17,17,17,17,17,10,4};
    case 'W': return {17,17,17,21,21,21,10}; case 'X': return {17,17,10,4,10,17,17};
    case 'Y': return {17,17,10,4,4,4,4}; case 'Z': return {31,1,2,4,8,16,31};
    case '0': return {14,17,19,21,25,17,14}; case '1': return {4,12,4,4,4,4,14};
    case '2': return {14,17,1,2,4,8,31}; case '3': return {30,1,1,14,1,1,30};
    case '4': return {2,6,10,18,31,2,2}; case '5': return {31,16,16,30,1,1,30};
    case '6': return {14,16,16,30,17,17,14}; case '7': return {31,1,2,4,8,8,8};
    case '8': return {14,17,17,14,17,17,14}; case '9': return {14,17,17,15,1,1,14};
    case ':': return {0,4,4,0,4,4,0}; case '.': return {0,0,0,0,0,6,6};
    case '-': return {0,0,0,31,0,0,0}; case '_': return {0,0,0,0,0,0,31};
    case '/': return {1,2,2,4,8,8,16}; case '[': return {14,8,8,8,8,8,14};
    case ']': return {14,2,2,2,2,2,14}; case ' ': return {0,0,0,0,0,0,0};
    default: return {14,17,1,2,4,0,4};
    }
}

#ifdef __SWITCH__
struct CachedGlyph { SDL_Texture* texture{}; int w{}, h{}, left{}, top{}, advance{}; };
FT_Library ft_library{};
FT_Face ft_face{};
bool ft_attempted = false;
std::unordered_map<unsigned long long, CachedGlyph> glyph_cache;

bool ensure_font() {
    if (ft_attempted) return ft_face != nullptr;
    ft_attempted = true;
    PlFontData data{};
    if (R_FAILED(plInitialize(PlServiceType_User))) return false;
    if (R_FAILED(plGetSharedFontByType(&data, PlSharedFontType_Standard)) || FT_Init_FreeType(&ft_library) ||
        FT_New_Memory_Face(ft_library, static_cast<const FT_Byte*>(data.address), data.size, 0, &ft_face)) return false;
    return true;
}

bool next_utf8(const std::string& text, std::size_t& pos, char32_t& cp) {
    if (pos >= text.size()) return false;
    const unsigned char c = text[pos++];
    if (c < 0x80) { cp = c; return true; }
    int extra = (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 ? 3 : -1;
    if (extra < 0 || pos + extra > text.size()) { cp = 0xfffd; return true; }
    cp = c & ((1 << (6 - extra)) - 1);
    while (extra--) { const unsigned char n = text[pos++]; if ((n & 0xc0) != 0x80) { cp = 0xfffd; break; } cp = (cp << 6) | (n & 0x3f); }
    return true;
}

void draw_unicode(SDL_Renderer* renderer, int x, int y, const std::string& text, int scale, SDL_Color color) {
    const int pixel_size = std::max(12, scale * 8);
    FT_Set_Pixel_Sizes(ft_face, 0, pixel_size);
    const int origin = x;
    const int baseline = y + pixel_size;
    std::size_t pos = 0; char32_t cp = 0;
    while (next_utf8(text, pos, cp)) {
        if (cp == '\n') { x = origin; y += pixel_size + 3; continue; }
        const unsigned long long key = (static_cast<unsigned long long>(pixel_size) << 32) | cp;
        auto it = glyph_cache.find(key);
        if (it == glyph_cache.end()) {
            CachedGlyph cached{};
            if (!FT_Load_Char(ft_face, cp, FT_LOAD_RENDER)) {
                const auto& bm = ft_face->glyph->bitmap;
                cached.w = bm.width; cached.h = bm.rows;
                cached.left = ft_face->glyph->bitmap_left; cached.top = ft_face->glyph->bitmap_top;
                cached.advance = ft_face->glyph->advance.x >> 6;
                if (cached.w && cached.h) {
                    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, cached.w, cached.h, 32, SDL_PIXELFORMAT_RGBA32);
                    auto* pixels = static_cast<Uint32*>(surface->pixels);
                    for (int py = 0; py < cached.h; ++py) for (int px = 0; px < cached.w; ++px) {
                        const Uint8 alpha = bm.buffer[py * bm.pitch + px];
                        pixels[py * (surface->pitch / 4) + px] = SDL_MapRGBA(surface->format, 255, 255, 255, alpha);
                    }
                    cached.texture = SDL_CreateTextureFromSurface(renderer, surface); SDL_FreeSurface(surface);
                    SDL_SetTextureBlendMode(cached.texture, SDL_BLENDMODE_BLEND);
                }
            }
            it = glyph_cache.emplace(key, cached).first;
        }
        auto& g = it->second;
        if (g.texture) {
            SDL_SetTextureColorMod(g.texture, color.r, color.g, color.b);
            SDL_SetTextureAlphaMod(g.texture, color.a);
            SDL_Rect dst{x + g.left, baseline - g.top, g.w, g.h}; SDL_RenderCopy(renderer, g.texture, nullptr, &dst);
        }
        x += g.advance ? g.advance : pixel_size / 2;
    }
}
#endif
}

void draw_text(SDL_Renderer* renderer, int x, int y, const std::string& text, int scale, SDL_Color color) {
#ifdef __SWITCH__
    if (ensure_font()) { draw_unicode(renderer, x, y, text, scale, color); return; }
#endif
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    const int origin = x;
    for (unsigned char raw : text) {
        if (raw == '\n') { x = origin; y += 9 * scale; continue; }
        if (raw >= 128) continue;
        const Glyph rows = glyph(static_cast<char>(raw));
        for (int row = 0; row < 7; ++row) for (int col = 0; col < 5; ++col) {
            if (rows[row] & (1 << (4 - col))) {
                SDL_Rect pixel{x + col * scale, y + row * scale, scale, scale};
                SDL_RenderFillRect(renderer, &pixel);
            }
        }
        x += 6 * scale;
    }
}
}

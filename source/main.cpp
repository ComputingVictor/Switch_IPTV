#include "http_client.hpp"
#include "epg.hpp"
#include "logo_cache.hpp"
#include "player.hpp"
#include "playlist.hpp"
#include "storage.hpp"
#include "ui_text.hpp"

#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <vector>

#ifdef __SWITCH__
#include <switch.h>
#endif

using namespace switchiptv;

static void draw_gradient(SDL_Renderer* renderer, SDL_Color top, SDL_Color bottom) {
    for (int y = 0; y < 720; ++y) {
        const float t = static_cast<float>(y) / 719.0f;
        const auto mix = [t](Uint8 a, Uint8 b) { return static_cast<Uint8>(a + (b - a) * t); };
        SDL_SetRenderDrawColor(renderer, mix(top.r, bottom.r), mix(top.g, bottom.g), mix(top.b, bottom.b), 255);
        SDL_RenderDrawLine(renderer, 0, y, 1279, y);
    }
}

static void draw_card(SDL_Renderer* renderer, const SDL_Rect& rect, SDL_Color fill, SDL_Color border) {
    SDL_Rect shadow{rect.x + 5, rect.y + 6, rect.w, rect.h};
    SDL_SetRenderDrawColor(renderer, 4, 7, 13, 150); SDL_RenderFillRect(renderer, &shadow);
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a); SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a); SDL_RenderDrawRect(renderer, &rect);
}

static void draw_filled_circle(SDL_Renderer* renderer, int cx, int cy, int radius, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int y = -radius; y <= radius; ++y) {
        const int half = static_cast<int>(std::sqrt(static_cast<double>(radius * radius - y * y)));
        SDL_RenderDrawLine(renderer, cx - half, cy + y, cx + half, cy + y);
    }
}

static int draw_button_hint(SDL_Renderer* renderer, int x, int y, const std::string& button,
                            const std::string& action, SDL_Color color) {
    draw_filled_circle(renderer, x + 14, y + 14, 14, color);
    draw_text(renderer, x + 10, y + 7, button, 2, SDL_Color{255, 255, 255, 255});
    draw_text(renderer, x + 36, y + 7, action, 2, SDL_Color{185, 198, 216, 255});
    return 42 + static_cast<int>(action.size()) * 12;
}

static int draw_shoulder_hint(SDL_Renderer* renderer, int x, int y, const std::string& action) {
    SDL_Rect key{x, y + 1, 46, 27};
    SDL_SetRenderDrawColor(renderer, 67, 82, 105, 255); SDL_RenderFillRect(renderer, &key);
    SDL_SetRenderDrawColor(renderer, 123, 145, 173, 255); SDL_RenderDrawRect(renderer, &key);
    draw_text(renderer, x + 7, y + 7, "L/R", 2, SDL_Color{255, 255, 255, 255});
    draw_text(renderer, x + 56, y + 7, action, 2, SDL_Color{185, 198, 216, 255});
    return 62 + static_cast<int>(action.size()) * 12;
}

static std::string clean_source(std::string source) {
    const auto begin = source.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const auto end = source.find_last_not_of(" \t\r\n");
    source = source.substr(begin, end - begin + 1);
    if (source.size() >= 2 && ((source.front() == '"' && source.back() == '"') ||
        (source.front() == '\'' && source.back() == '\''))) source = source.substr(1, source.size() - 2);
    return source;
}

static std::string read_source(const std::string& raw_source) {
    const std::string source = clean_source(raw_source);
    if (source.rfind("http://", 0) == 0 || source.rfind("https://", 0) == 0) return HttpClient{}.get(source);
    std::ifstream input(source, std::ios::binary);
    if (!input) throw std::runtime_error("Could not open " + source);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

static bool load_playlist(const Settings& settings, Playlist& playlist, std::string& error) {
    playlist = {};
    error.clear();
    if (settings.playlist_source.empty()) {
        error = "NO PLAYLIST. PRESS X TO ADD ONE";
        return false;
    }
    try {
        playlist = M3uParser::parse(read_source(settings.playlist_source), settings.playlist_name);
        if (playlist.channels.empty()) error = "THE PLAYLIST CONTAINS NO CHANNELS";
    } catch (const std::exception& e) { error = e.what(); }
    return !playlist.channels.empty();
}

static bool request_playlist_source(const std::string& initial, std::string& output) {
#ifdef __SWITCH__
    SwkbdConfig keyboard;
    if (R_FAILED(swkbdCreate(&keyboard, 0))) return false;
    swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetHeaderText(&keyboard, "Add IPTV playlist");
    swkbdConfigSetSubText(&keyboard, "Enter an M3U/M3U8 URL or an sdmc:/ path");
    swkbdConfigSetGuideText(&keyboard, "https://servidor/lista.m3u");
    swkbdConfigSetOkButtonText(&keyboard, "Load");
    swkbdConfigSetStringLenMax(&keyboard, 2047);
    if (!initial.empty()) swkbdConfigSetInitialText(&keyboard, initial.c_str());
    char buffer[2048]{};
    const Result result = swkbdShow(&keyboard, buffer, sizeof(buffer));
    swkbdClose(&keyboard);
    if (R_FAILED(result) || buffer[0] == '\0') return false;
    output = buffer;
    return true;
#else
    (void)initial; (void)output;
    return false;
#endif
}

static std::vector<std::string> playlist_categories(const Playlist& playlist) {
    std::vector<std::string> result{"ALL", "FAVORITES"};
    std::set<std::string> seen;
    for (const auto& channel : playlist.channels) {
        const std::string group = channel.group.empty() ? "UNCATEGORIZED" : channel.group;
        if (seen.insert(group).second) result.push_back(group);
    }
    return result;
}

static std::vector<std::size_t> visible_channels(const Playlist& playlist,
                                                  const std::set<std::string>& favorites,
                                                  const std::string& category) {
    std::vector<std::size_t> result;
    for (std::size_t i = 0; i < playlist.channels.size(); ++i) {
        const auto& channel = playlist.channels[i];
        const std::string group = channel.group.empty() ? "UNCATEGORIZED" : channel.group;
        if (category == "ALL" || (category == "FAVORITES" && favorites.count(channel.url)) || category == group)
            result.push_back(i);
    }
    return result;
}

int main(int argc, char** argv) {
#ifdef __SWITCH__
    const Result socket_result = socketInitializeDefault();
    const bool socket_ready = R_SUCCEEDED(socket_result);
    const std::string base = "sdmc:/switch/SwitchIPTV/config";
#else
    const bool socket_ready = true;
    const std::string base = "config";
#endif
    Storage storage(base);
    auto settings = storage.load_settings();
    if (argc > 1) { settings.playlist_source = argv[1]; storage.save_settings(settings); }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0) return 1;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    SDL_Window* window = SDL_CreateWindow("SwitchIPTV", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                           1280, 720, SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
    if (!window || !renderer) {
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
#ifdef __SWITCH__
        if (socket_ready) socketExit();
#endif
        return 2;
    }
    // All UI coordinates use 1280x720. SDL scales this logical canvas to the
    // complete physical output (720p handheld or 1080p docked).
    SDL_RenderSetLogicalSize(renderer, 1280, 720);
    SDL_RenderSetIntegerScale(renderer, SDL_FALSE);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    Playlist playlist;
    std::string error;
    if (!socket_ready && (settings.playlist_source.rfind("http://", 0) == 0 || settings.playlist_source.rfind("https://", 0) == 0))
        error = "COULD NOT INITIALIZE SWITCH NETWORKING";
    else
        load_playlist(settings, playlist, error);
    auto favorites = storage.load_favorites();
    std::size_t selected = 0;
    std::size_t category_index = 0;
    std::string playing_name;
    Uint64 osd_until = 0;
    auto categories = playlist_categories(playlist);
    auto visible = visible_channels(playlist, favorites, categories[category_index]);
    auto refresh_view = [&]() {
        categories = playlist_categories(playlist);
        if (category_index >= categories.size()) category_index = 0;
        visible = visible_channels(playlist, favorites, categories[category_index]);
        if (visible.empty()) selected = 0;
        else if (selected >= visible.size()) selected = visible.size() - 1;
    };
    bool running = true;
    {
    Player player(renderer);
    LogoCache logos(renderer);
    EpgGuide epg;
    epg.load_async(playlist.epg_url);
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i)) SDL_GameControllerOpen(i);
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_KEYDOWN) {
                int key = e.type == SDL_KEYDOWN ? e.key.keysym.sym : -1;
                int button = e.type == SDL_CONTROLLERBUTTONDOWN ? e.cbutton.button : -1;
                // SDL nombra los botones por disposicion Xbox; en Switch las letras fisicas estan cruzadas.
                const bool switch_a = button == SDL_CONTROLLER_BUTTON_B;
                const bool switch_b = button == SDL_CONTROLLER_BUTTON_A;
                const bool switch_x = button == SDL_CONTROLLER_BUTTON_Y;
                const bool switch_y = button == SDL_CONTROLLER_BUTTON_X;
                const bool switch_plus = button == SDL_CONTROLLER_BUTTON_START;
                if (key == SDLK_ESCAPE || switch_b) {
                    if (player.active()) player.stop(); else running = false;
                } else if (player.active() && switch_plus) {
                    osd_until = SDL_GetTicks64() + 5000;
                } else if (!player.active() && !visible.empty() && (key == SDLK_DOWN || button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)) {
                    selected = (selected + 1) % visible.size();
                } else if (!player.active() && !visible.empty() && (key == SDLK_UP || button == SDL_CONTROLLER_BUTTON_DPAD_UP)) {
                    selected = (selected + visible.size() - 1) % visible.size();
                } else if (!player.active() && !categories.empty() &&
                           (key == SDLK_LEFT || button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) {
                    category_index = (category_index + categories.size() - 1) % categories.size();
                    selected = 0; refresh_view();
                } else if (!player.active() && !categories.empty() &&
                           (key == SDLK_RIGHT || button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) {
                    category_index = (category_index + 1) % categories.size();
                    selected = 0; refresh_view();
                } else if (player.active() && (key == SDLK_RETURN || switch_a)) {
                    player.toggle_pause(); osd_until = SDL_GetTicks64() + 4000;
                } else if (player.active() && (key == SDLK_x || switch_x)) {
                    player.next_audio_track(); osd_until = SDL_GetTicks64() + 5000;
                } else if (player.active() && (key == SDLK_y || switch_y)) {
                    player.next_subtitle_track(); osd_until = SDL_GetTicks64() + 5000;
                } else if (!player.active() && (key == SDLK_x || switch_x)) {
                    std::string source;
                    if (request_playlist_source(settings.playlist_source, source)) {
                        settings.playlist_source = clean_source(source);
                        storage.save_settings(settings);
                        if (!socket_ready && (settings.playlist_source.rfind("http://", 0) == 0 || settings.playlist_source.rfind("https://", 0) == 0))
                            error = "COULD NOT INITIALIZE SWITCH NETWORKING";
                        else
                            load_playlist(settings, playlist, error);
                        selected = 0;
                        category_index = 0;
                        refresh_view();
                        epg.load_async(playlist.epg_url);
                    }
                } else if (!visible.empty() && (key == SDLK_RETURN || switch_a)) {
                    const auto& ch = playlist.channels[visible[selected]];
                    playing_name = ch.name; player.load(ch.url, ch.user_agent, ch.referrer);
                    osd_until = SDL_GetTicks64() + 5000;
                } else if (!visible.empty() && (key == SDLK_y || switch_y)) {
                    const auto& url = playlist.channels[visible[selected]].url;
                    if (favorites.erase(url) == 0) favorites.insert(url);
                    storage.save_favorites(favorites);
                    refresh_view();
                }
            }
            if (e.type == SDL_FINGERDOWN && !visible.empty() && e.tfinger.x > 0.25f && e.tfinger.y > 0.15f) {
                const std::size_t first = selected > 5 ? selected - 5 : 0;
                const int row = static_cast<int>((e.tfinger.y * 720.0f - 116.0f) / 49.0f);
                if (row >= 0 && first + static_cast<std::size_t>(row) < visible.size())
                    selected = first + static_cast<std::size_t>(row);
            }
        }
        SDL_SetRenderDrawColor(renderer, 8, 11, 19, 255); SDL_RenderClear(renderer);
        if (player.active()) {
            SDL_Rect video_area{0, 0, 1280, 720};
            player.render(video_area);
            const std::string subtitle = player.subtitle_text();
            if (!subtitle.empty()) {
                SDL_Rect subtitle_bg{120, 530, 1040, 52};
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 185); SDL_RenderFillRect(renderer, &subtitle_bg);
                std::string line = subtitle; if (line.size() > 76) line.resize(76);
                draw_text(renderer, 148, 545, line, 3, SDL_Color{255, 255, 255, 255});
            }
            if (SDL_GetTicks64() < osd_until) {
                SDL_Rect osd{0, 602, 1280, 118};
                SDL_SetRenderDrawColor(renderer, 5, 10, 18, 220); SDL_RenderFillRect(renderer, &osd);
                std::string title = playing_name; if (title.size() > 52) title.resize(52);
                draw_text(renderer, 28, 620, title, 3, SDL_Color{245, 248, 252, 255});
                draw_text(renderer, 28, 652, player.paused() ? "PAUSED" : "PLAYING", 2,
                          player.paused() ? SDL_Color{255, 194, 74, 255} : SDL_Color{82, 214, 133, 255});
                int x = 290;
                x += draw_button_hint(renderer, x, 635, "A", player.paused() ? "Resume" : "Pause", SDL_Color{52, 170, 92, 255});
                x += draw_button_hint(renderer, x, 635, "X", "Audio: " + player.audio_track(), SDL_Color{45, 132, 210, 255});
                x += draw_button_hint(renderer, x, 635, "Y", "Subs: " + player.subtitle_track(), SDL_Color{221, 177, 43, 255});
                draw_button_hint(renderer, std::min(x, 1150), 635, "B", "Back", SDL_Color{205, 66, 74, 255});
            }
        } else if (!playlist.channels.empty()) {
            draw_gradient(renderer, SDL_Color{12, 20, 38, 255}, SDL_Color{7, 10, 18, 255});
            SDL_SetRenderDrawColor(renderer, 22, 118, 210, 255);
            SDL_Rect accent{0, 0, 9, 720}; SDL_RenderFillRect(renderer, &accent);
            draw_text(renderer, 34, 20, "SWITCH IPTV", 4, SDL_Color{244, 248, 255, 255});
            draw_text(renderer, 34, 60, "YOUR TV. YOUR CHANNELS.", 2, SDL_Color{82, 181, 255, 255});
            int hint_x = 604;
            hint_x += draw_shoulder_hint(renderer, hint_x, 27, "Category");
            hint_x += draw_button_hint(renderer, hint_x, 27, "A", "Play", SDL_Color{52, 170, 92, 255});
            hint_x += draw_button_hint(renderer, hint_x, 27, "Y", "Favorite", SDL_Color{221, 177, 43, 255});
            draw_button_hint(renderer, hint_x, 27, "X", "Playlist", SDL_Color{45, 132, 210, 255});

            SDL_Rect sidebar{22, 104, 292, 592};
            draw_card(renderer, sidebar, SDL_Color{18, 27, 43, 255}, SDL_Color{38, 54, 78, 255});
            draw_text(renderer, 42, 128, "BROWSE", 3, SDL_Color{82, 181, 255, 255});
            SDL_SetRenderDrawColor(renderer, 42, 61, 87, 255);
            SDL_Rect divider{42, 160, 250, 2}; SDL_RenderFillRect(renderer, &divider);
            const std::size_t cat_first = category_index > 7 ? category_index - 7 : 0;
            for (std::size_t i = cat_first; i < categories.size() && i < cat_first + 15; ++i) {
                SDL_Rect cat_row{34, 178 + static_cast<int>(i - cat_first) * 32, 268, 28};
                if (i == category_index) {
                    SDL_SetRenderDrawColor(renderer, 20, 118, 214, 255); SDL_RenderFillRect(renderer, &cat_row);
                    SDL_SetRenderDrawColor(renderer, 91, 195, 255, 255);
                    SDL_Rect marker{cat_row.x, cat_row.y, 5, cat_row.h}; SDL_RenderFillRect(renderer, &marker);
                }
                std::string name = categories[i]; if (name.size() > 22) name.resize(22);
                draw_text(renderer, 48, cat_row.y + 7, name, 2,
                          i == category_index ? SDL_Color{255, 255, 255, 255} : SDL_Color{174, 187, 207, 255});
            }

            std::string heading = categories[category_index] + "  " + std::to_string(visible.size()) + " CHANNELS";
            if (heading.size() > 55) heading.resize(55);
            draw_text(renderer, 344, 111, heading, 3, SDL_Color{235, 242, 252, 255});
            if (visible.empty())
                draw_text(renderer, 344, 190, "NO CHANNELS IN THIS CATEGORY", 2, SDL_Color{170, 180, 195, 255});
            const int row_h = 49;
            const std::size_t first = selected > 5 ? selected - 5 : 0;
            for (std::size_t pos = first; pos < visible.size() && pos < first + 11; ++pos) {
                const auto channel_index = visible[pos];
                const auto& channel = playlist.channels[channel_index];
                logos.request(channel.logo);
                SDL_Rect row{332, 150 + static_cast<int>(pos - first) * row_h, 924, row_h - 6};
                const bool focused = pos == selected;
                draw_card(renderer, row,
                          focused ? SDL_Color{22, 111, 198, 255} : SDL_Color{22, 31, 47, 255},
                          focused ? SDL_Color{91, 195, 255, 255} : SDL_Color{35, 49, 70, 255});
                SDL_Rect logo_box{row.x + 8, row.y + 4, 35, 35};
                if (SDL_Texture* logo = logos.texture(channel.logo)) SDL_RenderCopy(renderer, logo, nullptr, &logo_box);
                else {
                    SDL_SetRenderDrawColor(renderer, focused ? 255 : 77, focused ? 255 : 92, focused ? 255 : 113, 255);
                    SDL_RenderDrawRect(renderer, &logo_box);
                    draw_text(renderer, row.x + 14, row.y + 14, std::to_string(channel_index + 1), 1,
                              SDL_Color{220, 232, 245, 255});
                }
                std::string label = channel.name;
                if (label.size() > 43) label.resize(43);
                draw_text(renderer, row.x + 56, row.y + 11, label, 3, SDL_Color{245, 248, 252, 255});
                std::string current_program = epg.now(channel.tvg_id);
                if (current_program.empty() && epg.loading()) current_program = "Loading guide...";
                if (current_program.size() > 72) current_program.resize(72);
                if (!current_program.empty())
                    draw_text(renderer, row.x + 57, row.y + 30, current_program, 1, SDL_Color{150, 174, 201, 255});
                if (favorites.count(channel.url))
                    draw_text(renderer, row.x + row.w - 36, row.y + 13, "*", 3, SDL_Color{255, 204, 79, 255});
            }
            if (visible.size() > 11) {
                SDL_SetRenderDrawColor(renderer, 38, 51, 71, 255);
                SDL_Rect track{1264, 150, 5, 533}; SDL_RenderFillRect(renderer, &track);
                const int thumb_h = std::max(34, static_cast<int>(533 * 11 / visible.size()));
                const int thumb_y = 150 + static_cast<int>((533 - thumb_h) * selected / (visible.size() - 1));
                SDL_SetRenderDrawColor(renderer, 82, 181, 255, 255);
                SDL_Rect thumb{1264, thumb_y, 5, thumb_h}; SDL_RenderFillRect(renderer, &thumb);
            }
            const std::string playback_error = player.last_error();
            if (!playback_error.empty())
                draw_text(renderer, 28, 680, playback_error.substr(0, 80), 2, SDL_Color{255, 120, 120, 255});
        } else {
            SDL_SetRenderDrawColor(renderer, error.empty() ? 55 : 130, 40, 45, 255);
            SDL_Rect panel{160, 220, 960, 280}; SDL_RenderFillRect(renderer, &panel);
            draw_text(renderer, 210, 275, "SWITCH IPTV", 5, SDL_Color{255, 255, 255, 255});
            std::string message = error.empty() ? "NO PLAYLIST" : error;
            if (message.size() > 70) message.resize(70);
            draw_text(renderer, 210, 350, message, 2, SDL_Color{255, 220, 220, 255});
            if (!settings.playlist_source.empty())
                draw_text(renderer, 210, 385, ("PLAYLIST: " + settings.playlist_source).substr(0, 72), 2,
                          SDL_Color{255, 220, 220, 255});
            draw_button_hint(renderer, 210, 415, "X", "ADD OR CHANGE PLAYLIST", SDL_Color{45, 132, 210, 255});
        }
        SDL_RenderPresent(renderer);
    }
    }
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
#ifdef __SWITCH__
    if (socket_ready) socketExit();
#endif
    return 0;
}

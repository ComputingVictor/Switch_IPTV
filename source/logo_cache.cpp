#include "logo_cache.hpp"
#include "http_client.hpp"
#include <SDL2/SDL.h>
#include <png.h>

namespace switchiptv {
LogoCache::LogoCache(SDL_Renderer* renderer) : renderer_(renderer), worker_(&LogoCache::worker_loop, this) {}

LogoCache::~LogoCache() {
    { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
    wake_.notify_one();
    if (worker_.joinable()) worker_.join();
    for (auto& pair : entries_) if (pair.second.texture) SDL_DestroyTexture(pair.second.texture);
}

void LogoCache::request(const std::string& url) {
    if (url.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (entries_.size() >= 160 || entries_.count(url)) return;
    entries_.emplace(url, Entry{}); queue_.push(url); wake_.notify_one();
}

SDL_Texture* LogoCache::texture(const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(url); if (it == entries_.end() || it->second.state != Entry::ready) return nullptr;
    auto& entry = it->second;
    if (!entry.texture && !entry.pixels.empty()) {
        entry.texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, entry.w, entry.h);
        if (entry.texture) {
            SDL_UpdateTexture(entry.texture, nullptr, entry.pixels.data(), entry.w * 4);
            SDL_SetTextureBlendMode(entry.texture, SDL_BLENDMODE_BLEND);
            entry.pixels.clear(); entry.pixels.shrink_to_fit();
        }
    }
    return entry.texture;
}

void LogoCache::worker_loop() {
    for (;;) {
        std::string url;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
            if (stopping_) return;
            url = queue_.front(); queue_.pop(); entries_[url].state = Entry::loading;
        }
        Entry decoded; decoded.state = Entry::failed;
        try {
            const std::string bytes = HttpClient{}.get(url, 12);
            if (bytes.size() <= 2 * 1024 * 1024) {
                png_image image{}; image.version = PNG_IMAGE_VERSION;
                if (png_image_begin_read_from_memory(&image, bytes.data(), bytes.size()) && image.width <= 1024 && image.height <= 1024) {
                    image.format = PNG_FORMAT_RGBA; decoded.w = image.width; decoded.h = image.height;
                    decoded.pixels.resize(PNG_IMAGE_SIZE(image));
                    if (png_image_finish_read(&image, nullptr, decoded.pixels.data(), 0, nullptr)) decoded.state = Entry::ready;
                    else decoded.pixels.clear();
                    png_image_free(&image);
                }
            }
        } catch (...) {}
        std::lock_guard<std::mutex> lock(mutex_);
        auto& target = entries_[url]; target.state = decoded.state; target.w = decoded.w; target.h = decoded.h; target.pixels.swap(decoded.pixels);
    }
}
}

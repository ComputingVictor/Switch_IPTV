#pragma once
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

namespace switchiptv {
class LogoCache {
public:
    explicit LogoCache(SDL_Renderer* renderer);
    ~LogoCache();
    void request(const std::string& url);
    SDL_Texture* texture(const std::string& url);
private:
    struct Entry { enum State { queued, loading, ready, failed } state = queued; int w{}, h{}; std::vector<std::uint8_t> pixels; SDL_Texture* texture{}; };
    void worker_loop();
    SDL_Renderer* renderer_{};
    std::map<std::string, Entry> entries_;
    std::queue<std::string> queue_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_{};
    std::thread worker_;
};
}

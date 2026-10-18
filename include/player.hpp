#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct AVFormatContext;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Rect;

namespace switchiptv {
class Player {
public:
    struct Frame {
        std::vector<std::uint8_t> pixels;
        int width = 0;
        int height = 0;
    };
    enum class Aspect { Auto, SixteenByNine, FourByThree, Fill };
    explicit Player(SDL_Renderer* renderer);
    ~Player();
    void load(const std::string& url, const std::string& user_agent = {}, const std::string& referrer = {});
    void stop();
    void toggle_pause();
    void next_audio_track();
    void next_subtitle_track();
    void render(const SDL_Rect& destination);
    void set_aspect(Aspect aspect) { aspect_ = aspect; }
    Aspect aspect() const { return aspect_; }
    bool active() const { return active_.load(); }
    bool paused() const { return paused_.load(); }
    std::string last_error() const;
    std::string audio_track() const;
    std::string subtitle_track() const;
    std::string subtitle_text() const;
    Frame frame() const;
private:
    void decode(std::string url, std::string user_agent, std::string referrer);
    void set_error(const std::string& message);

    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    std::thread worker_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> active_{false};
    std::atomic<bool> paused_{false};
    std::atomic<unsigned int> audio_device_{0};
    mutable std::mutex state_mutex_;
    std::string last_error_;
    struct Track { int stream_index{}; std::string label; };
    std::vector<Track> audio_tracks_;
    std::vector<Track> subtitle_tracks_;
    std::atomic<int> requested_audio_{0};
    std::atomic<int> requested_subtitle_{-1};
    std::string subtitle_text_;
    std::int64_t subtitle_until_ms_{};
    std::vector<std::uint8_t> pixels_;
    int frame_width_ = 0;
    int frame_height_ = 0;
    bool frame_ready_ = false;
    Aspect aspect_ = Aspect::Auto;
};
}

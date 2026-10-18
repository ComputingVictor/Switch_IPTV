#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace switchiptv {
struct EpgProgram { std::int64_t start{}; std::int64_t stop{}; std::string title; };

class EpgGuide {
public:
    EpgGuide() = default;
    ~EpgGuide();
    void load_async(const std::string& url);
    std::string now(const std::string& channel_id) const;
    std::string next(const std::string& channel_id) const;
    bool loading() const { return loading_.load(); }
private:
    void stop();
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::vector<EpgProgram>> programs_;
    std::thread worker_;
    std::atomic<bool> loading_{false};
};
}

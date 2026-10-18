#include "storage.hpp"

#include <filesystem>
#include <fstream>

namespace switchiptv {
namespace {
std::string escape(const std::string& value) {
    std::string out;
    for (char c : value) {
        if (c == '\\' || c == '\n' || c == '=') out.push_back('\\');
        out.push_back(c == '\n' ? 'n' : c);
    }
    return out;
}
std::string unescape(const std::string& value) {
    std::string out;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            const char next = value[++i];
            out.push_back(next == 'n' ? '\n' : next);
        } else out.push_back(value[i]);
    }
    return out;
}
}

Storage::Storage(std::string directory) : directory_(std::move(directory)) {}

Settings Storage::load_settings() const {
    Settings s;
    std::ifstream in(directory_ + "/settings.ini");
    std::string line;
    while (std::getline(in, line)) {
        auto split = line.find('=');
        if (split == std::string::npos) continue;
        auto key = line.substr(0, split);
        auto value = unescape(line.substr(split + 1));
        if (key == "playlist_source") s.playlist_source = value;
        else if (key == "playlist_name") s.playlist_name = value;
    }
    return s;
}

bool Storage::save_settings(const Settings& s) const {
    std::filesystem::create_directories(directory_);
    std::ofstream out(directory_ + "/settings.ini", std::ios::trunc);
    out << "playlist_source=" << escape(s.playlist_source) << '\n';
    out << "playlist_name=" << escape(s.playlist_name) << '\n';
    return out.good();
}

std::set<std::string> Storage::load_favorites() const {
    std::set<std::string> result;
    std::ifstream in(directory_ + "/favorites.txt");
    std::string line;
    while (std::getline(in, line)) if (!line.empty()) result.insert(unescape(line));
    return result;
}

bool Storage::save_favorites(const std::set<std::string>& urls) const {
    std::filesystem::create_directories(directory_);
    std::ofstream out(directory_ + "/favorites.txt", std::ios::trunc);
    for (const auto& url : urls) out << escape(url) << '\n';
    return out.good();
}

} // namespace switchiptv

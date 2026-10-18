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
        else if (key == "epg_source") s.epg_source = value;
        else if (key.rfind("playlist_source_", 0) == 0 && !value.empty()) s.additional_sources.push_back(value);
        else if (key == "hidden_groups") {
            std::size_t start = 0;
            while (start <= value.size()) {
                const auto end = value.find(',', start);
                const auto group = value.substr(start, end == std::string::npos ? end : end - start);
                if (!group.empty()) s.hidden_groups.insert(group);
                if (end == std::string::npos) break;
                start = end + 1;
            }
        }
    }
    return s;
}

bool Storage::save_settings(const Settings& s) const {
    std::filesystem::create_directories(directory_);
    std::ofstream out(directory_ + "/settings.ini", std::ios::trunc);
    out << "playlist_source=" << escape(s.playlist_source) << '\n';
    out << "playlist_name=" << escape(s.playlist_name) << '\n';
    out << "epg_source=" << escape(s.epg_source) << '\n';
    for (std::size_t i = 0; i < s.additional_sources.size(); ++i)
        out << "playlist_source_" << (i + 2) << '=' << escape(s.additional_sources[i]) << '\n';
    out << "hidden_groups=";
    bool first = true;
    for (const auto& group : s.hidden_groups) {
        if (!first) out << ',';
        out << escape(group);
        first = false;
    }
    out << '\n';
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

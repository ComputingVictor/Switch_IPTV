#include "playlist.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace switchiptv {
namespace {
std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

std::unordered_map<std::string, std::string> attributes(const std::string& line) {
    std::unordered_map<std::string, std::string> out;
    auto end = line.find(',');
    auto fields = line.substr(0, end);
    const bool extinf = fields.rfind("#EXTINF:", 0) == 0;
    std::size_t p = extinf ? fields.find(':') : fields.find(' ');
    if (p == std::string::npos) return out;
    ++p;
    if (extinf) {
        p = fields.find_first_of(" \t", p);
        if (p == std::string::npos) return out;
    }
    while (p < fields.size()) {
        while (p < fields.size() && std::isspace(static_cast<unsigned char>(fields[p]))) ++p;
        auto eq = fields.find('=', p);
        if (eq == std::string::npos) break;
        std::string key = trim(fields.substr(p, eq - p));
        p = eq + 1;
        std::string value;
        if (p < fields.size() && (fields[p] == '"' || fields[p] == '\'')) {
            const char quote = fields[p++];
            const auto close = fields.find(quote, p);
            if (close == std::string::npos) break;
            value = fields.substr(p, close - p);
            p = close + 1;
        } else {
            auto space = fields.find(' ', p);
            value = fields.substr(p, space - p);
            p = space == std::string::npos ? fields.size() : space + 1;
        }
        out[key] = value;
    }
    return out;
}

std::string get(const std::unordered_map<std::string, std::string>& attrs, const char* key) {
    const auto it = attrs.find(key);
    return it == attrs.end() ? std::string{} : it->second;
}

bool truthy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value == "1" || value == "true" || value == "yes";
}
}

Playlist M3uParser::parse(const std::string& input, const std::string& fallback_name) {
    std::string text = input;
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xef &&
        static_cast<unsigned char>(text[1]) == 0xbb && static_cast<unsigned char>(text[2]) == 0xbf) {
        text.erase(0, 3);
    }
    std::istringstream stream(text);
    std::string line;
    if (!std::getline(stream, line) || trim(line).rfind("#EXTM3U", 0) != 0)
        throw std::runtime_error("The content is not a valid M3U playlist");

    Playlist result;
    result.name = fallback_name;
    auto header = attributes(line);
    result.epg_url = get(header, "x-tvg-url");
    if (result.epg_url.empty()) result.epg_url = get(header, "url-tvg");

    Channel pending;
    bool have_info = false;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        line = trim(line);
        if (line.empty()) continue;
        if (line.rfind("#EXTINF:", 0) == 0) {
            pending = {};
            have_info = true;
            const auto attrs = attributes(line);
            const auto comma = line.find(',');
            pending.name = comma == std::string::npos ? get(attrs, "tvg-name") : trim(line.substr(comma + 1));
            if (pending.name.empty()) pending.name = get(attrs, "tvg-name");
            pending.tvg_id = get(attrs, "tvg-id");
            pending.tvg_name = get(attrs, "tvg-name");
            pending.tvg_url = get(attrs, "tvg-url");
            pending.logo = get(attrs, "tvg-logo");
            pending.group = get(attrs, "group-title");
            pending.user_agent = get(attrs, "user-agent");
            pending.referrer = get(attrs, "http-referrer");
            pending.catchup = get(attrs, "catchup");
            pending.catchup_days = get(attrs, "catchup-days");
            pending.catchup_source = get(attrs, "catchup-source");
            pending.timeshift = get(attrs, "timeshift");
            pending.radio = truthy(get(attrs, "radio"));
        } else if (have_info && line.rfind("#EXTGRP:", 0) == 0) {
            pending.group = trim(line.substr(8));
        } else if (have_info && line.rfind("#EXTVLCOPT:http-user-agent=", 0) == 0) {
            pending.user_agent = line.substr(27);
        } else if (have_info && line.rfind("#EXTVLCOPT:http-referrer=", 0) == 0) {
            pending.referrer = line.substr(25);
        } else if (have_info && line[0] != '#') {
            pending.url = line;
            if (pending.name.empty()) pending.name = pending.url;
            result.channels.push_back(std::move(pending));
            pending = {};
            have_info = false;
        }
    }
    return result;
}

} // namespace switchiptv

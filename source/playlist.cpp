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

std::string json_string(const std::string& object, const char* key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    const auto key_pos = object.find(needle);
    if (key_pos == std::string::npos) return {};
    auto p = object.find(':', key_pos + needle.size());
    if (p == std::string::npos) return {};
    const auto begin = object.find_first_not_of(" \t\r\n", p + 1);
    if (begin == std::string::npos) return {};
    if (object[begin] != '"') {
        const auto end = object.find_first_of(",}", begin);
        return object.substr(begin, end == std::string::npos ? end : end - begin);
    }
    p = begin;
    std::string value;
    for (++p; p < object.size(); ++p) {
        if (object[p] == '"' && (p == 0 || object[p - 1] != '\\')) break;
        if (object[p] == '\\' && p + 1 < object.size()) ++p;
        value.push_back(object[p]);
    }
    return value;
}

std::vector<std::string> json_objects(const std::string& json) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while ((start = json.find('{', start)) != std::string::npos) {
        int depth = 0;
        bool quoted = false;
        for (std::size_t p = start; p < json.size(); ++p) {
            if (json[p] == '"' && (p == 0 || json[p - 1] != '\\')) quoted = !quoted;
            if (quoted) continue;
            if (json[p] == '{') ++depth;
            if (json[p] == '}' && --depth == 0) {
                result.push_back(json.substr(start, p - start + 1));
                start = p + 1;
                break;
            }
        }
        if (start == 0) break;
    }
    return result;
}

std::string stream_url(const XtreamSource& source, const std::string& object, bool vod) {
    const std::string id = json_string(object, "stream_id");
    const std::string type = json_string(object, "stream_type");
    if (id.empty()) return {};
    return source.base_url + "/" + (vod ? "movie" : (type.empty() ? "live" : type)) +
           "/" + source.username + "/" + source.password + "/" + id +
           (vod ? ".mp4" : ".ts");
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

bool XtreamParser::parse_source(const std::string& source, XtreamSource& result) {
    const auto scheme = source.find("://");
    if (scheme == std::string::npos) return false;
    const auto authority_start = scheme + 3;
    const auto first_slash = source.find('/', authority_start);
    if (first_slash == std::string::npos) return false;
    const auto at = source.find('@', authority_start);
    if (at != std::string::npos && at < first_slash) {
        const auto colon = source.find(':', authority_start);
        if (colon == std::string::npos || colon > at) return false;
        result.base_url = source.substr(0, scheme + 3) + source.substr(at + 1, first_slash - at - 1);
        result.username = source.substr(authority_start, colon - authority_start);
        result.password = source.substr(colon + 1, at - colon - 1);
    } else {
        const auto second_slash = source.find('/', first_slash + 1);
        if (second_slash == std::string::npos) return false;
        result.base_url = source.substr(0, first_slash);
        if (result.base_url.rfind("xtream://", 0) == 0) result.base_url.replace(0, 9, "http://");
        result.username = source.substr(first_slash + 1, second_slash - first_slash - 1);
        result.password = source.substr(second_slash + 1);
    }
    while (!result.base_url.empty() && result.base_url.back() == '/') result.base_url.pop_back();
    return !result.username.empty() && !result.password.empty();
}

Playlist XtreamParser::parse_live(const std::string& json, const XtreamSource& source,
                                  const std::string& fallback_name) {
    Playlist result; result.name = fallback_name;
    for (const auto& object : json_objects(json)) {
        Channel channel;
        channel.name = json_string(object, "name");
        channel.group = json_string(object, "category_name");
        channel.logo = json_string(object, "stream_icon");
        channel.tvg_id = json_string(object, "epg_channel_id");
        channel.url = stream_url(source, object, false);
        if (!channel.name.empty() && !channel.url.empty()) result.channels.push_back(std::move(channel));
    }
    return result;
}

Playlist XtreamParser::parse_vod(const std::string& json, const XtreamSource& source,
                                 const std::string& fallback_name) {
    Playlist result; result.name = fallback_name;
    for (const auto& object : json_objects(json)) {
        Channel channel;
        channel.name = json_string(object, "name");
        channel.group = json_string(object, "category_name");
        channel.logo = json_string(object, "stream_icon");
        channel.url = stream_url(source, object, true);
        if (!channel.name.empty() && !channel.url.empty()) result.channels.push_back(std::move(channel));
    }
    return result;
}

} // namespace switchiptv

#pragma once

#include <string>
#include <vector>

namespace switchiptv {

struct Channel {
    std::string name;
    std::string url;
    std::string group;
    std::string tvg_id;
    std::string tvg_name;
    std::string tvg_url;
    std::string logo;
    std::string user_agent;
    std::string referrer;
    std::string catchup;
    std::string catchup_days;
    std::string catchup_source;
    std::string timeshift;
    bool radio = false;
};

struct Playlist {
    std::string name;
    std::string epg_url;
    std::vector<Channel> channels;
};

class M3uParser {
public:
    static Playlist parse(const std::string& text, const std::string& fallback_name = "Playlist");
};

struct XtreamSource {
    std::string base_url;
    std::string username;
    std::string password;
};

class XtreamParser {
public:
    static bool parse_source(const std::string& source, XtreamSource& result);
    static Playlist parse_live(const std::string& json, const XtreamSource& source,
                               const std::string& fallback_name = "Xtream Live");
    static Playlist parse_vod(const std::string& json, const XtreamSource& source,
                              const std::string& fallback_name = "Xtream VOD");
};

} // namespace switchiptv

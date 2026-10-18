#pragma once

#include <set>
#include <string>

namespace switchiptv {

struct Settings {
    std::string playlist_source;
    std::string playlist_name = "My IPTV";
};

class Storage {
public:
    explicit Storage(std::string directory);
    Settings load_settings() const;
    bool save_settings(const Settings& settings) const;
    std::set<std::string> load_favorites() const;
    bool save_favorites(const std::set<std::string>& urls) const;
private:
    std::string directory_;
};

} // namespace switchiptv

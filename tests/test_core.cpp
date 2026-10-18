#include "playlist.hpp"
#include "storage.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>

using namespace switchiptv;
int main() {
    const std::string m3u = "\xEF\xBB\xBF#EXTM3U x-tvg-url=\"https://example.test/epg.xml\"\r\n"
        "#EXTINF:-1 tvg-id=\"news.one\" tvg-name=\"News\" tvg-logo=\"https://img/logo.png\" tvg-url=\"https://example.test/news.xml\" group-title=\"Noticias\" catchup=\"append\" catchup-days=\"7\" catchup-source=\"?utc={utc}\" timeshift=\"2\",News One\r\n"
        "#EXTVLCOPT:http-user-agent=AuthorizedClient\r\nhttps://example.test/live.m3u8\r\n"
        "#EXTINF:-1 radio=\"true\" http-referrer=\"https://example.test/\",Radio\n#EXTGRP:Radio\nhttps://example.test/radio.mp3\n";
    auto p = M3uParser::parse(m3u, "Test");
    assert(p.channels.size() == 2);
    assert(p.epg_url == "https://example.test/epg.xml");
    assert(p.channels[0].name == "News One" && p.channels[0].group == "Noticias");
    assert(p.channels[0].tvg_name == "News" && p.channels[0].tvg_url == "https://example.test/news.xml");
    assert(p.channels[0].user_agent == "AuthorizedClient");
    assert(p.channels[0].catchup == "append" && p.channels[0].catchup_days == "7");
    assert(p.channels[0].catchup_source == "?utc={utc}" && p.channels[0].timeshift == "2");
    assert(p.channels[1].group == "Radio" && p.channels[1].radio);
    assert(p.channels[1].referrer == "https://example.test/");
    const auto dir = std::filesystem::temp_directory_path() / "switchiptv-core-test";
    std::filesystem::remove_all(dir);
    Storage storage(dir.string());
    Settings s{"https://example.test/list.m3u", "Demo=IPTV"};
    assert(storage.save_settings(s));
    auto loaded = storage.load_settings();
    assert(loaded.playlist_source == s.playlist_source && loaded.playlist_name == s.playlist_name);
    std::set<std::string> fav{"https://example.test/live.m3u8"};
    assert(storage.save_favorites(fav));
    assert(storage.load_favorites() == fav);
    std::filesystem::remove_all(dir);
    std::cout << "core tests: OK\n";
}

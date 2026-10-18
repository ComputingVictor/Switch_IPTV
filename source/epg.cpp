#include "epg.hpp"
#include "http_client.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstring>
#include <zlib.h>

namespace switchiptv { namespace {
std::string attribute(const std::string& tag, const std::string& name) {
    const std::string key = name + "="; auto p = tag.find(key); if (p == std::string::npos) return {};
    p += key.size(); if (p >= tag.size() || (tag[p] != '\"' && tag[p] != '\'')) return {};
    const char quote = tag[p++]; const auto end = tag.find(quote, p); return end == std::string::npos ? std::string{} : tag.substr(p, end - p);
}
std::string entities(std::string value) {
    const std::pair<const char*, const char*> replacements[] = {{"&amp;","&"},{"&lt;","<"},{"&gt;",">"},{"&quot;","\""},{"&apos;","'"}};
    for (const auto& r : replacements) for (std::size_t p = 0; (p = value.find(r.first, p)) != std::string::npos; p += std::char_traits<char>::length(r.second)) value.replace(p, std::char_traits<char>::length(r.first), r.second);
    return value;
}
std::int64_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2; const int era = (y >= 0 ? y : y - 399) / 400; const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097LL + static_cast<int>(doe) - 719468;
}
std::int64_t xmltv_time(const std::string& value) {
    if (value.size() < 14) return 0;
    auto number = [&](std::size_t p, std::size_t n) { int v=0; for (std::size_t i=0;i<n;++i) { if (!std::isdigit(static_cast<unsigned char>(value[p+i]))) return -1; v=v*10+value[p+i]-'0'; } return v; };
    const int y=number(0,4), m=number(4,2), d=number(6,2), hh=number(8,2), mm=number(10,2), ss=number(12,2); if (ss < 0) return 0;
    std::int64_t epoch = days_from_civil(y,m,d)*86400LL + hh*3600 + mm*60 + ss;
    const auto space=value.find(' ',14); if (space!=std::string::npos && space+5<value.size() && (value[space+1]=='+' || value[space+1]=='-')) {
        const int oh=number(space+2,2), om=number(space+4,2); if (oh>=0 && om>=0) { const int offset=(oh*60+om)*60; epoch += value[space+1]=='+' ? -offset : offset; }
    }
    return epoch;
}
std::unordered_map<std::string, std::vector<EpgProgram>> parse_xmltv(const std::string& xml) {
    std::unordered_map<std::string, std::vector<std::string>> aliases;
    std::size_t channel_pos = 0;
    while ((channel_pos = xml.find("<channel ", channel_pos)) != std::string::npos) {
        const auto tag_end = xml.find('>', channel_pos), close = xml.find("</channel>", tag_end);
        if (tag_end == std::string::npos || close == std::string::npos) break;
        const std::string id = attribute(xml.substr(channel_pos, tag_end - channel_pos + 1), "id");
        std::size_t name_pos = tag_end;
        while ((name_pos = xml.find("<display-name", name_pos)) != std::string::npos && name_pos < close) {
            const auto begin = xml.find('>', name_pos), end = xml.find("</display-name>", begin);
            if (begin == std::string::npos || end == std::string::npos || end > close) break;
            aliases[id].push_back(entities(xml.substr(begin + 1, end - begin - 1))); name_pos = end + 15;
        }
        channel_pos = close + 10;
    }
    std::unordered_map<std::string, std::vector<EpgProgram>> result; std::size_t p=0;
    while ((p=xml.find("<programme",p))!=std::string::npos) {
        const auto tag_end=xml.find('>',p), close=xml.find("</programme>",tag_end); if (tag_end==std::string::npos || close==std::string::npos) break;
        const std::string tag=xml.substr(p,tag_end-p+1), channel=attribute(tag,"channel");
        EpgProgram program{xmltv_time(attribute(tag,"start")),xmltv_time(attribute(tag,"stop")),{}};
        const auto title=xml.find("<title",tag_end); if (title!=std::string::npos && title<close) { const auto begin=xml.find('>',title), end=xml.find("</title>",begin); if (begin!=std::string::npos && end!=std::string::npos && end<close) program.title=entities(xml.substr(begin+1,end-begin-1)); }
        if (!channel.empty() && program.start && program.stop && !program.title.empty()) result[channel].push_back(std::move(program));
        p=close+12;
    }
    for (const auto& alias : aliases) {
        const auto programs = result.find(alias.first);
        if (programs != result.end()) for (const auto& name : alias.second)
            if (!name.empty() && !result.count(name)) result[name] = programs->second;
    }
    for (auto& pair:result) std::sort(pair.second.begin(),pair.second.end(),[](const auto& a,const auto& b){return a.start<b.start;});
    return result;
}

std::string normalized_id(const std::string& value) {
    std::string result;
    for (unsigned char c : value) if (std::isalnum(c)) result.push_back(static_cast<char>(std::tolower(c)));
    return result;
}

std::string maybe_gunzip(const std::string& data) {
    if (data.size() < 2 || static_cast<unsigned char>(data[0]) != 0x1f ||
        static_cast<unsigned char>(data[1]) != 0x8b) return data;
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    stream.avail_in = static_cast<uInt>(data.size());
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) return {};
    std::string result;
    std::array<char, 16384> buffer{};
    int status = Z_OK;
    while (status == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        status = inflate(&stream, Z_NO_FLUSH);
        result.append(buffer.data(), buffer.size() - stream.avail_out);
    }
    inflateEnd(&stream);
    return status == Z_STREAM_END ? result : std::string{};
}
}}

namespace switchiptv {
EpgGuide::~EpgGuide(){ stop(); }
void EpgGuide::stop(){ if(worker_.joinable()) worker_.join(); loading_=false; }
void EpgGuide::load_async(const std::string& url){ stop(); if(url.empty()) return; loading_=true; worker_=std::thread([this,url]{ try { const auto body = maybe_gunzip(HttpClient{}.get(url,45)); auto parsed=parse_xmltv(body); std::lock_guard<std::mutex> lock(mutex_); programs_.swap(parsed); } catch(...) {} loading_=false; }); }
std::string EpgGuide::now(const std::string& id) const { if(id.empty()) return {}; const auto current=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()); std::lock_guard<std::mutex> lock(mutex_); auto it=programs_.find(id); if(it==programs_.end()) { const auto wanted=normalized_id(id); it=std::find_if(programs_.begin(),programs_.end(),[&](const auto& item){return normalized_id(item.first)==wanted;}); } if(it==programs_.end()) return {}; for(const auto& p:it->second) if(p.start<=current && current<p.stop) return p.title; return {}; }
std::string EpgGuide::next(const std::string& id) const { if(id.empty()) return {}; const auto current=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()); std::lock_guard<std::mutex> lock(mutex_); auto it=programs_.find(id); if(it==programs_.end()) { const auto wanted=normalized_id(id); it=std::find_if(programs_.begin(),programs_.end(),[&](const auto& item){return normalized_id(item.first)==wanted;}); } if(it==programs_.end()) return {}; for(const auto& p:it->second) if(p.start>current) return p.title; return {}; }
}

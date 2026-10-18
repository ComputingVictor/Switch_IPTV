#include "epg.hpp"
#include "http_client.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>

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
    std::unordered_map<std::string, std::vector<EpgProgram>> result; std::size_t p=0;
    while ((p=xml.find("<programme",p))!=std::string::npos) {
        const auto tag_end=xml.find('>',p), close=xml.find("</programme>",tag_end); if (tag_end==std::string::npos || close==std::string::npos) break;
        const std::string tag=xml.substr(p,tag_end-p+1), channel=attribute(tag,"channel");
        EpgProgram program{xmltv_time(attribute(tag,"start")),xmltv_time(attribute(tag,"stop")),{}};
        const auto title=xml.find("<title",tag_end); if (title!=std::string::npos && title<close) { const auto begin=xml.find('>',title), end=xml.find("</title>",begin); if (begin!=std::string::npos && end!=std::string::npos && end<close) program.title=entities(xml.substr(begin+1,end-begin-1)); }
        if (!channel.empty() && program.start && program.stop && !program.title.empty()) result[channel].push_back(std::move(program));
        p=close+12;
    }
    for (auto& pair:result) std::sort(pair.second.begin(),pair.second.end(),[](const auto& a,const auto& b){return a.start<b.start;});
    return result;
}
}}

namespace switchiptv {
EpgGuide::~EpgGuide(){ stop(); }
void EpgGuide::stop(){ if(worker_.joinable()) worker_.join(); loading_=false; }
void EpgGuide::load_async(const std::string& url){ stop(); if(url.empty()) return; loading_=true; worker_=std::thread([this,url]{ try { auto parsed=parse_xmltv(HttpClient{}.get(url,45)); std::lock_guard<std::mutex> lock(mutex_); programs_.swap(parsed); } catch(...) {} loading_=false; }); }
std::string EpgGuide::now(const std::string& id) const { if(id.empty()) return {}; const auto current=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()); std::lock_guard<std::mutex> lock(mutex_); const auto it=programs_.find(id); if(it==programs_.end()) return {}; for(const auto& p:it->second) if(p.start<=current && current<p.stop) return p.title; return {}; }
}

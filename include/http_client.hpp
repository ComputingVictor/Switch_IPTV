#pragma once
#include <string>

namespace switchiptv {
class HttpClient {
public:
    HttpClient();
    ~HttpClient();
    std::string get(const std::string& url, long timeout_seconds = 30) const;
};
}

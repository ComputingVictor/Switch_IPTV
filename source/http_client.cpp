#include "http_client.hpp"
#include <curl/curl.h>
#include <stdexcept>

namespace switchiptv {
namespace {
size_t append(char* data, size_t size, size_t count, void* target) {
    static_cast<std::string*>(target)->append(data, size * count);
    return size * count;
}
}
HttpClient::HttpClient() { curl_global_init(CURL_GLOBAL_DEFAULT); }
HttpClient::~HttpClient() { curl_global_cleanup(); }
std::string HttpClient::get(const std::string& url, long timeout) const {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("Could not initialize libcurl");
    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    // Keep playlist requests compatible with providers that distinguish their
    // portal/download endpoint from the media player opening each stream.
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "SwitchIPTV/0.1");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    const CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    if (result != CURLE_OK) throw std::runtime_error(std::string("Network: ") + curl_easy_strerror(result));
    if (status >= 400) throw std::runtime_error("HTTP " + std::to_string(status));
    return body;
}
}

#include "money.hpp"

#include <cstdio>
#include <ctime>

std::string rfc3339(std::chrono::system_clock::time_point tp) {
    using namespace std::chrono;
    const auto ms = duration_cast<milliseconds>(tp.time_since_epoch());
    const std::time_t secs = ms.count() / 1000;
    std::tm utc{};
    gmtime_r(&secs, &utc);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                  utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                  utc.tm_hour, utc.tm_min, utc.tm_sec, static_cast<int>(ms.count() % 1000));
    return buf;
}

namespace {
template <typename T, typename F>
std::string joinBraces(const std::vector<T>& v, F toString) {
    std::string out = "{";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += ',';
        out += toString(v[i]);
    }
    out += '}';
    return out;
}
}  // namespace

std::string pgArray(const std::vector<std::string>& v) {
    return joinBraces(v, [](const std::string& s) { return s; });
}

std::string pgArray(const std::vector<int64_t>& v) {
    return joinBraces(v, [](int64_t n) { return std::to_string(n); });
}

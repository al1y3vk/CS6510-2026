#pragma once

#include <charconv>
#include <drogon/HttpRequest.h>
#include <optional>
#include <string>

// Query parameter as a whole non-negative integer. Absent → nullopt; present but not a
// clean integer (or negative) → `bad` is set, so the caller can answer 400.
inline std::optional<long> queryInt(const drogon::HttpRequestPtr& req, const std::string& key, bool& bad) {
    const auto& params = req->getParameters();
    auto it = params.find(key);
    if (it == params.end()) return std::nullopt;
    const std::string& text = it->second;
    long value = 0;
    auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    bad = ec != std::errc{} || end != text.data() + text.size() || value < 0;
    return value;
}

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>

inline double centsToDouble(int64_t cents) {
    return static_cast<double>(cents) / 100.0;
}

// "2026-09-15T04:12:33.123Z" — UTC, millisecond precision.
std::string rfc3339(std::chrono::system_clock::time_point);
// {a,b,c} — Postgres array literal. SKUs and ints need no quoting.
std::string pgArray(const std::vector<std::string>&);
std::string pgArray(const std::vector<int64_t>&);
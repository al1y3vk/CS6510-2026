#pragma once

#include <atomic>

#include "api_error.hpp"
#include "catalog.hpp"

// Catalog service: GET /items. Serves its in-memory copy of the items table.
struct Services {
    Catalog catalog;
};

inline std::atomic<Services*> gServices{nullptr};

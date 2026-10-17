#pragma once

#include <drogon/orm/DbClient.h>

#include "catalog.hpp"

// items (reads). Every service that needs sku/name/price loads its own copy at startup.
// Blocking; throws std::runtime_error if the table is empty (db/reset.sh not run).
Catalog loadCatalog(const drogon::orm::DbClientPtr& db);

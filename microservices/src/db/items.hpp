#pragma once

#include <drogon/orm/DbClient.h>

#include "catalog.hpp"

// items (reads), catalog service only. The other services fetch GET /items from it instead.
// Blocking; throws std::runtime_error if the table is empty (db/reset.sh not run).
Catalog loadCatalog(const drogon::orm::DbClientPtr& db);

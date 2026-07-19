#pragma once

#include <expected>
#include <memory>
#include <optional>
#include <string>

#include "catalog/catalog_writer.hpp"
#include "catalog/repository.hpp"

namespace sonarium::catalog {

// A catalog backend opened from environment configuration. `repository` and
// `writer` alias the same underlying object.
struct OpenedCatalog {
    std::shared_ptr<Repository> repository;
    std::shared_ptr<CatalogWriter> writer;
    std::string kind; // "postgres" or "sqlite"
};

// True when SONARIUM_PG_CONNINFO or SONARIUM_SQLITE_PATH is set — i.e. the
// operator configured a durable backend (vs. the in-memory demo catalog).
[[nodiscard]] bool catalog_backend_configured();

// Open the backend the environment selects and run ensure_schema on it.
// SONARIUM_PG_CONNINFO wins over SONARIUM_SQLITE_PATH when both are set.
// nullopt = neither variable set; error = a backend was configured but
// failed to open or migrate.
[[nodiscard]] std::expected<std::optional<OpenedCatalog>, std::string> open_catalog_from_env();

} // namespace sonarium::catalog

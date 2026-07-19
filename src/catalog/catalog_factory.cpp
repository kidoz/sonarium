#include "catalog/catalog_factory.hpp"

#include <cstdlib>
#include <filesystem>

#include "catalog/postgres_repository.hpp"
#include "catalog/sqlite_repository.hpp"

namespace sonarium::catalog {

namespace {

[[nodiscard]] std::string env_or_empty(char const* name) {
    auto const* v = std::getenv(name);
    return (v != nullptr) ? std::string{v} : std::string{};
}

} // namespace

bool catalog_backend_configured() {
    return !env_or_empty("SONARIUM_PG_CONNINFO").empty()
           || !env_or_empty("SONARIUM_SQLITE_PATH").empty();
}

std::expected<std::optional<OpenedCatalog>, std::string> open_catalog_from_env() {
    if (auto const conninfo = env_or_empty("SONARIUM_PG_CONNINFO"); !conninfo.empty()) {
        auto repo = PostgresRepository::open(conninfo);
        if (!repo.has_value()) {
            return std::unexpected("postgres: " + repo.error());
        }
        if (auto schema = (*repo)->ensure_schema(); !schema.has_value()) {
            return std::unexpected("postgres: " + schema.error());
        }
        return std::optional<OpenedCatalog>{OpenedCatalog{*repo, *repo, "postgres"}};
    }

    if (auto const path = env_or_empty("SONARIUM_SQLITE_PATH"); !path.empty()) {
        auto repo = SqliteRepository::open(std::filesystem::path{path});
        if (!repo.has_value()) {
            return std::unexpected("sqlite: " + repo.error());
        }
        if (auto schema = (*repo)->ensure_schema(); !schema.has_value()) {
            return std::unexpected("sqlite: " + schema.error());
        }
        return std::optional<OpenedCatalog>{OpenedCatalog{*repo, *repo, "sqlite"}};
    }

    return std::optional<OpenedCatalog>{std::nullopt};
}

} // namespace sonarium::catalog

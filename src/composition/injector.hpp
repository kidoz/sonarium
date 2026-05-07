#pragma once

#include <memory>

#include "catalog/repository.hpp"
#include "composition/dlna_server.hpp"
#include "composition/service_config.hpp"
#include "core/logger.hpp"
#include "dlna-core/device_profile.hpp"

namespace sonarium::composition {

// Factory that wires every DLNA service through ctorwire and returns the
// fully-resolved `DlnaServer`. The caller supplies the externally-built
// catalog, profile registry, and logger — that mirrors the realistic case
// where the catalog is loaded from a database, the profile registry is
// configured from disk, and the logger is a LogSpineLogger backed by a
// production sink.
[[nodiscard]] std::shared_ptr<DlnaServer>
make_dlna_server(std::shared_ptr<sonarium::catalog::Repository> catalog,
                 std::shared_ptr<sonarium::dlna::DeviceProfileRegistry> profiles,
                 std::shared_ptr<sonarium::core::Logger> logger,
                 ServiceConfig config);

// Convenience overload: default profile registry + null logger. Useful for
// tests that don't assert on log output.
[[nodiscard]] std::shared_ptr<DlnaServer>
make_dlna_server(std::shared_ptr<sonarium::catalog::Repository> catalog, ServiceConfig config);

} // namespace sonarium::composition

#pragma once

#include <atria/application.hpp>
#include <memory>

#include "catalog/repository.hpp"
#include "composition/dlna_server.hpp"

namespace sonarium::composition {

// Routes registered on the application:
//
//   GET/HEAD /description.xml
//   GET/HEAD /ContentDirectory/scpd.xml
//   GET/HEAD /ConnectionManager/scpd.xml
//   POST     /upnp/control/content-directory
//   POST     /upnp/control/connection-manager
//   GET/HEAD /media/renditions/{id}              (resolves via the repository)
//
// All handlers capture `server` and `catalog` by shared ownership; the routes
// remain valid as long as the application object exists.
void register_dlna_routes(::atria::Application& app,
                          std::shared_ptr<DlnaServer const> server,
                          std::shared_ptr<sonarium::catalog::Repository const> catalog);

} // namespace sonarium::composition

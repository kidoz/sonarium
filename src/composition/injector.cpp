#include "composition/injector.hpp"

#include <ctorwire/ctorwire.hpp>
#include <memory>
#include <utility>

#include "composition/connection_manager_service.hpp"
#include "composition/content_directory_service.hpp"
#include "composition/dlna_server.hpp"
#include "composition/soap_router.hpp"
#include "core/null_logger.hpp"

namespace sonarium::composition {

std::shared_ptr<DlnaServer>
make_dlna_server(std::shared_ptr<sonarium::catalog::Repository> catalog,
                 std::shared_ptr<sonarium::dlna::DeviceProfileRegistry> profiles,
                 std::shared_ptr<sonarium::core::Logger> logger,
                 ServiceConfig config) {
    auto config_ptr = std::make_shared<ServiceConfig const>(std::move(config));

    auto injector = ctorwire::make_injector(
        ctorwire::instance<std::shared_ptr<sonarium::catalog::Repository>>(std::move(catalog)),
        ctorwire::instance<std::shared_ptr<sonarium::dlna::DeviceProfileRegistry>>(
            std::move(profiles)),
        ctorwire::instance<std::shared_ptr<sonarium::core::Logger>>(std::move(logger)),
        ctorwire::instance<std::shared_ptr<ServiceConfig const>>(config_ptr),
        ctorwire::bind<ContentDirectoryService>().to<ContentDirectoryService>().as_singleton(),
        ctorwire::bind<ConnectionManagerService>().to<ConnectionManagerService>().as_singleton(),
        ctorwire::bind<SoapRouter>().to<SoapRouter>().as_singleton(),
        ctorwire::bind<DlnaServer>().to<DlnaServer>().as_singleton());

    return injector.resolve<std::shared_ptr<DlnaServer>>();
}

std::shared_ptr<DlnaServer> make_dlna_server(std::shared_ptr<sonarium::catalog::Repository> catalog,
                                             ServiceConfig config) {
    auto profiles = std::make_shared<sonarium::dlna::DeviceProfileRegistry>(
        sonarium::dlna::DeviceProfileRegistry::with_defaults());
    return make_dlna_server(std::move(catalog),
                            std::move(profiles),
                            sonarium::core::make_null_logger(),
                            std::move(config));
}

} // namespace sonarium::composition

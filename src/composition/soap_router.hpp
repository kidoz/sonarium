#pragma once

#include <ctorwire/describe.hpp>
#include <memory>
#include <string>
#include <string_view>

#include "composition/connection_manager_service.hpp"
#include "composition/content_directory_service.hpp"
#include "core/logger.hpp"
#include "upnp/soap_envelope.hpp"

namespace sonarium::composition {

// Routes parsed SOAP requests to the appropriate UPnP service handler based on
// the service URN declared on the action element.
class SoapRouter {
public:
    SoapRouter(std::shared_ptr<ContentDirectoryService> content_directory,
               std::shared_ptr<ConnectionManagerService> connection_manager,
               std::shared_ptr<sonarium::core::Logger> logger);

    [[nodiscard]] std::string dispatch(sonarium::upnp::ParsedSoapRequest const& request,
                                       std::string_view user_agent) const;

    [[nodiscard]] std::string dispatch_body(std::string_view body,
                                            std::string_view user_agent) const;

private:
    std::shared_ptr<ContentDirectoryService> content_directory_;
    std::shared_ptr<ConnectionManagerService> connection_manager_;
    std::shared_ptr<sonarium::core::Logger> logger_;
};

} // namespace sonarium::composition

template <>
struct ctorwire::dependencies<sonarium::composition::SoapRouter> {
    using type = ctorwire::types<std::shared_ptr<sonarium::composition::ContentDirectoryService>,
                                 std::shared_ptr<sonarium::composition::ConnectionManagerService>,
                                 std::shared_ptr<sonarium::core::Logger>>;
};

#pragma once

#include <ctorwire/describe.hpp>
#include <memory>
#include <string>
#include <string_view>

#include "composition/service_config.hpp"
#include "core/logger.hpp"
#include "dlna-core/device_profile.hpp"
#include "upnp/soap_envelope.hpp"

namespace sonarium::composition {

class ConnectionManagerService {
public:
    ConnectionManagerService(std::shared_ptr<sonarium::dlna::DeviceProfileRegistry> profiles,
                             std::shared_ptr<ServiceConfig const> config,
                             std::shared_ptr<sonarium::core::Logger> logger);

    [[nodiscard]] std::string dispatch(sonarium::upnp::ParsedSoapRequest const& request,
                                       std::string_view user_agent) const;

    static constexpr std::string_view service_urn() noexcept {
        return "urn:schemas-upnp-org:service:ConnectionManager:1";
    }

private:
    std::shared_ptr<sonarium::dlna::DeviceProfileRegistry> profiles_;
    std::shared_ptr<ServiceConfig const> config_;
    std::shared_ptr<sonarium::core::Logger> logger_;
};

} // namespace sonarium::composition

template <>
struct ctorwire::dependencies<sonarium::composition::ConnectionManagerService> {
    using type = ctorwire::types<std::shared_ptr<sonarium::dlna::DeviceProfileRegistry>,
                                 std::shared_ptr<sonarium::composition::ServiceConfig const>,
                                 std::shared_ptr<sonarium::core::Logger>>;
};

#pragma once

#include <ctorwire/describe.hpp>
#include <memory>
#include <string>
#include <string_view>

#include "composition/service_config.hpp"
#include "composition/soap_router.hpp"

namespace sonarium::composition {

// Top-level facade exposing every static and dynamic UPnP MediaServer surface
// the future Atria HTTP route will need. Holds no socket state.
class DlnaServer {
public:
    DlnaServer(std::shared_ptr<SoapRouter> router, std::shared_ptr<ServiceConfig const> config);

    // GET /description.xml
    [[nodiscard]] std::string description_xml() const;

    // GET /ContentDirectory/scpd.xml
    [[nodiscard]] static std::string_view content_directory_scpd() noexcept;

    // GET /ConnectionManager/scpd.xml
    [[nodiscard]] static std::string_view connection_manager_scpd() noexcept;

    // POST /upnp/control/<service>: full SOAP pipeline.
    [[nodiscard]] std::string dispatch_soap(std::string_view body,
                                            std::string_view user_agent) const;

    [[nodiscard]] ServiceConfig const& config() const noexcept { return *config_; }

private:
    std::shared_ptr<SoapRouter> router_;
    std::shared_ptr<ServiceConfig const> config_;
};

} // namespace sonarium::composition

template <>
struct ctorwire::dependencies<sonarium::composition::DlnaServer> {
    using type = ctorwire::types<std::shared_ptr<sonarium::composition::SoapRouter>,
                                 std::shared_ptr<sonarium::composition::ServiceConfig const>>;
};

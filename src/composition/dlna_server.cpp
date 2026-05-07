#include "composition/dlna_server.hpp"

#include <utility>

#include "upnp/device_description.hpp"
#include "upnp/scpd_documents.hpp"

namespace sonarium::composition {

DlnaServer::DlnaServer(std::shared_ptr<SoapRouter> router,
                       std::shared_ptr<ServiceConfig const> config)
    : router_{std::move(router)}, config_{std::move(config)} {}

std::string DlnaServer::description_xml() const {
    return sonarium::upnp::build_device_description(config_->device, config_->service_paths);
}

std::string_view DlnaServer::content_directory_scpd() noexcept {
    return sonarium::upnp::content_directory_scpd_xml();
}

std::string_view DlnaServer::connection_manager_scpd() noexcept {
    return sonarium::upnp::connection_manager_scpd_xml();
}

std::string DlnaServer::dispatch_soap(std::string_view body, std::string_view user_agent) const {
    return router_->dispatch_body(body, user_agent);
}

} // namespace sonarium::composition

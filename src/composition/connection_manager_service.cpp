#include "composition/connection_manager_service.hpp"

#include <logspine/field.hpp>
#include <utility>

#include "dlna-core/connection_manager_handler.hpp"
#include "upnp/upnp_error.hpp"

namespace sonarium::composition {

namespace {

constexpr std::string_view component_name = "ConnectionManager";

} // namespace

ConnectionManagerService::ConnectionManagerService(
    std::shared_ptr<sonarium::dlna::DeviceProfileRegistry> profiles,
    std::shared_ptr<ServiceConfig const> config,
    std::shared_ptr<sonarium::core::Logger> logger)
    : profiles_{std::move(profiles)}, config_{std::move(config)}, logger_{std::move(logger)} {}

std::string ConnectionManagerService::dispatch(sonarium::upnp::ParsedSoapRequest const& request,
                                               std::string_view user_agent) const {
    using sonarium::upnp::build_soap_fault;
    using sonarium::upnp::build_soap_response;
    using sonarium::upnp::UpnpErrorCode;

    sonarium::dlna::RequestHeaders const headers{user_agent, {}};
    auto const& profile = profiles_->match(headers);

    if (request.action == "GetProtocolInfo") {
        auto const result = sonarium::dlna::handle_get_protocol_info(
            request, profile, config_->protocol_info_catalog);
        if (!result.has_value()) {
            logger_->warn("dlna.protocol_info.fault",
                          {::logspine::kv("component", std::string{component_name}),
                           ::logspine::kv("device_profile", profile.name)});
            return build_soap_fault(result.error());
        }
        logger_->info("dlna.protocol_info.ok",
                      {::logspine::kv("component", std::string{component_name}),
                       ::logspine::kv("soap_action", std::string{request.action}),
                       ::logspine::kv("device_profile", profile.name)});
        return build_soap_response(std::string(service_urn()), request.action, *result);
    }
    if (request.action == "GetCurrentConnectionIDs") {
        return build_soap_response(
            std::string(service_urn()), request.action, {{"ConnectionIDs", "0"}});
    }
    if (request.action == "GetCurrentConnectionInfo") {
        return build_soap_response(std::string(service_urn()),
                                   request.action,
                                   {{"RcsID", "-1"},
                                    {"AVTransportID", "-1"},
                                    {"ProtocolInfo", ""},
                                    {"PeerConnectionManager", ""},
                                    {"PeerConnectionID", "-1"},
                                    {"Direction", "Output"},
                                    {"Status", "OK"}});
    }

    logger_->warn("dlna.action.unknown",
                  {::logspine::kv("component", std::string{component_name}),
                   ::logspine::kv("soap_action", std::string{request.action})});
    return build_soap_fault(UpnpErrorCode::invalid_action);
}

} // namespace sonarium::composition

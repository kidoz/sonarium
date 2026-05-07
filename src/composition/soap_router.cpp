#include "composition/soap_router.hpp"

#include <logspine/field.hpp>
#include <utility>

#include "upnp/upnp_error.hpp"

namespace sonarium::composition {

namespace {

constexpr std::string_view component_name = "SoapRouter";

} // namespace

SoapRouter::SoapRouter(std::shared_ptr<ContentDirectoryService> content_directory,
                       std::shared_ptr<ConnectionManagerService> connection_manager,
                       std::shared_ptr<sonarium::core::Logger> logger)
    : content_directory_{std::move(content_directory)},
      connection_manager_{std::move(connection_manager)},
      logger_{std::move(logger)} {}

std::string SoapRouter::dispatch(sonarium::upnp::ParsedSoapRequest const& request,
                                 std::string_view user_agent) const {
    logger_->debug("soap.dispatch",
                   {::logspine::kv("component", std::string{component_name}),
                    ::logspine::kv("service_urn", request.service_urn),
                    ::logspine::kv("soap_action", request.action),
                    ::logspine::kv("user_agent", std::string{user_agent})});

    if (request.service_urn == ContentDirectoryService::service_urn()) {
        return content_directory_->dispatch(request, user_agent);
    }
    if (request.service_urn == ConnectionManagerService::service_urn()) {
        return connection_manager_->dispatch(request, user_agent);
    }

    logger_->warn("soap.unknown_service",
                  {::logspine::kv("component", std::string{component_name}),
                   ::logspine::kv("service_urn", request.service_urn),
                   ::logspine::kv("soap_action", request.action)});
    return sonarium::upnp::build_soap_fault(sonarium::upnp::UpnpErrorCode::invalid_action);
}

std::string SoapRouter::dispatch_body(std::string_view body, std::string_view user_agent) const {
    auto parsed = sonarium::upnp::parse_soap_request(body);
    if (!parsed) {
        logger_->warn(
            "soap.parse_failed",
            {::logspine::kv("component", std::string{component_name}),
             ::logspine::kv("user_agent", std::string{user_agent}),
             ::logspine::kv("upnp_error",
                            static_cast<std::int64_t>(static_cast<unsigned>(parsed.error())))});
        return sonarium::upnp::build_soap_fault(sonarium::upnp::UpnpErrorCode::invalid_args);
    }
    return dispatch(*parsed, user_agent);
}

} // namespace sonarium::composition

#include "composition/content_directory_service.hpp"

#include <logspine/field.hpp>
#include <utility>

#include "dlna-core/browse_handler.hpp"
#include "upnp/upnp_error.hpp"

namespace sonarium::composition {

namespace {

constexpr std::string_view component_name = "ContentDirectory";

} // namespace

ContentDirectoryService::ContentDirectoryService(
    std::shared_ptr<sonarium::catalog::Repository> catalog,
    std::shared_ptr<sonarium::dlna::DeviceProfileRegistry> profiles,
    std::shared_ptr<ServiceConfig const> config,
    std::shared_ptr<sonarium::core::Logger> logger)
    : catalog_{std::move(catalog)},
      profiles_{std::move(profiles)},
      config_{std::move(config)},
      logger_{std::move(logger)} {}

std::string ContentDirectoryService::dispatch(sonarium::upnp::ParsedSoapRequest const& request,
                                              std::string_view user_agent) const {
    using sonarium::upnp::build_soap_fault;
    using sonarium::upnp::build_soap_response;
    using sonarium::upnp::UpnpErrorCode;

    sonarium::dlna::RequestHeaders const headers{user_agent, {}};
    auto const& profile = profiles_->match(headers);

    if (request.action == "Browse") {
        sonarium::dlna::BrowseContext ctx;
        ctx.catalog = catalog_.get();
        ctx.profile = &profile;
        ctx.base_url = config_->base_url;
        ctx.token_signer = config_->media_token_signer.get();
        auto const object_id = request.arg_or("ObjectID", "");
        auto const result = sonarium::dlna::handle_browse(request, ctx);
        if (!result.has_value()) {
            logger_->warn(
                "dlna.browse.fault",
                {::logspine::kv("component", std::string{component_name}),
                 ::logspine::kv("soap_action", std::string{request.action}),
                 ::logspine::kv("object_id", object_id),
                 ::logspine::kv("device_profile", profile.name),
                 ::logspine::kv("upnp_error",
                                static_cast<std::int64_t>(static_cast<unsigned>(result.error())))});
            return build_soap_fault(result.error());
        }
        logger_->info(
            "dlna.browse.ok",
            {::logspine::kv("component", std::string{component_name}),
             ::logspine::kv("soap_action", std::string{request.action}),
             ::logspine::kv("object_id", object_id),
             ::logspine::kv("device_profile", profile.name),
             ::logspine::kv("number_returned", static_cast<std::int64_t>(result->number_returned)),
             ::logspine::kv("total_matches", static_cast<std::int64_t>(result->total_matches))});
        return build_soap_response(std::string(service_urn()),
                                   request.action,
                                   {{"Result", result->didl_lite},
                                    {"NumberReturned", std::to_string(result->number_returned)},
                                    {"TotalMatches", std::to_string(result->total_matches)},
                                    {"UpdateID", std::to_string(result->update_id)}});
    }

    if (request.action == "GetSearchCapabilities") {
        return build_soap_response(
            std::string(service_urn()), request.action, {{"SearchCaps", ""}});
    }
    if (request.action == "GetSortCapabilities") {
        return build_soap_response(std::string(service_urn()), request.action, {{"SortCaps", ""}});
    }
    if (request.action == "GetSystemUpdateID") {
        return build_soap_response(std::string(service_urn()),
                                   request.action,
                                   {{"Id", std::to_string(catalog_->system_update_id())}});
    }

    logger_->warn("dlna.action.unknown",
                  {::logspine::kv("component", std::string{component_name}),
                   ::logspine::kv("soap_action", std::string{request.action})});
    return build_soap_fault(UpnpErrorCode::invalid_action);
}

} // namespace sonarium::composition

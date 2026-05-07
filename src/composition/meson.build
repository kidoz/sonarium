composition_lib = static_library(
    'composition',
    files(
        'content_directory_service.cpp',
        'connection_manager_service.cpp',
        'soap_router.cpp',
        'dlna_server.cpp',
        'injector.cpp',
        'http_routes.cpp',
        'ssdp_service.cpp',
    ),
    include_directories : sonarium_inc,
    cpp_args : sonarium_common_args,
    dependencies : [
        sonarium_module_deps['core'],
        sonarium_module_deps['media'],
        sonarium_module_deps['catalog'],
        sonarium_module_deps['dlna-core'],
        sonarium_module_deps['upnp'],
        sonarium_external_deps['ctorwire'],
        sonarium_external_deps['atria'],
    ],
    install : false,
)

composition_dep = declare_dependency(
    link_with : composition_lib,
    include_directories : sonarium_inc,
    dependencies : [
        sonarium_module_deps['core'],
        sonarium_module_deps['media'],
        sonarium_module_deps['catalog'],
        sonarium_module_deps['dlna-core'],
        sonarium_module_deps['upnp'],
        sonarium_external_deps['ctorwire'],
        sonarium_external_deps['atria'],
    ],
)

sonarium_module_deps += {'composition': composition_dep}

sonarium_lint_sources += files(
    'service_config.hpp',
    'content_directory_service.hpp',
    'content_directory_service.cpp',
    'connection_manager_service.hpp',
    'connection_manager_service.cpp',
    'soap_router.hpp',
    'soap_router.cpp',
    'dlna_server.hpp',
    'dlna_server.cpp',
    'injector.hpp',
    'injector.cpp',
    'http_routes.hpp',
    'http_routes.cpp',
    'ssdp_service.hpp',
    'ssdp_service.cpp',
)

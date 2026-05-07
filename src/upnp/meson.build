upnp_lib = static_library(
    'upnp',
    files(
        'soap_action.cpp',
        'soap_envelope.cpp',
        'scpd_documents.cpp',
        'device_description.cpp',
        'ssdp_messages.cpp',
    ),
    include_directories : sonarium_inc,
    cpp_args : sonarium_common_args,
    dependencies : [sonarium_module_deps['dlna-core']],
    install : false,
)

upnp_dep = declare_dependency(
    link_with : upnp_lib,
    include_directories : sonarium_inc,
    dependencies : [sonarium_module_deps['dlna-core']],
)

sonarium_module_deps += {'upnp': upnp_dep}

sonarium_lint_sources += files(
    'upnp_error.hpp',
    'soap_action.hpp',
    'soap_action.cpp',
    'soap_envelope.hpp',
    'soap_envelope.cpp',
    'scpd_documents.hpp',
    'scpd_documents.cpp',
    'device_description.hpp',
    'device_description.cpp',
    'ssdp_messages.hpp',
    'ssdp_messages.cpp',
)

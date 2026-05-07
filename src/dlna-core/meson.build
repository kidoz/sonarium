dlna_core_lib = static_library(
    'dlna-core',
    files(
        'xml_escape.cpp',
        'dlna_object_id.cpp',
        'protocol_info.cpp',
        'didl_lite_builder.cpp',
        'device_profile.cpp',
        'media_resource_selector.cpp',
        'browse_handler.cpp',
        'connection_manager_handler.cpp',
    ),
    include_directories : sonarium_inc,
    cpp_args : sonarium_common_args,
    dependencies : [
        sonarium_module_deps['core'],
        sonarium_module_deps['media'],
        sonarium_module_deps['catalog'],
    ],
    install : false,
)

dlna_core_dep = declare_dependency(
    link_with : dlna_core_lib,
    include_directories : sonarium_inc,
    dependencies : [
        sonarium_module_deps['core'],
        sonarium_module_deps['media'],
        sonarium_module_deps['catalog'],
    ],
)

sonarium_module_deps += {'dlna-core': dlna_core_dep}

sonarium_lint_sources += files(
    'xml_escape.hpp',
    'xml_escape.cpp',
    'dlna_object_id.hpp',
    'dlna_object_id.cpp',
    'protocol_info.hpp',
    'protocol_info.cpp',
    'didl_lite_builder.hpp',
    'didl_lite_builder.cpp',
    'device_profile.hpp',
    'device_profile.cpp',
    'media_resource_selector.hpp',
    'media_resource_selector.cpp',
    'browse_handler.hpp',
    'browse_handler.cpp',
    'connection_manager_handler.hpp',
    'connection_manager_handler.cpp',
)

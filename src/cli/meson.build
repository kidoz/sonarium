cli_lib = static_library(
    'cli-lib',
    files(
        'http_client.cpp',
        'dlna_status.cpp',
    ),
    include_directories : sonarium_inc,
    cpp_args : sonarium_common_args,
    install : false,
)

cli_lib_dep = declare_dependency(
    link_with : cli_lib,
    include_directories : sonarium_inc,
)

sonarium_module_deps += {'cli': cli_lib_dep}

executable(
    'sonariumctl',
    files('main.cpp'),
    dependencies : [
        sonarium_module_deps['core'],
        sonarium_module_deps['catalog'],
        sonarium_module_deps['scanner'],
        sonarium_module_deps['transcode'],
        cli_lib_dep,
    ],
    cpp_args : sonarium_common_args,
    install : true,
)

sonarium_lint_sources += files(
    'http_client.hpp',
    'http_client.cpp',
    'dlna_status.hpp',
    'dlna_status.cpp',
    'main.cpp',
)

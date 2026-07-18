server_routes_lib = static_library(
    'server-routes',
    files('hls_routes.cpp'),
    include_directories : sonarium_inc,
    cpp_args : sonarium_common_args,
    dependencies : [
        sonarium_module_deps['core'],
        sonarium_module_deps['catalog'],
        sonarium_module_deps['hls'],
        atria_dep,
    ],
    install : false,
)

server_routes_dep = declare_dependency(
    link_with : server_routes_lib,
    include_directories : sonarium_inc,
    dependencies : [
        sonarium_module_deps['core'],
        sonarium_module_deps['catalog'],
        sonarium_module_deps['hls'],
        atria_dep,
    ],
)

sonarium_module_deps += {'server': server_routes_dep}

executable(
    'sonarium-server',
    files('main.cpp'),
    dependencies : [
        sonarium_module_deps['core'],
        sonarium_module_deps['catalog'],
        sonarium_module_deps['hls'],
        server_routes_dep,
        atria_dep,
    ],
    cpp_args : sonarium_common_args,
    link_args : sonarium_hardening_link_args,
    install : true,
)

sonarium_lint_sources += files('hls_routes.hpp', 'hls_routes.cpp', 'main.cpp')

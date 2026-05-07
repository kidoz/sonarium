executable(
    'sonarium-dlna',
    files('main.cpp'),
    dependencies : [
        sonarium_module_deps['core'],
        sonarium_module_deps['media'],
        sonarium_module_deps['catalog'],
        sonarium_module_deps['dlna-core'],
        sonarium_module_deps['upnp'],
        sonarium_module_deps['composition'],
    ],
    cpp_args : sonarium_common_args,
    install : true,
)

sonarium_lint_sources += files(
    'config.hpp',
    'main.cpp',
)

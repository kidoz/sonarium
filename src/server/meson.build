executable(
    'sonarium-server',
    files('main.cpp'),
    dependencies : [
        sonarium_module_deps['core'],
        sonarium_module_deps['catalog'],
        sonarium_module_deps['hls'],
        atria_dep,
    ],
    cpp_args : sonarium_common_args,
    install : true,
)

sonarium_lint_sources += files('main.cpp')

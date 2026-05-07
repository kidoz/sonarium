executable(
    'sonarium-server',
    files('main.cpp'),
    dependencies : [sonarium_module_deps['core']],
    cpp_args : sonarium_common_args,
    install : true,
)

sonarium_lint_sources += files('main.cpp')

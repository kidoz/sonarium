server_tests = executable(
    'server-tests',
    files('hls_routes_tests.cpp'),
    dependencies : [
        sonarium_module_deps['server'],
        sonarium_module_deps['catalog'],
        sonarium_module_deps['media'],
        atria_dep,
        catch2_with_main_dep,
    ],
    cpp_args : sonarium_common_args,
)

test(
    'server',
    server_tests,
    protocol : 'tap',
    args : ['--reporter', 'tap'],
    suite : 'server',
)

sonarium_lint_sources += files('hls_routes_tests.cpp')

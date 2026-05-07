composition_tests = executable(
    'composition-tests',
    files(
        'dlna_server_tests.cpp',
        'logging_tests.cpp',
        'http_routes_tests.cpp',
        'media_token_routes_tests.cpp',
        'ssdp_service_tests.cpp',
    ),
    dependencies : [
        sonarium_module_deps['composition'],
        catch2_with_main_dep,
    ],
    cpp_args : sonarium_common_args,
)

test(
    'composition',
    composition_tests,
    protocol : 'tap',
    args : ['--reporter', 'tap'],
    suite : 'composition',
)

sonarium_lint_sources += files(
    'dlna_server_tests.cpp',
    'logging_tests.cpp',
    'http_routes_tests.cpp',
    'media_token_routes_tests.cpp',
    'ssdp_service_tests.cpp',
)

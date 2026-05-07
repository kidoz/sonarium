cli_tests = executable(
    'cli-tests',
    files(
        'http_client_tests.cpp',
        'dlna_status_tests.cpp',
    ),
    dependencies : [
        sonarium_module_deps['cli'],
        catch2_with_main_dep,
    ],
    cpp_args : sonarium_common_args,
)

test(
    'cli',
    cli_tests,
    protocol : 'tap',
    args : ['--reporter', 'tap'],
    suite : 'cli',
)

sonarium_lint_sources += files(
    'http_client_tests.cpp',
    'dlna_status_tests.cpp',
)

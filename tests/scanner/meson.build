scanner_tests = executable(
    'scanner-tests',
    files(
        'path_layout_tests.cpp',
        'media_scanner_tests.cpp',
    ),
    dependencies : [
        sonarium_module_deps['scanner'],
        catch2_with_main_dep,
    ],
    cpp_args : sonarium_common_args,
)

test(
    'scanner',
    scanner_tests,
    protocol : 'tap',
    args : ['--reporter', 'tap'],
    suite : 'scanner',
)

sonarium_lint_sources += files(
    'path_layout_tests.cpp',
    'media_scanner_tests.cpp',
)

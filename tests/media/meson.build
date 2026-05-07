media_tests = executable(
    'media-tests',
    files(
        'mime_type_tests.cpp',
        'duration_format_tests.cpp',
    ),
    dependencies : [
        sonarium_module_deps['media'],
        catch2_with_main_dep,
    ],
    cpp_args : sonarium_common_args,
)

test(
    'media',
    media_tests,
    protocol : 'tap',
    args : ['--reporter', 'tap'],
    suite : 'media',
)

sonarium_lint_sources += files(
    'mime_type_tests.cpp',
    'duration_format_tests.cpp',
)

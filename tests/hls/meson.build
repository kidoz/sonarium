hls_tests = executable(
    'hls-tests',
    files(
        'playlist_builder_tests.cpp',
        'segmenter_tests.cpp',
    ),
    dependencies : [
        sonarium_module_deps['hls'],
        catch2_with_main_dep,
    ],
    cpp_args : sonarium_common_args,
)

test(
    'hls',
    hls_tests,
    protocol : 'tap',
    args : ['--reporter', 'tap'],
    suite : 'hls',
)

sonarium_lint_sources += files(
    'playlist_builder_tests.cpp',
    'segmenter_tests.cpp',
)

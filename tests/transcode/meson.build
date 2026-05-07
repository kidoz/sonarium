transcode_tests = executable(
    'transcode-tests',
    files(
        'ffmpeg_command_tests.cpp',
    ),
    dependencies : [
        sonarium_module_deps['transcode'],
        catch2_with_main_dep,
    ],
    cpp_args : sonarium_common_args,
)

test(
    'transcode',
    transcode_tests,
    protocol : 'tap',
    args : ['--reporter', 'tap'],
    suite : 'transcode',
)

sonarium_lint_sources += files(
    'ffmpeg_command_tests.cpp',
)

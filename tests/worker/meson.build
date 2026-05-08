worker_tests = executable(
    'worker-tests',
    files(
        'fs_watcher_tests.cpp',
    ),
    dependencies : [
        sonarium_module_deps['worker'],
        catch2_with_main_dep,
    ],
    cpp_args : sonarium_common_args,
)

test(
    'worker',
    worker_tests,
    protocol : 'tap',
    args : ['--reporter', 'tap'],
    suite : 'worker',
    timeout : 30,
)

sonarium_lint_sources += files(
    'fs_watcher_tests.cpp',
)

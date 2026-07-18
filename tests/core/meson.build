core_tests = executable(
    'core-tests',
    files(
        'version_tests.cpp',
        'external_deps_smoke_tests.cpp',
        'sha256_tests.cpp',
        'hmac_sha256_tests.cpp',
        'media_token_tests.cpp',
        'operator_mode_tests.cpp',
        'path_containment_tests.cpp',
    ),
    dependencies : [
        sonarium_module_deps['core'],
        catch2_with_main_dep,
    ],
    cpp_args : sonarium_common_args,
)

test(
    'core',
    core_tests,
    protocol : 'tap',
    args : ['--reporter', 'tap'],
    suite : 'core',
)

sonarium_lint_sources += files(
    'version_tests.cpp',
    'external_deps_smoke_tests.cpp',
    'sha256_tests.cpp',
    'hmac_sha256_tests.cpp',
    'media_token_tests.cpp',
    'operator_mode_tests.cpp',
    'path_containment_tests.cpp',
)

catalog_tests = executable(
    'catalog-tests',
    files(
        'in_memory_repository_tests.cpp',
        'postgres_repository_tests.cpp',
    ),
    dependencies : [
        sonarium_module_deps['catalog'],
        catch2_with_main_dep,
    ],
    cpp_args : sonarium_common_args,
)

test(
    'catalog',
    catalog_tests,
    protocol : 'tap',
    args : ['--reporter', 'tap'],
    suite : 'catalog',
)

sonarium_lint_sources += files(
    'in_memory_repository_tests.cpp',
    'postgres_repository_tests.cpp',
)

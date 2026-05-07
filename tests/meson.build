subdir('core')
subdir('media')
subdir('catalog')
subdir('scanner')
subdir('dlna-core')
subdir('upnp')
subdir('composition')
subdir('transcode')
subdir('cli')

# Top-level smoke test: confirms Catch2 wiring works without any sonarium dep.
sonarium_smoke_tests = executable(
    'sonarium-smoke-tests',
    files('smoke_tests.cpp'),
    dependencies : [catch2_with_main_dep],
    cpp_args : sonarium_common_args,
    install : false,
)

test(
    'sonarium-smoke',
    sonarium_smoke_tests,
    protocol : 'tap',
    args : ['--reporter', 'tap'],
    suite : 'smoke',
)

sonarium_lint_sources += files('smoke_tests.cpp')

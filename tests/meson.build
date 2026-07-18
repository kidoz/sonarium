subdir('core')
subdir('media')
subdir('catalog')
subdir('scanner')
subdir('dlna-core')
subdir('upnp')
subdir('composition')
subdir('transcode')
subdir('hls')
subdir('server')
subdir('worker')
subdir('cli')

# Top-level smoke test: in-process renderer flow across the whole composition
# graph (describe → browse → fetch media), no sockets.
sonarium_smoke_tests = executable(
    'sonarium-smoke-tests',
    files('smoke_tests.cpp'),
    dependencies : [
        sonarium_module_deps['composition'],
        sonarium_module_deps['catalog'],
        sonarium_module_deps['dlna-core'],
        sonarium_module_deps['media'],
        atria_dep,
        catch2_with_main_dep,
    ],
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

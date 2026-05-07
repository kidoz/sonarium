upnp_tests = executable(
    'upnp-tests',
    files(
        'soap_action_tests.cpp',
        'soap_envelope_tests.cpp',
        'scpd_documents_tests.cpp',
        'device_description_tests.cpp',
        'ssdp_messages_tests.cpp',
    ),
    dependencies : [
        sonarium_module_deps['upnp'],
        catch2_with_main_dep,
    ],
    cpp_args : sonarium_common_args,
)

test(
    'upnp',
    upnp_tests,
    protocol : 'tap',
    args : ['--reporter', 'tap'],
    suite : 'upnp',
)

sonarium_lint_sources += files(
    'soap_action_tests.cpp',
    'soap_envelope_tests.cpp',
    'scpd_documents_tests.cpp',
    'device_description_tests.cpp',
    'ssdp_messages_tests.cpp',
)

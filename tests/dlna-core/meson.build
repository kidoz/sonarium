dlna_core_tests = executable(
    'dlna-core-tests',
    files(
        'xml_escape_tests.cpp',
        'dlna_object_id_tests.cpp',
        'protocol_info_tests.cpp',
        'didl_lite_builder_tests.cpp',
        'device_profile_tests.cpp',
        'media_resource_selector_tests.cpp',
        'browse_handler_tests.cpp',
        'connection_manager_handler_tests.cpp',
    ),
    dependencies : [
        sonarium_module_deps['dlna-core'],
        catch2_with_main_dep,
    ],
    cpp_args : sonarium_common_args,
)

test(
    'dlna-core',
    dlna_core_tests,
    protocol : 'tap',
    args : ['--reporter', 'tap'],
    suite : 'dlna-core',
)

sonarium_lint_sources += files(
    'xml_escape_tests.cpp',
    'dlna_object_id_tests.cpp',
    'protocol_info_tests.cpp',
    'didl_lite_builder_tests.cpp',
    'device_profile_tests.cpp',
    'media_resource_selector_tests.cpp',
    'browse_handler_tests.cpp',
    'connection_manager_handler_tests.cpp',
)

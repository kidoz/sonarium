scanner_lib = static_library(
    'scanner',
    files(
        'path_layout.cpp',
        'media_scanner.cpp',
    ),
    include_directories : sonarium_inc,
    cpp_args : sonarium_common_args,
    dependencies : [
        sonarium_module_deps['media'],
        sonarium_module_deps['catalog'],
    ],
    install : false,
)

scanner_dep = declare_dependency(
    link_with : scanner_lib,
    include_directories : sonarium_inc,
    dependencies : [
        sonarium_module_deps['media'],
        sonarium_module_deps['catalog'],
    ],
)

sonarium_module_deps += {'scanner': scanner_dep}

sonarium_lint_sources += files(
    'path_layout.hpp',
    'path_layout.cpp',
    'media_scanner.hpp',
    'media_scanner.cpp',
)

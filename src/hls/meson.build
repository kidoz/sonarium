hls_lib = static_library(
    'hls',
    files(
        'playlist_builder.cpp',
    ),
    include_directories : sonarium_inc,
    cpp_args : sonarium_common_args,
    dependencies : [
        sonarium_module_deps['media'],
    ],
    install : false,
)

hls_dep = declare_dependency(
    link_with : hls_lib,
    include_directories : sonarium_inc,
    dependencies : [
        sonarium_module_deps['media'],
    ],
)

sonarium_module_deps += {'hls': hls_dep}

sonarium_lint_sources += files(
    'playlist_builder.hpp',
    'playlist_builder.cpp',
)

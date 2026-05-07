media_lib = static_library(
    'media',
    files(
        'mime_type.cpp',
        'duration_format.cpp',
    ),
    include_directories : sonarium_inc,
    cpp_args : sonarium_common_args,
    install : false,
)

media_dep = declare_dependency(
    link_with : media_lib,
    include_directories : sonarium_inc,
)

sonarium_module_deps += {'media': media_dep}

sonarium_lint_sources += files(
    'mime_type.hpp',
    'mime_type.cpp',
    'duration_format.hpp',
    'duration_format.cpp',
    'media_rendition.hpp',
)

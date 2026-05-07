transcode_lib = static_library(
    'transcode',
    files(
        'transcode_request.cpp',
        'ffmpeg_command.cpp',
        'ffmpeg_runner.cpp',
    ),
    include_directories : sonarium_inc,
    cpp_args : sonarium_common_args,
    dependencies : [
        sonarium_module_deps['media'],
    ],
    install : false,
)

transcode_dep = declare_dependency(
    link_with : transcode_lib,
    include_directories : sonarium_inc,
    dependencies : [
        sonarium_module_deps['media'],
    ],
)

sonarium_module_deps += {'transcode': transcode_dep}

sonarium_lint_sources += files(
    'transcode_request.hpp',
    'transcode_request.cpp',
    'ffmpeg_command.hpp',
    'ffmpeg_command.cpp',
    'ffmpeg_runner.hpp',
    'ffmpeg_runner.cpp',
)

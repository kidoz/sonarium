core_lib = static_library(
    'core',
    files(
        'version.cpp',
        'external_deps_smoke.cpp',
        'logspine_logger.cpp',
        'sha256.cpp',
        'hmac_sha256.cpp',
        'media_token.cpp',
        'operator_mode.cpp',
        'path_containment.cpp',
    ),
    include_directories : sonarium_inc,
    cpp_args : sonarium_common_args,
    dependencies : [
        sonarium_external_deps['atria'],
        sonarium_external_deps['ctorwire'],
        sonarium_external_deps['logspine'],
        sonarium_external_deps['asterorm'],
    ],
    install : false,
)

core_dep = declare_dependency(
    link_with : core_lib,
    include_directories : sonarium_inc,
    dependencies : [
        sonarium_external_deps['atria'],
        sonarium_external_deps['ctorwire'],
        sonarium_external_deps['logspine'],
        sonarium_external_deps['asterorm'],
    ],
)

sonarium_module_deps += {'core': core_dep}

sonarium_lint_sources += files(
    'version.hpp',
    'version.cpp',
    'external_deps_smoke.hpp',
    'external_deps_smoke.cpp',
    'logger.hpp',
    'null_logger.hpp',
    'logspine_logger.hpp',
    'logspine_logger.cpp',
    'constant_time.hpp',
    'sha256.hpp',
    'sha256.cpp',
    'hmac_sha256.hpp',
    'hmac_sha256.cpp',
    'media_token.hpp',
    'media_token.cpp',
    'operator_mode.hpp',
    'operator_mode.cpp',
    'path_containment.hpp',
    'path_containment.cpp',
)

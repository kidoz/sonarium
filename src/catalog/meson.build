catalog_lib = static_library(
    'catalog',
    files(
        'in_memory_repository.cpp',
        'postgres_repository.cpp',
    ),
    include_directories : sonarium_inc,
    cpp_args : sonarium_common_args,
    dependencies : [
        sonarium_module_deps['media'],
        sonarium_external_deps['asterorm-pg'],
    ],
    install : false,
)

catalog_dep = declare_dependency(
    link_with : catalog_lib,
    include_directories : sonarium_inc,
    dependencies : [
        sonarium_module_deps['media'],
        sonarium_external_deps['asterorm-pg'],
    ],
)

sonarium_module_deps += {'catalog': catalog_dep}

sonarium_lint_sources += files(
    'artist.hpp',
    'album.hpp',
    'track.hpp',
    'playlist.hpp',
    'storage_asset.hpp',
    'repository.hpp',
    'catalog_writer.hpp',
    'in_memory_repository.hpp',
    'in_memory_repository.cpp',
    'postgres_schema.hpp',
    'postgres_repository.hpp',
    'postgres_repository.cpp',
)

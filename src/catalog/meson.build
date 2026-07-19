# SQLite: prefer pkg-config (Linux distros), fall back to the platform
# library + header (macOS SDK ships libsqlite3 without a .pc file).
sqlite_dep = dependency('sqlite3', required : false)
if not sqlite_dep.found()
    sqlite_dep = cxx.find_library('sqlite3', has_headers : ['sqlite3.h'])
endif

catalog_lib = static_library(
    'catalog',
    files(
        'in_memory_repository.cpp',
        'postgres_repository.cpp',
        'sqlite_repository.cpp',
        'catalog_factory.cpp',
    ),
    include_directories : sonarium_inc,
    cpp_args : sonarium_common_args,
    dependencies : [
        sonarium_module_deps['media'],
        sonarium_external_deps['asterorm-pg'],
        sqlite_dep,
    ],
    install : false,
)

catalog_dep = declare_dependency(
    link_with : catalog_lib,
    include_directories : sonarium_inc,
    dependencies : [
        sonarium_module_deps['media'],
        sonarium_external_deps['asterorm-pg'],
        sqlite_dep,
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
    'sqlite_schema.hpp',
    'sqlite_repository.hpp',
    'sqlite_repository.cpp',
    'catalog_factory.hpp',
    'catalog_factory.cpp',
)

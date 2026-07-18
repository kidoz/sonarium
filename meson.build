project(
    'sonarium',
    'cpp',
    version : '0.1.0',
    license : 'MIT',
    default_options : [
        'warning_level=3',
        'cpp_std=c++23',
        'werror=false',
        'b_ndebug=if-release',
    ],
    meson_version : '>=1.3.0',
)

cxx = meson.get_compiler('cpp')

# Project-wide include root — all sources live flat under src/<module>/...
sonarium_inc = include_directories('src')

sonarium_common_args = cxx.get_supported_arguments([
    '-Wconversion',
    '-Wshadow',
    '-Wnon-virtual-dtor',
    '-Wold-style-cast',
    '-Wcast-align',
    '-Wunused',
    '-Woverloaded-virtual',
    '-Wnull-dereference',
    '-Wdouble-promotion',
    '-Wformat=2',
    '-Wimplicit-fallthrough',
])

catch2_with_main_dep = dependency(
    'catch2-with-main',
    fallback : ['catch2', 'catch2_with_main_dep'],
    default_options : ['tests=false'],
    required : true,
)

# ---------------------------------------------------------------------------
# kidoz/* dependencies pulled via Meson wraps under subprojects/.
#
#   atria      — networking framework (HTTP, UDP/multicast, file responses)
#   ctorwire   — header-only constructor-first dependency injection
#   logspine   — structured logger
#
# These are declared up-front so all sonarium modules can opt in by listing
# them in their dependencies. Wrap default options keep tests/examples off.
# ---------------------------------------------------------------------------

# Order matters: instantiate leaf deps (ctorwire, logspine) first so their default
# options stick before atria pulls them in transitively.
ctorwire_dep = dependency(
    'ctorwire',
    fallback : ['ctorwire', 'ctorwire_dep'],
    default_options : [
        'werror=false',
        'build_tests=false',
        'build_examples=false',
        'enable_format_check=false',
        'enable_clang_tidy=false',
    ],
    required : true,
)

logspine_dep = dependency(
    'logspine',
    fallback : ['logspine', 'logspine_dep'],
    default_options : [
        'werror=false',
        'build_tests=false',
        'build_examples=false',
        'build_benchmarks=false',
    ],
    required : true,
)

atria_dep = dependency(
    'atria',
    fallback : ['atria', 'atria_dep'],
    default_options : ['werror=false'],
    required : true,
)

# AsterORM — postgres-first ORM. `asterorm` exposes the core lib (no libpq);
# `asterorm-pg` exposes the postgres adapter and requires libpq on the build
# host. The catalog module's PostgresRepository depends on `asterorm-pg`.
asterorm_dep = dependency(
    'asterorm',
    fallback : ['asterorm', 'asterorm_core_dep'],
    default_options : [
        'tests=false',
        'examples=false',
        'clickhouse=disabled',
        'postgres=enabled',
    ],
    required : true,
)

asterorm_pg_dep = dependency(
    'asterorm-pg',
    fallback : ['asterorm', 'asterorm_pg_dep'],
    default_options : [
        'tests=false',
        'examples=false',
        'clickhouse=disabled',
        'postgres=enabled',
    ],
    required : true,
)

sonarium_external_deps = {
    'atria'       : atria_dep,
    'ctorwire'    : ctorwire_dep,
    'logspine'    : logspine_dep,
    'asterorm'    : asterorm_dep,
    'asterorm-pg' : asterorm_pg_dep,
}

# Module dependency registry. Each src/<module>/meson.build assigns into this.
sonarium_module_deps = {}

# Lint source list. Each module appends its files via `sonarium_lint_sources += ...`.
sonarium_lint_sources = []

subdir('src')
subdir('tests')

# ---------------------------------------------------------------------------
# Code-quality run targets.
# ---------------------------------------------------------------------------

clang_format = find_program('clang-format', required : false)
if clang_format.found()
    # Names prefixed with `sonarium-` so they don't collide with subproject
    # run_targets that also define `clang-format` (e.g. logspine).
    run_target(
        'sonarium-clang-format',
        command : [clang_format, '-i', '--style=file', sonarium_lint_sources],
    )
    run_target(
        'sonarium-clang-format-check',
        command : [
            clang_format,
            '--dry-run',
            '--Werror',
            '--style=file',
            sonarium_lint_sources,
        ],
    )
endif

clang_tidy = find_program('clang-tidy', required : false)
if clang_tidy.found()
    # Header-only files don't have entries in compile_commands.json. clang-tidy
    # picks a fallback command from a similar TU, but the include directories
    # don't always survive. Pass `-I<src>` (and the macOS SDK isysroot) as an
    # extra-arg so headers like `repository.hpp` can resolve `catalog/...`.
    tidy_extra_args = [
        '--extra-arg=-I' + meson.project_source_root() / 'src',
    ]
    if host_machine.system() == 'darwin'
        xcrun = find_program('xcrun', required : false)
        if xcrun.found()
            sdk_path_result = run_command(xcrun, '--show-sdk-path', check : false)
            if sdk_path_result.returncode() == 0
                tidy_extra_args += [
                    '--extra-arg=-isysroot',
                    '--extra-arg=' + sdk_path_result.stdout().strip(),
                ]
            endif
        endif
    endif

    run_target(
        'sonarium-clang-tidy',
        command : [
            clang_tidy,
            '-p', meson.project_build_root(),
            tidy_extra_args,
            sonarium_lint_sources,
        ],
    )
endif

summary(
    {
        'C++ standard'  : 'c++23',
        'Build type'    : get_option('buildtype'),
        'Catch2'        : catch2_with_main_dep.found() ? 'yes' : 'no',
        'atria'         : atria_dep.found() ? 'yes' : 'no',
        'ctorwire'      : ctorwire_dep.found() ? 'yes' : 'no',
        'logspine'      : logspine_dep.found() ? 'yes' : 'no',
        'asterorm'      : asterorm_dep.found() ? 'yes' : 'no',
        'asterorm-pg'   : asterorm_pg_dep.found() ? 'yes' : 'no',
        'clang-format'  : clang_format.found() ? 'yes' : 'no',
        'clang-tidy'    : clang_tidy.found() ? 'yes' : 'no',
    },
    section : 'Sonarium',
)

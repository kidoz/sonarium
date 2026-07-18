# FS-watcher backend: pick the platform-specific implementation. The polling
# fallback is always built so the worker has a runtime fallback when the
# native backend can't be opened (e.g. in a container without inotify).
worker_fs_sources = files('fs_watcher_polling.cpp')
worker_fs_deps = []
if host_machine.system() == 'linux'
    worker_fs_sources += files('fs_watcher_inotify.cpp')
elif host_machine.system() == 'darwin'
    worker_fs_sources += files('fs_watcher_fsevents.cpp')
    worker_fs_deps += dependency(
        'appleframeworks',
        modules : ['CoreServices', 'CoreFoundation'],
    )
else
    worker_fs_sources += files('fs_watcher_unsupported.cpp')
endif

worker_lib = static_library(
    'worker',
    worker_fs_sources,
    include_directories : sonarium_inc,
    cpp_args : sonarium_common_args,
    dependencies : worker_fs_deps,
    install : false,
)

worker_lib_dep = declare_dependency(
    link_with : worker_lib,
    include_directories : sonarium_inc,
    dependencies : worker_fs_deps,
)

sonarium_module_deps += {'worker': worker_lib_dep}

executable(
    'sonarium-worker',
    files('main.cpp'),
    dependencies : [
        sonarium_module_deps['core'],
        sonarium_module_deps['media'],
        sonarium_module_deps['catalog'],
        sonarium_module_deps['scanner'],
        worker_lib_dep,
    ],
    cpp_args : sonarium_common_args,
    link_args : sonarium_hardening_link_args,
    install : true,
)

sonarium_lint_sources += files(
    'fs_watcher.hpp',
    'fs_watcher_polling.cpp',
    'main.cpp',
)
if host_machine.system() == 'linux'
    sonarium_lint_sources += files('fs_watcher_inotify.cpp')
elif host_machine.system() == 'darwin'
    sonarium_lint_sources += files('fs_watcher_fsevents.cpp')
else
    sonarium_lint_sources += files('fs_watcher_unsupported.cpp')
endif

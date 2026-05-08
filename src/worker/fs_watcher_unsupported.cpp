#include "worker/fs_watcher.hpp"

namespace sonarium::worker {

std::expected<std::unique_ptr<FsWatcher>, std::string>
make_native_fs_watcher(std::filesystem::path const& /*root*/) {
    return std::unexpected("native fs watcher not available on this platform");
}

} // namespace sonarium::worker

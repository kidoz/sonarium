#pragma once

#include <filesystem>

namespace sonarium::core {

// True when `candidate` resolves to a location inside `root`. Both paths are
// canonicalized (weakly_canonical follows symlinks on the existing prefix), so
// a symlink under the media tree pointing at /etc cannot smuggle files out of
// the root, and neither can a poisoned catalog row holding an absolute path.
//
// An empty root disables containment and returns true — the dev-mode default.
// Production startup invariants require a configured root.
[[nodiscard]] bool path_within_root(std::filesystem::path const& candidate,
                                    std::filesystem::path const& root);

} // namespace sonarium::core

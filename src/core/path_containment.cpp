#include "core/path_containment.hpp"

#include <system_error>

namespace sonarium::core {

bool path_within_root(std::filesystem::path const& candidate, std::filesystem::path const& root) {
    if (root.empty()) {
        return true;
    }

    std::error_code ec;
    auto canon_root = std::filesystem::weakly_canonical(root, ec);
    if (ec || canon_root.empty()) {
        return false; // unresolvable root fails closed
    }
    // A trailing separator leaves an empty final element that would defeat the
    // component-wise prefix walk below.
    if (canon_root.filename().empty()) {
        canon_root = canon_root.parent_path();
    }

    auto const canon_candidate = std::filesystem::weakly_canonical(candidate, ec);
    if (ec) {
        return false;
    }

    auto root_it = canon_root.begin();
    auto cand_it = canon_candidate.begin();
    for (; root_it != canon_root.end(); ++root_it, ++cand_it) {
        if (cand_it == canon_candidate.end() || *cand_it != *root_it) {
            return false;
        }
    }
    return true;
}

} // namespace sonarium::core

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/path_containment.hpp"

using sonarium::core::path_within_root;

namespace {

// Fresh scratch tree per test, removed on destruction.
struct ScratchTree {
    std::filesystem::path root;

    ScratchTree() {
        root = std::filesystem::temp_directory_path() / "sonarium-containment-test";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "library" / "artist");
        std::ofstream{root / "library" / "artist" / "track.mp3"} << "audio";
        std::ofstream{root / "outside.txt"} << "secret";
    }

    ScratchTree(ScratchTree const&) = delete;
    ScratchTree& operator=(ScratchTree const&) = delete;
    ScratchTree(ScratchTree&&) = delete;
    ScratchTree& operator=(ScratchTree&&) = delete;

    ~ScratchTree() { std::filesystem::remove_all(root); }
};

} // namespace

TEST_CASE("empty root disables containment", "[core][path_containment]") {
    REQUIRE(path_within_root("/etc/passwd", {}));
}

TEST_CASE("path inside the root is contained", "[core][path_containment]") {
    ScratchTree const t;
    REQUIRE(path_within_root(t.root / "library" / "artist" / "track.mp3", t.root / "library"));
}

TEST_CASE("path outside the root is rejected", "[core][path_containment]") {
    ScratchTree const t;
    REQUIRE_FALSE(path_within_root(t.root / "outside.txt", t.root / "library"));
    REQUIRE_FALSE(path_within_root("/etc/passwd", t.root / "library"));
}

TEST_CASE("dot-dot traversal cannot escape the root", "[core][path_containment]") {
    ScratchTree const t;
    REQUIRE_FALSE(path_within_root(t.root / "library" / "artist" / ".." / ".." / "outside.txt",
                                   t.root / "library"));
}

TEST_CASE("sibling directory sharing a name prefix is rejected", "[core][path_containment]") {
    ScratchTree const t;
    std::filesystem::create_directories(t.root / "library-evil");
    std::ofstream{t.root / "library-evil" / "f.mp3"} << "x";
    REQUIRE_FALSE(path_within_root(t.root / "library-evil" / "f.mp3", t.root / "library"));
}

TEST_CASE("trailing slash on the root does not break containment", "[core][path_containment]") {
    ScratchTree const t;
    auto const root_with_slash = std::filesystem::path{(t.root / "library").string() + "/"};
    REQUIRE(path_within_root(t.root / "library" / "artist" / "track.mp3", root_with_slash));
    REQUIRE_FALSE(path_within_root(t.root / "outside.txt", root_with_slash));
}

TEST_CASE("symlink pointing outside the root is rejected", "[core][path_containment]") {
    ScratchTree const t;
    std::error_code ec;
    std::filesystem::create_symlink(
        t.root / "outside.txt", t.root / "library" / "artist" / "link.mp3", ec);
    if (ec) {
        SUCCEED("symlinks unsupported on this filesystem — skipping");
        return;
    }
    REQUIRE_FALSE(path_within_root(t.root / "library" / "artist" / "link.mp3", t.root / "library"));
}

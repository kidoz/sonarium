#include "dlna-core/dlna_object_id.hpp"

#include <string>
#include <string_view>

namespace sonarium::dlna {

namespace {

constexpr std::string_view root_id = "0";
constexpr std::string_view music_id = "1";
constexpr std::string_view artists_id = "artists";
constexpr std::string_view artist_prefix = "artist:";
constexpr std::string_view albums_suffix = ":albums";
constexpr std::string_view album_prefix = "album:";
constexpr std::string_view tracks_id = "tracks";
constexpr std::string_view track_prefix = "track:";
constexpr std::string_view playlists_id = "playlists";
constexpr std::string_view playlist_prefix = "playlist:";

} // namespace

std::string make_root_id() {
    return std::string(root_id);
}
std::string make_music_id() {
    return std::string(music_id);
}
std::string make_artists_id() {
    return std::string(artists_id);
}
std::string make_artist_id(std::string_view artist) {
    return std::string(artist_prefix) + std::string(artist);
}
std::string make_artist_albums_id(std::string_view artist) {
    return make_artist_id(artist) + std::string(albums_suffix);
}
std::string make_album_id(std::string_view album) {
    return std::string(album_prefix) + std::string(album);
}
std::string make_tracks_id() {
    return std::string(tracks_id);
}
std::string make_track_id(std::string_view track) {
    return std::string(track_prefix) + std::string(track);
}
std::string make_playlists_id() {
    return std::string(playlists_id);
}
std::string make_playlist_id(std::string_view playlist) {
    return std::string(playlist_prefix) + std::string(playlist);
}

std::expected<ObjectId, ObjectIdParseError> parse_object_id(std::string_view id) {
    if (id.empty()) {
        return std::unexpected(ObjectIdParseError::empty);
    }
    if (id == root_id) {
        return ObjectId{ObjectIdKind::root, {}};
    }
    if (id == music_id) {
        return ObjectId{ObjectIdKind::music, {}};
    }
    if (id == artists_id) {
        return ObjectId{ObjectIdKind::artists, {}};
    }
    if (id == tracks_id) {
        return ObjectId{ObjectIdKind::tracks, {}};
    }
    if (id == playlists_id) {
        return ObjectId{ObjectIdKind::playlists, {}};
    }

    if (id.starts_with(artist_prefix)) {
        auto rest = id.substr(artist_prefix.size());
        if (rest.ends_with(albums_suffix)) {
            auto entity = rest.substr(0, rest.size() - albums_suffix.size());
            if (entity.empty()) {
                return std::unexpected(ObjectIdParseError::invalid_format);
            }
            return ObjectId{ObjectIdKind::artist_albums, std::string(entity)};
        }
        if (rest.empty()) {
            return std::unexpected(ObjectIdParseError::invalid_format);
        }
        return ObjectId{ObjectIdKind::artist, std::string(rest)};
    }
    if (id.starts_with(album_prefix)) {
        auto rest = id.substr(album_prefix.size());
        if (rest.empty()) {
            return std::unexpected(ObjectIdParseError::invalid_format);
        }
        return ObjectId{ObjectIdKind::album, std::string(rest)};
    }
    if (id.starts_with(track_prefix)) {
        auto rest = id.substr(track_prefix.size());
        if (rest.empty()) {
            return std::unexpected(ObjectIdParseError::invalid_format);
        }
        return ObjectId{ObjectIdKind::track, std::string(rest)};
    }
    if (id.starts_with(playlist_prefix)) {
        auto rest = id.substr(playlist_prefix.size());
        if (rest.empty()) {
            return std::unexpected(ObjectIdParseError::invalid_format);
        }
        return ObjectId{ObjectIdKind::playlist, std::string(rest)};
    }

    return std::unexpected(ObjectIdParseError::unknown_kind);
}

} // namespace sonarium::dlna

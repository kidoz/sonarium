#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace sonarium::dlna {

// Stable, hierarchical DLNA ContentDirectory object IDs.
//
// Examples:
//   "0"                         root
//   "1"                         music container
//   "artists"                   artists container
//   "artist:7"                  one artist
//   "artist:7:albums"           albums of an artist
//   "album:42"                  one album
//   "track:99"                  one track

enum class ObjectIdKind : std::uint8_t {
    root,
    music,
    artists,
    artist,
    artist_albums,
    album,
    tracks,
    playlists,
    playlist,
    track,
    unknown,
};

struct ObjectId {
    ObjectIdKind kind = ObjectIdKind::unknown;
    std::string entity_id; // numeric or string id; empty for top-level kinds
};

enum class ObjectIdParseError : std::uint8_t {
    empty,
    invalid_format,
    unknown_kind,
};

[[nodiscard]] std::string make_root_id();
[[nodiscard]] std::string make_music_id();
[[nodiscard]] std::string make_artists_id();
[[nodiscard]] std::string make_artist_id(std::string_view artist);
[[nodiscard]] std::string make_artist_albums_id(std::string_view artist);
[[nodiscard]] std::string make_album_id(std::string_view album);
[[nodiscard]] std::string make_tracks_id();
[[nodiscard]] std::string make_track_id(std::string_view track);
[[nodiscard]] std::string make_playlists_id();
[[nodiscard]] std::string make_playlist_id(std::string_view playlist);

[[nodiscard]] std::expected<ObjectId, ObjectIdParseError> parse_object_id(std::string_view id);

} // namespace sonarium::dlna

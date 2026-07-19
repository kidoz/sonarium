#include "scanner/media_scanner.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>

#include "catalog/album.hpp"
#include "catalog/artist.hpp"
#include "catalog/storage_asset.hpp"
#include "catalog/track.hpp"
#include "media/media_rendition.hpp"
#include "media/mime_type.hpp"
#include "scanner/audio_metadata.hpp"
#include "scanner/path_layout.hpp"

namespace sonarium::scanner {

namespace {

namespace fs = std::filesystem;
using sonarium::catalog::Album;
using sonarium::catalog::Artist;
using sonarium::catalog::CatalogWriter;
using sonarium::catalog::StorageAsset;
using sonarium::catalog::Track;
using sonarium::media::AudioCodec;
using sonarium::media::AudioContainer;
using sonarium::media::MediaRendition;

struct ExtensionInfo {
    std::string_view ext;
    AudioCodec codec;
    AudioContainer container;
};

constexpr std::array<ExtensionInfo, 4> known_audio_exts{{
    {"mp3", AudioCodec::mp3, AudioContainer::mp3},
    {"flac", AudioCodec::flac, AudioContainer::flac},
    {"m4a", AudioCodec::aac_lc, AudioContainer::mp4},
    {"wav", AudioCodec::pcm_wav, AudioContainer::wav},
}};

[[nodiscard]] std::optional<ExtensionInfo> lookup_audio_ext(std::string_view ext) {
    for (auto const& row : known_audio_exts) {
        if (row.ext == ext) {
            return row;
        }
    }
    return std::nullopt;
}

// Album-art filenames we recognise — checked in order, lowercase comparison.
// First match wins.
[[nodiscard]] std::optional<fs::path> find_cover_art(fs::path const& album_dir) {
    static constexpr std::array<std::string_view, 6> candidates{
        "cover.jpg", "cover.jpeg", "cover.png", "folder.jpg", "folder.jpeg", "folder.png"};
    std::error_code ec;
    for (auto const& name : candidates) {
        auto const candidate = album_dir / std::string{name};
        if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
            return candidate;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::string protocol_info_for(std::string_view mime) {
    return std::string{"http-get:*:"} + std::string{mime} + ":*";
}

[[nodiscard]] std::uint64_t file_size_or_zero(fs::path const& path) {
    std::error_code ec;
    auto const size = fs::file_size(path, ec);
    if (ec) {
        return 0;
    }
    return size;
}

[[nodiscard]] std::string album_id_part(std::string_view artist_slug, std::string_view album_slug) {
    return std::string{artist_slug} + ":" + std::string{album_slug};
}

[[nodiscard]] std::string track_id_part(std::string_view album_part,
                                        std::string_view track_slug,
                                        std::optional<std::uint16_t> track_number) {
    std::string out{album_part};
    out.push_back(':');
    if (track_number.has_value()) {
        if (*track_number < 10) {
            out.push_back('0');
        }
        out += std::to_string(*track_number);
        out.push_back('-');
    }
    out += track_slug;
    return out;
}

} // namespace

ScanReport scan(fs::path const& root, CatalogWriter& writer) {
    ScanReport report;
    std::error_code ec;

    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
        report.errors.emplace_back("scan root does not exist or is not a directory: "
                                   + root.string());
        return report;
    }

    std::unordered_set<std::string> seen_artists;
    std::unordered_set<std::string> seen_albums;
    std::unordered_set<std::string> seen_covers;

    auto const record_error =
        [&](std::string_view label, fs::path const& path, std::string_view detail) {
            report.errors.emplace_back(std::string{label} + " " + path.string() + ": "
                                       + std::string{detail});
        };

    fs::recursive_directory_iterator it{root, fs::directory_options::skip_permission_denied, ec};
    fs::recursive_directory_iterator const end;
    if (ec) {
        report.errors.emplace_back("failed to open scan root: " + ec.message());
        return report;
    }

    for (; it != end; it.increment(ec)) {
        if (ec) {
            report.errors.emplace_back("scan iteration error: " + ec.message());
            ec.clear();
            continue;
        }
        if (!it->is_regular_file(ec)) {
            continue;
        }

        auto const& path = it->path();
        auto relative = fs::relative(path, root, ec);
        if (ec) {
            ec.clear();
            ++report.skipped_files;
            continue;
        }

        auto parsed = parse_track_path(relative);
        if (!parsed.has_value()) {
            ++report.skipped_files;
            continue;
        }

        auto const ext_info = lookup_audio_ext(parsed->extension);
        if (!ext_info.has_value()) {
            ++report.skipped_files;
            continue;
        }

        auto const artist_slug = slug(parsed->artist_name);
        auto const album_slug = slug(parsed->album_name);
        auto const track_slug = slug(parsed->track_title);
        if (artist_slug.empty() || album_slug.empty() || track_slug.empty()) {
            ++report.skipped_files;
            continue;
        }

        // IDs are stored *bare* (no `artist:` / `album:` etc. prefix) so they
        // match what `dlna::parse_object_id` strips off before calling into
        // the Repository. The DLNA layer re-adds the prefix on the way out.
        auto const artist_id = artist_slug;
        auto const album_id = album_id_part(artist_slug, album_slug);
        auto const track_id = track_id_part(album_id, track_slug, parsed->track_number);
        auto const rendition_id = track_id + ":" + parsed->extension;
        auto const cover_asset_id_str = "cover:" + album_id;

        // Cover art (one per album dir). Look up once and cache by album_id.
        std::optional<std::string> cover_asset_id;
        if (!seen_albums.contains(album_id)) {
            if (auto cover_path = find_cover_art(path.parent_path()); cover_path.has_value()) {
                auto const cover_ext = cover_path->extension().string();
                auto cover_mime = sonarium::media::mime_from_extension(cover_ext);
                auto const asset_id = cover_asset_id_str;
                if (!seen_covers.contains(asset_id)) {
                    StorageAsset asset;
                    asset.id = asset_id;
                    asset.storage_path = cover_path->string();
                    asset.mime_type =
                        cover_mime.has_value() ? std::string{*cover_mime} : "image/jpeg";
                    if (auto r = writer.upsert_asset(asset); r.has_value()) {
                        ++report.covers_upserted;
                        seen_covers.insert(asset_id);
                        cover_asset_id = asset_id;
                    } else {
                        record_error("upsert_asset", *cover_path, r.error());
                    }
                } else {
                    cover_asset_id = asset_id;
                }
            }
        }

        // ffprobe pulls duration / bitrate / sample-rate / channel info plus
        // optional artist/album/title tags. On failure (binary missing,
        // unrecognised file, fake bytes) we fall back to zeros and to the
        // path-derived display names.
        //
        // Tags fill the *display* fields only — IDs stay slug-derived from
        // the path so re-scans remain idempotent. First track to claim an
        // artist/album row wins (via the seen_* dedupe); subsequent files
        // don't churn the row even if their tags disagree.
        AudioMetadata audio{};
        if (auto md = read_audio_metadata(path); md.has_value()) {
            audio = *md;
        }

        auto const display_artist = audio.artist.has_value() ? *audio.artist : parsed->artist_name;
        auto const display_album = audio.album.has_value() ? *audio.album : parsed->album_name;
        auto const display_title = audio.title.has_value() ? *audio.title : parsed->track_title;

        if (!seen_artists.contains(artist_id)) {
            Artist artist;
            artist.id = artist_id;
            artist.name = display_artist;
            artist.sort_name = artist_slug;
            if (auto r = writer.upsert_artist(artist); r.has_value()) {
                ++report.artists_upserted;
                seen_artists.insert(artist_id);
            } else {
                record_error("upsert_artist", path, r.error());
                continue;
            }
        }

        if (!seen_albums.contains(album_id)) {
            Album album;
            album.id = album_id;
            album.artist_id = artist_id;
            album.title = display_album;
            album.sort_title = album_slug;
            album.cover_art_asset_id = cover_asset_id;
            if (auto r = writer.upsert_album(album); r.has_value()) {
                ++report.albums_upserted;
                seen_albums.insert(album_id);
            } else {
                record_error("upsert_album", path, r.error());
                continue;
            }
        }

        Track track;
        track.id = track_id;
        track.album_id = album_id;
        track.artist_id = artist_id;
        track.title = display_title;
        track.sort_title = track_slug;
        track.track_number = parsed->track_number;
        track.duration_ms = audio.duration_ms;
        if (auto r = writer.upsert_track(track); r.has_value()) {
            ++report.tracks_upserted;
        } else {
            record_error("upsert_track", path, r.error());
            continue;
        }

        sonarium::media::RenditionMime const mime_key{ext_info->codec, ext_info->container};
        auto const mime = sonarium::media::default_mime_for(mime_key);
        auto const dlna_pn = sonarium::media::dlna_org_pn_for(mime_key);
        auto const protocol_info = protocol_info_for(mime);

        MediaRendition rendition;
        rendition.id = rendition_id;
        rendition.track_id = track_id;
        rendition.codec = ext_info->codec;
        rendition.container = ext_info->container;
        rendition.mime_type = std::string{mime};
        rendition.bitrate_bps = audio.bitrate_bps;
        rendition.sample_rate_hz = audio.sample_rate_hz;
        rendition.bit_depth = audio.bit_depth;
        rendition.channels = audio.channels;
        rendition.duration_ms = audio.duration_ms;
        rendition.size_bytes = file_size_or_zero(path);
        rendition.storage_path = path.string();
        rendition.dlna_profile_name = dlna_pn.has_value() ? std::string{*dlna_pn} : std::string{};
        rendition.protocol_info = protocol_info;
        rendition.purpose = sonarium::media::RenditionPurpose::dlna_lossy;
        if (auto r = writer.upsert_rendition(rendition); r.has_value()) {
            ++report.renditions_upserted;
        } else {
            record_error("upsert_rendition", path, r.error());
        }
    }

    if (auto bumped = writer.bump_system_update_id(); !bumped.has_value()) {
        report.errors.emplace_back("bump_system_update_id: " + bumped.error());
    }

    return report;
}

} // namespace sonarium::scanner

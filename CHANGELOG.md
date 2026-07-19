# Changelog

All notable changes to Sonarium are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/); versions follow semver.

## [0.2.0] — 2026-07-19

### Added

- SQLite catalog backend (`SONARIUM_SQLITE_PATH`): a zero-service,
  single-file alternative to Postgres. Opened in WAL mode so the worker can
  import while the DLNA/HLS servers read the same file; same schema shape,
  upsert semantics, and `schema_version` migration flow as Postgres.
  Production mode accepts either backend. New `just import-sqlite` /
  `just run-sqlite` recipes.
- Arch Linux packaging: `packaging/archlinux/PKGBUILD` with pinned wrap
  sources (network-free `build()`), system Catch2 for `check()`, pruned
  package contents, and a sysusers.d service account.

### Changed

- Default Postgres image bumped to 17 (verified against 16, 17, and 18).
- systemd units hardened substantially (empty capability bounding set,
  `@system-service` syscall filter, address-family restrictions, and more);
  configuration errors no longer restart-loop.
- Sources modernized against clang-tidy (`.contains()`, ranges algorithms,
  const-correctness); the enforced lint-gate check set grew from 3 to 20
  categories.

### Fixed

- `sonarium.env.example` now sets the media/self base URLs the HLS server
  needs — the previous defaults pointed playlists at a dead port.
- Units shipped by the Arch package now reference `/usr/bin`.

## [0.1.0] — 2026-07-19

First tagged release: a self-hosted DLNA/UPnP MediaServer and HLS audio
server over a Postgres catalog.

### Highlights

- `sonarium-dlna`: SSDP discovery (interface-pinned, source-validated,
  rate-limited), ContentDirectory/ConnectionManager SOAP, DIDL-Lite browse,
  Range-capable media serving.
- `sonarium-server`: HLS master/media playlists, bounded on-demand ffmpeg
  segmentation with an LRU-capped on-disk cache.
- `sonarium-worker`: library scanner with native FS watchers (FSEvents /
  inotify), debounced rescans.
- `sonariumctl`: import, scan preview, transcode, and status probes.
- Security: HMAC-signed media URLs enforced across DLNA and HLS routes,
  media-root path containment, fail-closed production mode, SOAP parser
  work budget.
- Operations: graceful SIGTERM shutdown (SSDP byebye), `/healthz` +
  `/readyz` + `/version` probes, Postgres reconnect with SOAP-fault error
  surfacing, schema versioning, Dockerfile + compose profile + systemd
  units, CI (gate, ASan/UBSan, TSan, live Postgres).

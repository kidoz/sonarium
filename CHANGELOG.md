# Changelog

All notable changes to Sonarium are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/); versions follow semver.

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

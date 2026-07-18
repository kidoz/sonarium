# Sonarium

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![Build: Meson](https://img.shields.io/badge/build-Meson-7e7eed.svg)](https://mesonbuild.com/)
[![Tests: Catch2](https://img.shields.io/badge/tests-Catch2-3f6dbd.svg)](https://github.com/catchorg/Catch2)
[![Style: clang-format](https://img.shields.io/badge/style-clang--format-262d3a.svg)](https://clang.llvm.org/docs/ClangFormat.html)
[![Platform: Linux | macOS](https://img.shields.io/badge/platform-Linux%20%7C%20macOS-lightgrey.svg)](#requirements)

A self-hosted music streaming stack built around DLNA/UPnP discovery and an HLS audio server, written in C++23.

Sonarium imports your local music library, exposes it to DLNA renderers on the LAN, and serves the same catalog as HLS to browsers and modern clients. It is designed to run on a home server alongside a Postgres database.

## Components

| Binary             | Role                                                           |
| ------------------ | -------------------------------------------------------------- |
| `sonarium-dlna`    | DLNA/UPnP MediaServer (SSDP discovery, ContentDirectory, HTTP) |
| `sonarium-server`  | HLS audio server (master/media playlists, segment cache)       |
| `sonarium-worker`  | Catalog import + filesystem watcher (rendition jobs)           |
| `sonariumctl`      | Admin CLI (status probes, scan, transcode)                     |

Source modules live flat under `src/<name>/`: `core/`, `catalog/`, `media/`, `scanner/`, `transcode/`, `hls/`, `upnp/`, `dlna-core/`, `composition/`, plus the four binary roots above.

## Requirements

- C++23 toolchain (clang recommended — the project is developed against clang on macOS and Linux)
- [Meson](https://mesonbuild.com/) and [Ninja](https://ninja-build.org/)
- [just](https://github.com/casey/just) for the canonical task runner
- `clang-format` and `clang-tidy` for the format/lint gates
- `ffmpeg` / `ffprobe` for HLS segmenting and metadata extraction
- Docker (optional) for the Postgres backend
- Internet access on first build — Meson wraps fetch `atria`, `ctorwire`, `logspine`, `asterorm`, and Catch2 v3

## Quick start

```bash
# Configure + build everything
just build

# Run the canonical CI gate (format-check + lint + test)
just

# Boot the DLNA server (default port 8200) with the in-memory demo catalog
just run

# Boot the HLS server (default port 18201)
just run-server

# Import a local music tree (artist/album/[NN-]title.mp3 layout)
just import /path/to/music
```

The DLNA service binds the HTTP listener and advertises an LAN-reachable URL over SSDP on `239.255.255.250:1900`. Disable SSDP with `SONARIUM_DLNA_DISABLE_SSDP=1` or `--no-ssdp` when port 1900 is taken.

For an offline preview that prints `description.xml` and a sample `Browse(0)` response without opening a socket:

```bash
just run-offline
```

## Operator mode and safe defaults

Sonarium ships with two operator modes selected via `SONARIUM_MODE`:

- `development` (default): boots with insecure defaults so `just run` and tests work without configuration. Each insecure default emits a `WARN:` line at startup.
- `production`: refuses to start unless safe defaults are in place. Exits non-zero on any of:
  - wildcard bind (`0.0.0.0`, `::`, empty) without `SONARIUM_ALLOW_PUBLIC_BIND=1`
  - empty `SONARIUM_MEDIA_TOKEN_SECRET` (direct media URLs would be unauthenticated)
  - empty `SONARIUM_PG_CONNINFO` (the demo in-memory catalog must not run in production)
  - empty `SONARIUM_MEDIA_ROOT` (served file paths would not be contained to a library root)
  - Postgres connect/schema failure (no silent fallback to the demo catalog)

A minimal production invocation:

```bash
SONARIUM_MODE=production \
SONARIUM_DLNA_BIND_HOST=192.168.1.10 \
SONARIUM_MEDIA_TOKEN_SECRET="$(openssl rand -hex 32)" \
SONARIUM_PG_CONNINFO="host=db user=sonarium dbname=sonarium" \
SONARIUM_MEDIA_ROOT=/srv/music \
./buildDir/src/dlna/sonarium-dlna
```

## Media tokens and path containment

With `SONARIUM_MEDIA_TOKEN_SECRET` set, every media-serving route requires a
signed `?expires=...&sig=...` pair (HMAC-SHA256 over `<resource-id>|<expires>`):

- `sonarium-dlna` — `/media/renditions/{id}` (tokens are minted into DIDL-Lite
  resource URLs automatically).
- `sonarium-server` — all HLS routes. `/hls/tracks/{id}/master.m3u8` verifies a
  track-bound token; the variant and segment URLs it emits carry their own
  rendition-bound tokens, so a client only needs a signed master URL to play.
  Untokened or forged requests get `403`.

When `SONARIUM_MEDIA_ROOT` is set, both servers refuse to serve (or transcode)
any catalog storage path that does not canonicalize to a location inside that
root — symlinks pointing outside the library are rejected. Empty (dev mode)
disables containment.

The HLS segment cache is bounded: least-recently-used renditions are evicted
once the cache exceeds `SONARIUM_HLS_CACHE_MAX_MB`, and at most
`SONARIUM_HLS_MAX_TRANSCODES` ffmpeg jobs run at once — requests that would
exceed the bound receive `503` with `Retry-After` instead of queueing.

## Environment variables

| Variable                            | Service        | Purpose                                                          |
| ----------------------------------- | -------------- | ---------------------------------------------------------------- |
| `SONARIUM_MODE`                     | all            | `development` (default) or `production`                          |
| `SONARIUM_ALLOW_PUBLIC_BIND`        | all            | `1` to acknowledge `0.0.0.0` in production                       |
| `SONARIUM_MEDIA_TOKEN_SECRET`       | dlna / server  | HMAC secret for signed media URLs (empty disables signing)       |
| `SONARIUM_MEDIA_TOKEN_TTL_SECONDS`  | server         | Token expiry in seconds (default `3600`)                         |
| `SONARIUM_PG_CONNINFO`              | all            | libpq conninfo. When empty the demo catalog is used (dev only)   |
| `SONARIUM_DLNA_BIND_HOST`           | dlna           | HTTP bind address (default `0.0.0.0`)                            |
| `SONARIUM_DLNA_HTTP_PORT`           | dlna           | HTTP port (default `8200`)                                       |
| `SONARIUM_DLNA_ADVERTISED_HOST`     | dlna           | Override the SSDP `LOCATION` host (defaults to LAN auto-detect)  |
| `SONARIUM_DLNA_DISABLE_SSDP`        | dlna           | `1` to skip SSDP and run HTTP-only                                |
| `SONARIUM_SERVER_BIND_HOST`         | server         | HTTP bind address (default `0.0.0.0`)                            |
| `SONARIUM_SERVER_HTTP_PORT`         | server         | HTTP port (default `18201`)                                      |
| `SONARIUM_SERVER_BASE_URL`          | server         | Self URL embedded in playlists                                   |
| `SONARIUM_MEDIA_BASE_URL`           | server         | Base URL for `/media/renditions/...` (points at sonarium-dlna)   |
| `SONARIUM_HLS_CACHE_DIR`            | server         | On-disk segment cache (default `$TMPDIR/sonarium-hls`)            |
| `SONARIUM_HLS_SEGMENT_SECONDS`      | server         | Segment duration (default `6`)                                   |
| `SONARIUM_HLS_CACHE_MAX_MB`         | server         | Segment-cache size cap in MiB, LRU-evicted (default `8192`, `0` = unbounded) |
| `SONARIUM_HLS_MAX_TRANSCODES`       | server         | Max concurrent ffmpeg segmentation jobs (default `2`)            |
| `SONARIUM_MEDIA_ROOT`               | dlna / server / worker | Library root: containment boundary for served paths; scan root for the worker. Required in production |
| `SONARIUM_WORKER_POLL_INTERVAL_SECONDS` | worker     | Polling fallback interval                                        |
| `SONARIUM_WORKER_DEBOUNCE_SECONDS`  | worker         | Quiet period after FS events before rescanning (default `2`)     |

## Postgres backend

Sonarium ships a `docker-compose.yaml` with a Postgres instance for local development:

```bash
just pg-up        # start the container (waits for healthy)
just test-pg      # run Postgres-backed test suites against the live DB
just pg-psql      # open psql shell
just pg-stats     # show catalog table row counts
just pg-down      # stop the container (data preserved)
just pg-nuke      # stop + wipe the data volume
```

Schema is created idempotently by `PostgresRepository::ensure_schema()` on first connect. There is no separate migration step yet.

## Development workflow

```bash
just              # format-check + lint + test (canonical CI gate)
just build        # build all targets
just test         # run sonarium test suites
just lint         # clang-tidy across project sources
just format       # apply clang-format in place
just format-check # verify formatting (CI gate)
just rebuild      # clean + reconfigure + build
just --list       # all recipes
```

Test suites live under `tests/<module>/` mirroring `src/<module>/`. Postgres tests in `tests/catalog/` short-circuit unless `SONARIUM_PG_CONNINFO` is set.

## Repository layout

```text
src/
  core/          # primitives: sha256, hmac, media tokens, operator mode, logger interface
  media/         # MIME / codec / container types, duration formatting
  catalog/       # Repository interface, InMemoryRepository, PostgresRepository, schema
  scanner/       # media-tree walker, audio metadata via ffprobe, CatalogWriter
  transcode/     # ffmpeg command builder + runner
  hls/           # playlist builder, on-disk segmenter
  upnp/          # SOAP / SSDP / SCPD / device description
  dlna-core/     # DIDL-Lite, device profiles, browse handlers, protocol info
  composition/   # ctorwire-based DI for DLNA server, HTTP routes, SSDP service
  dlna/          # sonarium-dlna binary (main.cpp + DlnaConfig)
  server/        # sonarium-server binary (HLS routes)
  worker/        # sonarium-worker binary (FS watcher)
  cli/           # sonariumctl binary
tests/           # Catch2 tests mirroring src/
subprojects/     # Meson wraps for atria, ctorwire, logspine, asterorm, catch2
.agents/         # Durable project context, runbooks, and security policy
```

## License

[MIT](LICENSE) © 2026 Aleksandr Pavlov &lt;kidoz@gmail.com&gt;

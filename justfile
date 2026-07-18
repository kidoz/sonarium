# Sonarium task runner.
#
# Run `just` (no args) for the canonical CI gate: format-check + lint + test.
# Run `just --list` for the full recipe list.

set shell := ["bash", "-c"]
set positional-arguments := true

build_dir := "buildDir"
default_port := "18200"

# Default: run the canonical CI gate.
default: check

# ---------------------------------------------------------------------------
# Setup / build
# ---------------------------------------------------------------------------

# Configure Meson (debug). Idempotent — re-runs as `--reconfigure` if buildDir exists.
setup:
    if [ -d "{{build_dir}}/meson-info" ]; then \
        meson setup --reconfigure {{build_dir}}; \
    else \
        meson setup {{build_dir}}; \
    fi

# Configure Meson for release: optimized, LTO, hardening flags (see root
# meson.build for the flag set).
setup-release:
    if [ -d "{{build_dir}}/meson-info" ]; then \
        meson setup --reconfigure --buildtype=release -Db_lto=true {{build_dir}}; \
    else \
        meson setup --buildtype=release -Db_lto=true {{build_dir}}; \
    fi

# Build everything (auto-runs setup if needed).
build: setup
    meson compile -C {{build_dir}}

# Build a single target — e.g. `just build-target sonarium-dlna`.
build-target target: setup
    meson compile -C {{build_dir}} {{target}}

# Wipe the build directory.
clean:
    rm -rf {{build_dir}}

# Wipe + reconfigure + build.
rebuild: clean build

# Wipe build dir AND fetched wraps. Next setup re-clones atria/ctorwire/logspine.
distclean: clean
    rm -rf subprojects/atria subprojects/ctorwire subprojects/logspine subprojects/catch2

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

# Run sonarium suites only (skip atria's own integration tests). Optional
# timeout multiplier for slow environments — e.g. `just test 3` on CI runners.
test mult="1": build
    meson test -C {{build_dir}} --timeout-multiplier {{mult}} \
        --suite=core --suite=media --suite=catalog --suite=scanner --suite=dlna-core \
        --suite=upnp --suite=composition --suite=transcode --suite=hls --suite=server \
        --suite=worker --suite=cli --suite=smoke

# Run a single suite — e.g. `just test-suite composition`.
test-suite suite: build
    meson test -C {{build_dir}} --suite={{suite}}

# Run atria's own test suite (slow — exercises the framework).
test-atria: build
    meson test -C {{build_dir}} --suite=atria

# Run every test, sonarium + atria. Slowest gate.
test-all: build
    meson test -C {{build_dir}}

# ---------------------------------------------------------------------------
# Code quality
# ---------------------------------------------------------------------------

# Apply clang-format in place to every project source.
format: build
    meson compile -C {{build_dir}} sonarium-clang-format

# Verify formatting (CI gate). Fails if any file would change.
format-check: build
    meson compile -C {{build_dir}} sonarium-clang-format-check

# Run clang-tidy across every project source.
lint: build
    meson compile -C {{build_dir}} sonarium-clang-tidy

# Canonical CI gate — what `just` (no args) runs.
check: format-check lint test
    @echo "✓ format / tidy / tests all green"

# ---------------------------------------------------------------------------
# Run
# ---------------------------------------------------------------------------

#   just run             # bind 127.0.0.1:{{default_port}}
#   just run 8200        # bind 127.0.0.1:8200
# Run the DLNA service. Optional port arg, defaults to {{default_port}}.
run port=default_port: build
    SONARIUM_DLNA_HTTP_PORT={{port}} ./{{build_dir}}/src/dlna/sonarium-dlna

# Print description.xml + a sample Browse(0) without opening a socket.
run-offline: build
    ./{{build_dir}}/src/dlna/sonarium-dlna --offline

# Run the HLS server (default port 18201). Set SONARIUM_MEDIA_BASE_URL to point at sonarium-dlna.
run-server port="18201": build
    SONARIUM_SERVER_HTTP_PORT={{port}} ./{{build_dir}}/src/server/sonarium-server

# Run the admin CLI — e.g. `just ctl version`, `just ctl import ./music`.
ctl *args: build
    ./{{build_dir}}/src/cli/sonariumctl {{args}}

# ---------------------------------------------------------------------------
# Smoke
# ---------------------------------------------------------------------------

# End-to-end smoke against a local listener: start, curl description.xml + Browse + Range, stop.
smoke port=default_port: build
    #!/usr/bin/env bash
    set -euo pipefail
    pkill -TERM -f sonarium-dlna 2>/dev/null || true
    sleep 0.3
    SONARIUM_DLNA_HTTP_PORT={{port}} ./{{build_dir}}/src/dlna/sonarium-dlna \
        > /tmp/sonarium-dlna-smoke.log 2>&1 &
    SERVER_PID=$!
    trap 'kill -TERM $SERVER_PID 2>/dev/null || true' EXIT
    for i in $(seq 1 20); do
        if curl -sf -o /dev/null --max-time 1 http://127.0.0.1:{{port}}/description.xml; then
            echo "[smoke] sonarium-dlna ready on :{{port}}"
            break
        fi
        sleep 0.2
    done
    echo "--- GET /description.xml ---"
    curl -s -o /dev/null -w 'status=%{http_code} size=%{size_download} content_type=%{content_type}\n' \
        http://127.0.0.1:{{port}}/description.xml
    echo "--- POST Browse(album:1) ---"
    cat > /tmp/sonarium-browse.xml <<'XML'
    <?xml version="1.0" encoding="utf-8"?>
    <s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">
      <s:Body>
        <u:Browse xmlns:u="urn:schemas-upnp-org:service:ContentDirectory:1">
          <ObjectID>album:1</ObjectID>
          <BrowseFlag>BrowseDirectChildren</BrowseFlag>
          <Filter>*</Filter>
          <StartingIndex>0</StartingIndex>
          <RequestedCount>200</RequestedCount>
          <SortCriteria></SortCriteria>
        </u:Browse>
      </s:Body>
    </s:Envelope>
    XML
    media_url=$(curl -s -H 'Content-Type: text/xml' --data @/tmp/sonarium-browse.xml \
        http://127.0.0.1:{{port}}/upnp/control/content-directory \
        | grep -oE "http://[^\"<&]+/media/renditions/[^\"<&]+" | head -1)
    echo "  media_url=${media_url:-<none>}"
    echo "--- HEAD media ---"
    curl -sI "${media_url:-http://127.0.0.1:{{port}}/media/renditions/demo-mp3}" | head -5
    echo "--- Range bytes=0-15 ---"
    curl -s -H 'Range: bytes=0-15' -o /tmp/sonarium-part.bin \
        -w 'status=%{http_code} size=%{size_download}\n' \
        "${media_url:-http://127.0.0.1:{{port}}/media/renditions/demo-mp3}"
    echo "  bytes: $(head -c 16 /tmp/sonarium-part.bin)"
    echo "[smoke] ✓ all probes ok"

# Tail the smoke server log from the most recent run.
smoke-log:
    @tail -50 /tmp/sonarium-dlna-smoke.log 2>/dev/null || echo "no smoke log yet — run \`just smoke\` first"

# End-to-end smoke for the CLI + servers: spin up Postgres, import a fixture,
# transcode a track, probe `dlna status`, and curl the HLS playlists. Requires
# Docker + ffmpeg on PATH. Tears down servers on exit.
smoke-cli: build
    #!/usr/bin/env bash
    set -euo pipefail
    DLNA_PORT=18290
    HLS_PORT=18291
    FIXTURE=$(mktemp -d -t sonarium-smoke-cli-XXXXXX)
    trap 'pkill -TERM -f sonarium-dlna 2>/dev/null || true; pkill -TERM -f sonarium-server 2>/dev/null || true; rm -rf "$FIXTURE"' EXIT

    echo "[smoke-cli] building fixture audio library at $FIXTURE"
    mkdir -p "$FIXTURE/Sample Artist/Demo Album"
    ffmpeg -loglevel error -y -f lavfi -i "sine=frequency=440:duration=15" -ar 44100 -ac 2 \
        -metadata artist="Sample Artist" -metadata album="Demo Album" -metadata title="Hello Track" \
        "$FIXTURE/Sample Artist/Demo Album/01 - Hello Track.mp3"

    echo "[smoke-cli] starting Postgres (just pg-up)"
    just pg-up

    PG_DSN="{{pg_conninfo}}"
    export SONARIUM_PG_CONNINFO="$PG_DSN"

    # Wipe any prior catalog state so this run is reproducible.
    docker compose exec -T postgres psql -U sonarium -d sonarium -c \
        "TRUNCATE artist, album, track, media_rendition, storage_asset, playlist, playlist_item, system_state RESTART IDENTITY CASCADE" \
        >/dev/null 2>&1 || true

    echo "[smoke-cli] sonariumctl import"
    ./{{build_dir}}/src/cli/sonariumctl import "$FIXTURE"

    echo "[smoke-cli] sonariumctl scan (dry-run preview)"
    ./{{build_dir}}/src/cli/sonariumctl scan "$FIXTURE"

    TRACK_ID="sample-artist:demo-album:01-hello-track"
    echo "[smoke-cli] sonariumctl transcode --track-id $TRACK_ID --codec aac --bitrate 96"
    ./{{build_dir}}/src/cli/sonariumctl transcode --track-id "$TRACK_ID" --codec aac --bitrate 96

    echo "[smoke-cli] starting sonarium-dlna on :$DLNA_PORT"
    SONARIUM_DLNA_HTTP_PORT=$DLNA_PORT SONARIUM_DLNA_DISABLE_SSDP=1 \
        ./{{build_dir}}/src/dlna/sonarium-dlna > /tmp/sonarium-smoke-cli-dlna.log 2>&1 &
    for i in $(seq 1 20); do
        if curl -sf -o /dev/null --max-time 1 http://127.0.0.1:$DLNA_PORT/description.xml; then break; fi
        sleep 0.2
    done

    echo "[smoke-cli] sonariumctl dlna status --url http://127.0.0.1:$DLNA_PORT"
    ./{{build_dir}}/src/cli/sonariumctl dlna status --url "http://127.0.0.1:$DLNA_PORT"

    echo "[smoke-cli] starting sonarium-server on :$HLS_PORT"
    SONARIUM_SERVER_HTTP_PORT=$HLS_PORT \
    SONARIUM_MEDIA_BASE_URL="http://127.0.0.1:$DLNA_PORT" \
        ./{{build_dir}}/src/server/sonarium-server > /tmp/sonarium-smoke-cli-server.log 2>&1 &
    for i in $(seq 1 20); do
        if curl -sf -o /dev/null --max-time 1 http://127.0.0.1:$HLS_PORT/version; then break; fi
        sleep 0.2
    done

    echo "[smoke-cli] GET /hls/tracks/$TRACK_ID/master.m3u8"
    curl -sf "http://127.0.0.1:$HLS_PORT/hls/tracks/$TRACK_ID/master.m3u8"

    RENDITION_ID="$TRACK_ID:mp3"
    echo "[smoke-cli] GET /hls/renditions/$RENDITION_ID/index.m3u8 (triggers segmenting)"
    curl -sf "http://127.0.0.1:$HLS_PORT/hls/renditions/$RENDITION_ID/index.m3u8" | head -20

    echo "[smoke-cli] HEAD /hls/renditions/$RENDITION_ID/seg00000.ts"
    curl -sI "http://127.0.0.1:$HLS_PORT/hls/renditions/$RENDITION_ID/seg00000.ts" | head -5

    echo "[smoke-cli] ✓ all probes ok"

# ---------------------------------------------------------------------------
# Postgres (catalog backend) — docker-compose driven.
# ---------------------------------------------------------------------------

# DSN that all `pg-*` recipes use. Override by setting SONARIUM_PG_CONNINFO in the env.
pg_conninfo := env_var_or_default(
    "SONARIUM_PG_CONNINFO",
    "host=127.0.0.1 port=5432 user=sonarium password=sonarium dbname=sonarium"
)

# Start the dockerised Postgres backend (waits until it's healthy).
pg-up:
    docker compose up -d postgres
    @printf 'waiting for postgres'
    @for i in $(seq 1 30); do \
        if docker compose exec -T postgres pg_isready -U sonarium -d sonarium >/dev/null 2>&1; then \
            echo " ok"; \
            exit 0; \
        fi; \
        printf '.'; \
        sleep 1; \
    done; \
    echo " timed out"; \
    exit 1

# Stop the Postgres container (volume preserved). Use `pg-nuke` to wipe data.
pg-down:
    docker compose down

# Stop + delete the Postgres data volume. Use when schema migrations are stuck.
pg-nuke:
    docker compose down -v

# Open `psql` against the running Postgres container.
pg-psql:
    docker compose exec postgres psql -U sonarium -d sonarium

# Show how many rows are in each catalog table.
pg-stats:
    docker compose exec -T postgres psql -U sonarium -d sonarium -c \
        "SELECT 'artist' AS tbl, COUNT(*) FROM artist UNION ALL \
         SELECT 'album', COUNT(*) FROM album UNION ALL \
         SELECT 'track', COUNT(*) FROM track UNION ALL \
         SELECT 'rendition', COUNT(*) FROM media_rendition UNION ALL \
         SELECT 'asset', COUNT(*) FROM storage_asset UNION ALL \
         SELECT 'playlist', COUNT(*) FROM playlist;"

# Run the catalog suite against the live Postgres backend.
# Requires `just pg-up` first (or any reachable Postgres at SONARIUM_PG_CONNINFO).
test-pg: build
    SONARIUM_PG_CONNINFO="{{pg_conninfo}}" \
        meson test -C {{build_dir}} --suite=catalog --print-errorlogs

# Run the DLNA service against the live Postgres backend.
run-pg port=default_port: build
    SONARIUM_PG_CONNINFO="{{pg_conninfo}}" \
    SONARIUM_DLNA_HTTP_PORT={{port}} \
        ./{{build_dir}}/src/dlna/sonarium-dlna

# Scan a media root into the live Postgres catalog.
# Convention: <root>/<artist>/<album>/[NN - ]<title>.<mp3|flac|m4a|wav>
# Album art (`cover.{jpg,png}` or `folder.{jpg,png}`) is picked up automatically.
import path: build
    SONARIUM_PG_CONNINFO="{{pg_conninfo}}" \
        ./{{build_dir}}/src/cli/sonariumctl import "{{path}}"

# Same scan via the worker binary (uses --root or SONARIUM_MEDIA_ROOT).
worker path: build
    SONARIUM_PG_CONNINFO="{{pg_conninfo}}" \
        ./{{build_dir}}/src/worker/sonarium-worker --root "{{path}}"

# ---------------------------------------------------------------------------
# Wraps
# ---------------------------------------------------------------------------

# Show the commits each Meson wrap is pinned to.
wraps-status:
    @for w in subprojects/*.wrap; do \
        echo "=== $w ==="; \
        grep -E '^(url|revision)' "$w" | sed 's/^/  /'; \
    done

# Re-fetch the wrapped subprojects (clones + reconfigure).
wraps-refresh: distclean setup
    @echo "✓ wraps refreshed"

# ---------------------------------------------------------------------------
# AI agent standards (.agents/)
# ---------------------------------------------------------------------------

# Validate the canonical .agents tree against the vendored standard (v3.2.0).
# Stages .agents + root entrypoints in a temp dir so vendored deps, host
# wrapper dirs (.claude/), and backups don't produce false findings.
standards-validate:
    @rm -rf /tmp/sonarium-standards-check && mkdir -p /tmp/sonarium-standards-check
    @cp -a .agents AGENTS.md CLAUDE.md /tmp/sonarium-standards-check/
    PYTHONDONTWRITEBYTECODE=1 python3 .agents/tools/validate_standards_package.py \
        --root /tmp/sonarium-standards-check --mode strict-governance --consumer

# Regenerate .claude/skills/** wrappers from canonical .agents/skills/**.
standards-sync-claude:
    PYTHONDONTWRITEBYTECODE=1 python3 .agents/tools/sync_claude_skill_adapters.py --clean

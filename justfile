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

# Configure Meson for release.
setup-release:
    if [ -d "{{build_dir}}/meson-info" ]; then \
        meson setup --reconfigure --buildtype=release {{build_dir}}; \
    else \
        meson setup --buildtype=release {{build_dir}}; \
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

# Run sonarium suites only (skip atria's own integration tests).
test: build
    meson test -C {{build_dir}} \
        --suite=core --suite=media --suite=catalog --suite=scanner --suite=dlna-core \
        --suite=upnp --suite=composition --suite=transcode --suite=hls --suite=cli \
        --suite=smoke

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

# Sonarium container image. One image carries all four binaries; pick the
# service with the container command (see docker-compose.yaml).
#
#   docker build -t sonarium .
#   docker run --rm sonarium sonarium-dlna --offline
#
# The build stage fetches the pinned Meson wraps from their git revisions, so
# it needs network access.

FROM ubuntu:24.04 AS build

# GCC rather than clang: Ubuntu 24.04 ships clang 18, whose libstdc++ combo
# lacks std::expected (needs __cpp_concepts >= 202002L, clang 19+).
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential git ca-certificates \
        meson ninja-build pkg-config libpq-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# default_library=static keeps the wrap subprojects (logspine, asterorm, …)
# inside the binaries so the runtime stage only needs system libraries.
RUN meson setup build --buildtype=release -Db_lto=true -Ddefault_library=static \
    && meson compile -C build

FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
        ffmpeg libpq5 curl ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --create-home --shell /usr/sbin/nologin sonarium

COPY --from=build /src/build/src/dlna/sonarium-dlna \
                  /src/build/src/server/sonarium-server \
                  /src/build/src/worker/sonarium-worker \
                  /src/build/src/cli/sonariumctl \
                  /usr/local/bin/

# asterorm builds shared libraries regardless of default_library.
COPY --from=build /src/build/subprojects/asterorm/src/*.so* /usr/local/lib/
RUN ldconfig

USER sonarium
WORKDIR /home/sonarium

# sonarium-dlna HTTP / sonarium-server HTTP. SSDP (udp/1900) additionally
# requires host networking to be discoverable on the LAN.
EXPOSE 8200 18201

CMD ["sonarium-dlna"]

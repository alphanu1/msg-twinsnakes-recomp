# The Linux release build environment: Valve's Steam Runtime 3 "sniper" SDK
# (Debian 11 base, glibc 2.31) - what Steam's own Linux games are built in.
# A binary built here runs on the Steam Deck and on any newer desktop Linux;
# one built on a rolling distribution needs that distribution's glibc and
# runs nowhere older. (A plain debian:bullseye image was tried first: its
# support ended in 2026 and its security updates are not on the archive
# server yet, so its development packages no longer install.)
# Imported from Valve's flat sysroot tarball by build.sh: the layered image
# (registry.gitlab.steamos.cloud/steamrt/sniper/sdk) does not unpack on
# current Docker ("invalid hardlink target /bin/bunzip2").
FROM steamrt-sniper-sdk:sysroot
# SDL3, pinned to the release the port is developed against; built here so
# it carries glibc 2.31 too, and shipped beside the game.
ARG SDL_TAG=release-3.4.14
RUN git clone --depth 1 --branch ${SDL_TAG} https://github.com/libsdl-org/SDL /opt/sdl-src \
    && cmake -S /opt/sdl-src -B /opt/sdl-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_INSTALL_PREFIX=/opt/sdl3 -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
    && cmake --build /opt/sdl-build && cmake --install /opt/sdl-build \
    && rm -rf /opt/sdl-src /opt/sdl-build

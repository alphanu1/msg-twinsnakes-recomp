# Cross-compiling the Windows build on Linux: MinGW-w64 (the POSIX-threads
# variant, so the port's pthreads code builds unchanged) and SDL3's official
# MinGW development release, pinned to the version the port is developed
# against.
FROM debian:bookworm
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      gcc-mingw-w64-x86-64-posix g++-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64 \
      cmake ninja-build curl ca-certificates python3 \
    && rm -rf /var/lib/apt/lists/*
ARG SDL_VERSION=3.4.14
RUN curl -sL -o /tmp/sdl.tar.gz \
      https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VERSION}/SDL3-devel-${SDL_VERSION}-mingw.tar.gz \
    && mkdir -p /opt/sdl3 && tar -xzf /tmp/sdl.tar.gz -C /opt/sdl3 --strip-components=1 \
    && rm /tmp/sdl.tar.gz && ls /opt/sdl3

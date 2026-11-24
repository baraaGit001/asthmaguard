# Build environment matching Raspberry Pi OS Bookworm 64-bit (arm64).
#   podman build -t asthmaguard-build:bookworm -f device/deploy/build.Containerfile .
FROM docker.io/library/debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake pkg-config libsystemd-dev libasound2-dev ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src

FROM debian:stable

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    libglib2.0-dev \
    libkf6windowsystem-dev \
    liblua5.4-dev \
    pkg-config \
    qt6-base-dev \
    qt6-declarative-dev \
    qt6-svg-dev \
    qt6-wayland \
    qt6-wayland-dev \
    ninja-build \
    xdg-utils \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY . /workspace
CMD ["sleep", "infinity"]

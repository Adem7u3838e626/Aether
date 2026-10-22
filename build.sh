#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$(readlink -f "$0")")"

# Keep the desktop shell's QML modules out of HyprFM's Qt import scan.
unset QML_IMPORT_PATH QML2_IMPORT_PATH

cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=OFF
cmake --build build --parallel

wayland_socket="${WAYLAND_DISPLAY:-}"
if [[ -n "$wayland_socket" && "$wayland_socket" != /* ]]; then
    wayland_socket="${XDG_RUNTIME_DIR:-}/$wayland_socket"
fi

if [[ -n "${WAYLAND_DISPLAY:-}" && -S "$wayland_socket" ]]; then
    has_display=1
elif [[ -n "${DISPLAY:-}" && -S "/tmp/.X11-unix/X${DISPLAY##*:}" ]]; then
    has_display=1
else
    has_display=0
fi

if (( ! has_display )); then
    printf 'Build complete. No active Wayland or X11 display is available to launch HyprFM.\n'
    exit 0
fi

exec env QSG_RHI_BACKEND=opengl \
    ./build/src/hyprfm "$@"

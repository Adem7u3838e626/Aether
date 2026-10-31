#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"

cd "$ROOT_DIR"

# Keep the desktop shell's QML modules out of Aether's Qt import scan.
unset QML_IMPORT_PATH QML2_IMPORT_PATH

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=OFF
cmake --build "$BUILD_DIR" --parallel

# CMake installs the executable into /usr/bin/aether, the application menu
# entry into /usr/share/applications, and the icons/resources into /usr/share.
# On Arch, /bin is a symlink to /usr/bin.
sudo cmake --install "$BUILD_DIR" --prefix /usr
# Keep the project's normal `aether` command and provide the requested
# `ather` spelling as an alias too.
sudo ln -sfn aether /usr/bin/ather

# Add a launcher to the user's Desktop as well as the system application menu.
desktop_dir="$(xdg-user-dir DESKTOP 2>/dev/null || true)"
if [[ -z "$desktop_dir" || "$desktop_dir" == "$HOME" ]]; then
    desktop_dir="$HOME/Desktop"
fi
mkdir -p "$desktop_dir"
install -m 755 \
    "$ROOT_DIR/dist/io.github.adem7u3838e626.Aether.desktop" \
    "$desktop_dir/Aether.desktop"

printf 'Installed Aether to /usr/bin/aether\n'
printf 'Terminal commands: aether, ather\n'
printf 'Application menu shortcut: /usr/share/applications/io.github.adem7u3838e626.Aether.desktop\n'
printf 'Desktop shortcut: %s/Aether.desktop\n' "$desktop_dir"

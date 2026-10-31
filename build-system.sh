#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"

cd "$ROOT_DIR"
unset QML_IMPORT_PATH QML2_IMPORT_PATH

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DAETHER_DATA_DIR=/usr/share/Aether \
    -DBUILD_TESTS=OFF
cmake --build "$BUILD_DIR" --parallel

# Update the system installation, including the app menu entry and icons.
sudo cmake --install "$BUILD_DIR" --prefix /usr

# Keep the requested spelling available in the system command path.
sudo ln -sfn aether /usr/bin/ather

# Make the usual `aether` command resolve to the system build even when
# ~/.local/bin appears before /usr/bin in PATH. Preserve an existing local
# install as a backup before replacing it with the link.
user_aether="$HOME/.local/bin/aether"
mkdir -p "$(dirname "$user_aether")"
if [[ -L "$user_aether" ]]; then
    if [[ "$(readlink -f "$user_aether" 2>/dev/null || true)" != /usr/bin/aether ]]; then
        mv "$user_aether" "$user_aether.backup-$(date +%Y%m%d-%H%M%S)"
    fi
elif [[ -e "$user_aether" ]]; then
    mv "$user_aether" "$user_aether.backup-$(date +%Y%m%d-%H%M%S)"
fi
ln -sfn /usr/bin/aether "$user_aether"

# Refresh the user's Desktop shortcut too.
desktop_dir="$(xdg-user-dir DESKTOP 2>/dev/null || true)"
if [[ -z "$desktop_dir" || "$desktop_dir" == "$HOME" ]]; then
    desktop_dir="$HOME/Desktop"
fi
mkdir -p "$desktop_dir"
install -m 755 \
    "$ROOT_DIR/dist/io.github.adem7u3838e626.Aether.desktop" \
    "$desktop_dir/Aether.desktop"

printf 'Updated system installation: /usr/bin/aether\n'
printf 'Terminal commands: aether, ather\n'
printf 'Desktop shortcut: %s/Aether.desktop\n' "$desktop_dir"

printf 'User command now points to the system build: %s\n' "$user_aether"

#!/usr/bin/env bash
# Build/install the Flatpak from this repository's manifest, then launch it.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-dir}"
MANIFEST="$ROOT_DIR/io.github.adem7u3838e626.Aether.yml"
APP_ID="io.github.adem7u3838e626.Aether"

if ! command -v flatpak-builder >/dev/null 2>&1; then
    printf 'Missing required tool: flatpak-builder\n' >&2
    exit 1
fi
if ! command -v flatpak >/dev/null 2>&1; then
    printf 'Missing required tool: flatpak\n' >&2
    exit 1
fi

flatpak-builder --user --install --force-clean "$BUILD_DIR" "$MANIFEST"
exec flatpak run "$APP_ID" "$@"

#!/usr/bin/env bash
# Build/install the Flatpak from this repository's manifest, then launch it.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-dir}"
REPO_DIR="${REPO_DIR:-$ROOT_DIR/flatpak-repo}"
MANIFEST="$ROOT_DIR/io.github.adem7u3838e626.Aether.yml"
APP_ID="io.github.adem7u3838e626.Aether"

VERSION="$(sed -nE 's/^[[:space:]]*project\(aether[[:space:]]+VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$ROOT_DIR/CMakeLists.txt" | head -n1)"
if [ -z "$VERSION" ]; then
    printf 'Could not determine Aether version from CMakeLists.txt\n' >&2
    exit 1
fi
TAG="$(git -C "$ROOT_DIR" describe --tags --exact-match 2>/dev/null || printf 'v%s' "$VERSION")"
BUNDLE="$ROOT_DIR/Aether-${TAG}-x86_64.flatpak"

if ! command -v flatpak-builder >/dev/null 2>&1; then
    printf 'Missing required tool: flatpak-builder\n' >&2
    exit 1
fi
if ! command -v flatpak >/dev/null 2>&1; then
    printf 'Missing required tool: flatpak\n' >&2
    exit 1
fi

flatpak-builder --user --install --force-clean \
    --repo="$REPO_DIR" --default-branch=stable "$BUILD_DIR" "$MANIFEST"
flatpak build-update-repo "$REPO_DIR"
flatpak build-bundle "$REPO_DIR" "$BUNDLE" "$APP_ID" stable
printf 'Flatpak release bundle: %s\n' "$BUNDLE"
exec flatpak run "$APP_ID" "$@"

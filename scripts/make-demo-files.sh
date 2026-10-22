#!/bin/sh
# Build a demo tree for screen recordings: one file of every kind Aether can
# preview, thumbnail, or report metadata for.
#
#   ./scripts/make-demo-files.sh            # ~/Demo
#   ./scripts/make-demo-files.sh ~/Showcase # custom root
#
# Missing optional tools are skipped with a note; nothing here is required to
# build Aether. Rerunning wipes and rebuilds the tree.
set -eu

root="${1:-$HOME/Demo}"
case "$root" in
	"$HOME"/*) ;;
	*) echo "refusing to build outside \$HOME: $root" >&2; exit 1 ;;
esac

have() { command -v "$1" >/dev/null 2>&1; }
skip() { echo "  skip: $1 (install $2)"; }

[ -e "$root" ] && { printf 'remove existing %s? [y/N] ' "$root"; read -r a; case "$a" in y|Y) rm -rf "$root" ;; *) exit 1 ;; esac; }
mkdir -p "$root"/Pictures "$root"/Videos "$root"/Music "$root"/Documents \
         "$root"/Code "$root"/Fonts "$root"/Archives "$root"/Empty

echo "building $root"

# --- Pictures: png, jpg (with EXIF), webp, gif, svg -------------------------
echo "pictures"
if have magick; then
	# ImageMagick has no default font on a bare Arch install — resolve one.
	demofont="$(fc-match -f '%{file}' sans-serif 2>/dev/null || true)"
	label_img() { # size gradient label output
		if [ -n "$demofont" ]; then
			magick -size "$1" "gradient:$2" -gravity center -font "$demofont" \
				-pointsize "$5" -fill white -annotate 0 "$3" "$4"
		else
			magick -size "$1" "gradient:$2" "$4"
		fi
	}
	i=1
	for spec in "89b4fa-1e1e2e Aurora" "f38ba8-11111b Ember" "a6e3a1-181825 Fern" \
	            "f9e2af-1e1e2e Amber" "cba6f7-11111b Orchid" "94e2d5-181825 Lagoon"; do
		colors="${spec%% *}"; name="${spec##* }"
		label_img 1600x1000 "#${colors%%-*}-#${colors##*-}" "$name" \
			"$root/Pictures/$(printf '%02d' $i)-$name.png" 96
		i=$((i + 1))
	done
	# A wide one and a tall one, so the grid has to deal with both.
	magick -size 2400x800 gradient:'#313244-#89b4fa' "$root/Pictures/panorama.jpg"
	magick -size 700x1400 gradient:'#f5c2e7-#1e1e2e' "$root/Pictures/portrait.jpg"
	magick "$root/Pictures/01-Aurora.png" -resize 800x "$root/Pictures/aurora.webp"
	# Animated gif — proves the thumbnailer handles motion.
	magick -delay 12 -size 480x480 \
		gradient:'#89b4fa-#1e1e2e' gradient:'#f38ba8-#1e1e2e' \
		gradient:'#a6e3a1-#1e1e2e' gradient:'#f9e2af-#1e1e2e' \
		-loop 0 "$root/Pictures/loop.gif"
else
	skip "raster images" imagemagick
fi

cat > "$root/Pictures/logo.svg" <<'EOF'
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 200" width="200" height="200">
  <rect width="200" height="200" rx="24" fill="#1e1e2e"/>
  <circle cx="100" cy="100" r="62" fill="none" stroke="#89b4fa" stroke-width="10"/>
  <path d="M70 100h60M100 70v60" stroke="#f5c2e7" stroke-width="10" stroke-linecap="round"/>
</svg>
EOF

# EXIF makes the preview metadata sidebar worth showing.
if have exiftool && [ -f "$root/Pictures/panorama.jpg" ]; then
	exiftool -overwrite_original -q \
		-Make="Fujifilm" -Model="X-T5" -LensModel="XF 23mm f/1.4" \
		-FNumber=1.4 -ExposureTime=1/250 -ISO=400 -FocalLength=23 \
		-DateTimeOriginal="2025:06:14 18:32:10" \
		-GPSLatitude=23.7808 -GPSLatitudeRef=N \
		-GPSLongitude=90.4108 -GPSLongitudeRef=E \
		-Artist="Aether demo" -ImageDescription="Rooftop at golden hour" \
		"$root/Pictures/panorama.jpg"
else
	skip "EXIF metadata" perl-image-exiftool
fi

# --- Videos and audio -------------------------------------------------------
echo "videos + music"
if have ffmpeg; then
	ff() { ffmpeg -hide_banner -loglevel error -y "$@"; }
	ff -f lavfi -i testsrc2=size=1280x720:rate=30 -f lavfi -i sine=frequency=440 \
	   -t 12 -pix_fmt yuv420p -c:a aac -shortest "$root/Videos/clip-720p.mp4"
	ff -f lavfi -i smptebars=size=1920x1080:rate=25 -t 8 -pix_fmt yuv420p \
	   "$root/Videos/bars-1080p.mp4"
	ff -f lavfi -i mandelbrot=size=854x480:rate=25 -t 6 -c:v libvpx-vp9 -b:v 1M \
	   "$root/Videos/fractal.webm"
	ff -f lavfi -i "sine=frequency=220:duration=25" \
	   -metadata title="Low Tide" -metadata artist="Demo Ensemble" \
	   -metadata album="Field Recordings" -metadata date="2025" -metadata track="3" \
	   -metadata genre="Ambient" "$root/Music/low-tide.mp3"
	ff -f lavfi -i "sine=frequency=330:duration=18" \
	   -metadata title="Second Light" -metadata artist="Demo Ensemble" \
	   -metadata album="Field Recordings" "$root/Music/second-light.flac"
else
	skip "video + audio" ffmpeg
fi

# --- Documents: pdf, md, txt, csv, log --------------------------------------
echo "documents"
if have groff; then
	groff -Tpdf -ms > "$root/Documents/handbook.pdf" <<'EOF'
.TL
Aether Field Handbook
.AU
Demo Assets
.NH
Navigation
.PP
Type any letter to jump. Backspace goes up. Alt+Left and Alt+Right walk the
per-tab history. Ctrl+L edits the path directly.
.NH
Views
.PP
Ctrl+1 grid, Ctrl+2 Miller columns, Ctrl+3 detailed. Ctrl+Scroll zooms.
.bp
.NH
Previews
.PP
Space opens the quick preview overlay: images, video, audio, PDF, fonts, and
text all render inline with a metadata sidebar alongside.
.NH
Transfers
.PP
Copies run through rsync or gio, reporting live speed, ETA, and a pause button.
.bp
.NH
Theming
.PP
Themes are TOML. Saving one reloads it live, no restart.
EOF
else
	skip "PDF" groff
fi

cat > "$root/Documents/README.md" <<'EOF'
# Field Notes

A markdown file, so the text preview has headings, lists, and a code fence to
syntax-highlight.

## Checklist

- [x] Generate demo assets
- [x] Start the key overlay
- [ ] Record the take
- [ ] Trim and upload

## Snippet

```bash
aether ~/Demo --new-window
```

> Previews render through `bat` when it is installed, plain text otherwise.
EOF

cat > "$root/Documents/notes.txt" <<'EOF'
Plain text, no highlighting, no ceremony.

Line three exists so the preview has something to scroll.
Line four.
Line five.
EOF

{
	echo "date,region,transfers,bytes,duration_ms"
	echo "2025-06-01,north,142,88213402,4120"
	echo "2025-06-02,south,97,41028800,2210"
	echo "2025-06-03,east,318,190224881,9980"
	echo "2025-06-04,west,64,10240000,880"
} > "$root/Documents/transfers.csv"

{
	echo "2025-06-14 18:31:02 INFO  aether: session restored, 3 tabs"
	echo "2025-06-14 18:31:02 DEBUG previewservice: bat found at /usr/bin/bat"
	echo "2025-06-14 18:31:07 WARN  udisks2: no polkit agent on the session bus"
	echo "2025-06-14 18:31:19 INFO  fileops: copy started, 214 files, 1.8 GiB"
	echo "2025-06-14 18:31:44 ERROR gio: mount smb://nas/media failed: timeout"
} > "$root/Documents/aether.log"

# --- Code: a real git repo, so the status overlays light up -----------------
echo "code"
cat > "$root/Code/main.cpp" <<'EOF'
#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
EOF

cat > "$root/Code/watcher.py" <<'EOF'
"""Watch a directory and print what changes."""

import sys
import time
from pathlib import Path


def snapshot(root: Path) -> dict[Path, float]:
    return {p: p.stat().st_mtime for p in root.rglob("*") if p.is_file()}


def main(root: Path, interval: float = 1.0) -> None:
    previous = snapshot(root)
    while True:
        time.sleep(interval)
        current = snapshot(root)
        for path in current.keys() - previous.keys():
            print(f"added   {path}")
        for path in previous.keys() - current.keys():
            print(f"removed {path}")
        previous = current


if __name__ == "__main__":
    main(Path(sys.argv[1] if len(sys.argv) > 1 else "."))
EOF

cat > "$root/Code/index.html" <<'EOF'
<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8" />
    <title>Demo</title>
    <link rel="stylesheet" href="style.css" />
  </head>
  <body>
    <main class="card">
      <h1>Aether</h1>
      <p>A fast, keyboard-friendly file manager for Wayland.</p>
    </main>
  </body>
</html>
EOF

cat > "$root/Code/style.css" <<'EOF'
:root {
    --base: #1e1e2e;
    --text: #cdd6f4;
    --accent: #89b4fa;
}

body {
    background: var(--base);
    color: var(--text);
    font-family: "Inter", system-ui, sans-serif;
}

.card {
    max-width: 32rem;
    margin: 4rem auto;
    padding: 2rem;
    border: 1px solid color-mix(in oklab, var(--accent) 40%, transparent);
    border-radius: 12px;
}
EOF

cat > "$root/Code/deploy.sh" <<'EOF'
#!/bin/sh
set -eu

target="${1:?usage: deploy.sh <host>}"

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
rsync -a --info=progress2 build/src/aether "$target:/usr/local/bin/"
EOF
chmod +x "$root/Code/deploy.sh"

cat > "$root/Code/config.toml" <<'EOF'
[general]
theme = "catppuccin-mocha"
default_view = "grid"
show_hidden = false

[appearance]
transparency_enabled = true
transparency_level = 0.85
radius_large = 12
EOF

cat > "$root/Code/package.json" <<'EOF'
{
  "name": "aether-demo",
  "version": "1.0.0",
  "private": true,
  "scripts": {
    "build": "cmake --build build --parallel",
    "test": "ctest --test-dir build"
  },
  "keywords": ["wayland", "hyprland", "file-manager"]
}
EOF

if have git; then
	git -C "$root/Code" init -q
	git -C "$root/Code" add main.cpp style.css index.html
	git -C "$root/Code" -c user.email=demo@localhost -c user.name=Demo commit -qm "initial"
	# Leave one modified, one staged, one untracked — three different overlays.
	echo "// touched for the demo" >> "$root/Code/main.cpp"
	echo "/* staged */" >> "$root/Code/style.css"
	git -C "$root/Code" add style.css
else
	skip "git status overlays" git
fi

# --- Fonts ------------------------------------------------------------------
echo "fonts"
if have fc-list; then
	fc-list --format '%{file}\n' | grep -iE '\.(ttf|otf)$' | sort -u | head -3 \
		| while read -r f; do cp "$f" "$root/Fonts/"; done
else
	skip fonts fontconfig
fi

# --- Archives ---------------------------------------------------------------
echo "archives"
have tar && tar -czf "$root/Archives/documents.tar.gz" -C "$root" Documents
have zip && (cd "$root" && zip -qr Archives/code.zip Code -x 'Code/.git/*')
have tar && tar -cJf "$root/Archives/pictures.tar.xz" -C "$root" Pictures 2>/dev/null || true

# --- Extras: hidden files, symlink, a big file for the transfer shot --------
echo "extras"
cat > "$root/.hidden-notes.md" <<'EOF'
# Hidden

Ctrl+H toggles visibility of this file.
EOF
mkdir -p "$root/.cache-demo"
ln -sf ../Pictures/panorama.jpg "$root/Documents/cover.jpg"
mkdir -p "$root/Pictures/Nested/Deeper/Deepest"
: > "$root/Pictures/Nested/Deeper/Deepest/bottom.txt"

# 400 MiB of zeros: sparse on disk, real bytes on the wire, so the copy
# progress bar has time to show speed and ETA.
if have fallocate; then
	fallocate -l 400M "$root/large-transfer.bin"
else
	dd if=/dev/zero of="$root/large-transfer.bin" bs=1M count=400 status=none
fi

# A folder with enough entries that Ctrl+Scroll zoom and column counts matter.
mkdir -p "$root/Pictures/Batch"
if have magick; then
	n=1
	while [ "$n" -le 24 ]; do
		label_img 400x400 "#$(printf '%02x' $((80 + n * 5)))b4fa-#1e1e2e" "$n" \
			"$root/Pictures/Batch/shot-$(printf '%03d' $n).png" 72
		n=$((n + 1))
	done
fi

echo
echo "done: $root"
du -sh "$root" 2>/dev/null || true
find "$root" -type f | wc -l | sed 's/^/files: /'

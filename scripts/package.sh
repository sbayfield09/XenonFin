#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$project_dir"
test -x build/xenonfin
test -f build/xenon/xenonfin-boot.elf32
mkdir -p dist/stage
stage_dir=$(mktemp -d "$project_dir/dist/stage/package.XXXXXXXX")
trap 'rm -rf -- "$stage_dir"' EXIT
mkdir -p "$stage_dir/source" "$stage_dir/linux-x86_64" "$stage_dir/xbox-boot-diagnostic"
cp -a Makefile README.md LICENSE .gitignore src include platform scripts tests docs "$stage_dir/source/"
cp build/xenon/xenonfin-boot.elf32 "$stage_dir/xbox-boot-diagnostic/xenon.elf"
cp build/xenon/xenonfin-boot.elf32 dist/xenonfin-xell-music-fix.elf
chmod 644 dist/xenonfin-xell-music-fix.elf
cp docs/PLATFORM.md docs/HARDWARE.md docs/THIRD_PARTY.md docs/NATIVE_HTTP.md docs/NATIVE_BROWSING.md docs/NATIVE_MUSIC.md docs/NATIVE_RENDERING.md docs/LIBXENON_CONSOLE_LICENSE.md LICENSE "$stage_dir/xbox-boot-diagnostic/"
cp platform/xenon/third_party/minimp3/LICENSE "$stage_dir/xbox-boot-diagnostic/MINIMP3-LICENSE.txt"
printf '%s\n' 'XenonFin 0.4.1: complete-frame text presentation and 4 Hz playback statistics. Actual 0.4 MP3 playback confirmed on Xbox by user; new presentation requires console testing. No XEX or video playback. See NATIVE_RENDERING.md and NATIVE_MUSIC.md.' > "$stage_dir/xbox-boot-diagnostic/README.txt"
cp build/xenonfin "$stage_dir/linux-x86_64/"
strip --strip-unneeded "$stage_dir/linux-x86_64/xenonfin"
cp README.md LICENSE "$stage_dir/linux-x86_64/"
cp -a docs "$stage_dir/linux-x86_64/"
chmod -R u=rwX,go=rX "$stage_dir"
for target in source linux-x86_64 xbox-boot-diagnostic; do
    tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner --exclude=__pycache__ \
        -czf "dist/xenonfin-$target.tar.gz" -C "$stage_dir" "$target"
done
(cd dist && sha256sum xenonfin-*.tar.gz xenonfin-*.elf > SHA256SUMS)
printf '%s\n' 'Created source, Linux desktop, and Xbox boot diagnostic archives in dist/.'

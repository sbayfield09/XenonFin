#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$project_dir"
test -x build/xenonfin
test -f build/xenon/xenonfin-boot.elf32
test -f build/xenon/xenonfin-audio-test.elf32
test -f build/audio-fixtures/440-stereo.wav
test -f build/video-fixtures/xenonfin-test.m1v
test -f build/video-benchmarks/720x480/xenonfin-test.m1v
mkdir -p dist/stage
stage_dir=$(mktemp -d "$project_dir/dist/stage/package.XXXXXXXX")
trap 'rm -rf -- "$stage_dir"' EXIT
mkdir -p "$stage_dir/source" "$stage_dir/linux-x86_64" "$stage_dir/xbox-boot-diagnostic"
cp -a Makefile README.md LICENSE .gitignore .gitattributes src include platform scripts tests docs "$stage_dir/source/"
cp build/xenon/xenonfin-boot.elf32 "$stage_dir/xbox-boot-diagnostic/xenonfin-app.elf"
cp build/xenon/xenonfin-boot.elf32 dist/xenonfin-xell-video.elf
cp build/xenon/xenonfin-audio-test.elf32 dist/xenonfin-xell-audio-test.elf
cp build/xenon/xenonfin-audio-test.elf32 "$stage_dir/xbox-boot-diagnostic/xenonfin-audio-test.elf"
cp build/xenon/xenonfin-audio-test.elf32 "$stage_dir/xbox-boot-diagnostic/xenon.elf"
mkdir -p "$stage_dir/xbox-boot-diagnostic/audio-reference"
cp build/audio-fixtures/*.wav build/audio-fixtures/verification.json "$stage_dir/xbox-boot-diagnostic/audio-reference/"
chmod 644 dist/xenonfin-xell-video.elf dist/xenonfin-xell-audio-test.elf
cp build/video-fixtures/xenonfin-test.m1v dist/xenonfin-test.m1v
cp build/video-fixtures/xenonfin-test.m1v "$stage_dir/xbox-boot-diagnostic/xenonfin-test.m1v"
cp docs/PLATFORM.md docs/HARDWARE.md docs/THIRD_PARTY.md docs/NATIVE_HTTP.md docs/NATIVE_BROWSING.md docs/NATIVE_MUSIC.md docs/NATIVE_RENDERING.md docs/NATIVE_VIDEO.md docs/NATIVE_VIDEO_STREAMING.md docs/native-video-streaming-metrics.json docs/NATIVE_AV.md docs/NATIVE_AV_071.md docs/NATIVE_AV_072.md docs/NATIVE_AUDIO_073.md docs/native-audio-073-validation.json docs/native-av-072-metrics.json docs/LIBXENON_SOUND_LICENSE.md docs/native-av-recovery-metrics.json docs/native-av-metrics.json docs/LIBXENON_CONSOLE_LICENSE.md docs/LIBFAT_LICENSE.md LICENSE "$stage_dir/xbox-boot-diagnostic/"
cp platform/xenon/third_party/pl_mpeg/LICENSE "$stage_dir/xbox-boot-diagnostic/PL-MPEG-LICENSE.txt"
cp platform/xenon/third_party/pl_mpeg/SOURCE.md "$stage_dir/xbox-boot-diagnostic/PL-MPEG-SOURCE.md"
cp platform/xenon/third_party/minimp3/LICENSE "$stage_dir/xbox-boot-diagnostic/MINIMP3-LICENSE.txt"
printf '%s\n' 'XenonFin 0.7.3: original SDK sound backend restored after the 0.7.2 physical audio regression. FIRST: copy xenonfin-audio-test.elf as xenon.elf to FAT USB root and cold-boot XeLL. A runs a 440 Hz sine; stop if distorted. Left/Right selects 440/880/1000 Hz, silence, left/right/split tones; Up/Down selects 1/60 seconds; X stops/drains; B exits after drain. Follow NATIVE_AUDIO_073.md in order. Audio-only ELF excludes networking, video/MP3 decoding, video rendering and A/V scheduling. The archive default xenon.elf is the isolated audio test. The normal application is xenonfin-app.elf; rename it to xenon.elf for later tests only after PCM gates pass. Actual clean console audio remains UNCONFIRMED. Earlier 0.7.2 host/MMIO tests did not establish physical correctness. Movie/video/streaming sources are unchanged. WAV references contain the actual native generator PCM verified offline, not recorded Xbox output.' > "$stage_dir/xbox-boot-diagnostic/README.txt"
cp build/xenonfin "$stage_dir/linux-x86_64/"
strip --strip-unneeded "$stage_dir/linux-x86_64/xenonfin"
cp README.md LICENSE "$stage_dir/linux-x86_64/"
cp -a docs "$stage_dir/linux-x86_64/"
chmod -R u=rwX,go=rX "$stage_dir"
for target in source linux-x86_64 xbox-boot-diagnostic; do
    tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner --exclude=__pycache__ \
        -czf "dist/xenonfin-$target.tar.gz" -C "$stage_dir" "$target"
done
tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
    -czf dist/xenonfin-video-benchmarks.tar.gz -C build video-benchmarks
(cd dist && sha256sum xenonfin-*.tar.gz xenonfin-*.elf xenonfin-*.m1v > SHA256SUMS)
printf '%s\n' 'Created source, Linux desktop, and Xbox boot diagnostic archives in dist/.'

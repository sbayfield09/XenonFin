#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
output_dir=${1:-"$project_dir/build/video-benchmarks"}
for dimensions in 320x240 640x360 720x480; do
    mkdir -p -- "$output_dir/$dimensions"
    ffmpeg -hide_banner -loglevel error -f lavfi -i "testsrc2=size=$dimensions:rate=30" \
        -t 8 -an -c:v mpeg1video -bf 0 -g 12 -q:v 5 -pix_fmt yuv420p \
        -aspect "${dimensions/x/:}" -f mpeg1video -y "$output_dir/$dimensions/xenonfin-test.m1v"
done
printf '%s\n' 'Each clip is 8 seconds, 30 FPS, 240 silent MPEG-1 frames. Copy ONE profile' \
    'file to FAT USB root as xenonfin-test.m1v; do not concatenate the clips.' > "$output_dir/README.txt"
printf 'Created three 30 FPS hardware benchmark clips in %s\n' "$output_dir"

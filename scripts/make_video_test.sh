#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
output_file=${1:-"$project_dir/build/video-fixtures/xenonfin-test.m1v"}
mkdir -p -- "$(dirname -- "$output_file")"
ffmpeg -hide_banner -loglevel error -f lavfi -i testsrc2=size=320x240:rate=30 \
    -t 8 -an -c:v mpeg1video -bf 0 -g 12 -q:v 5 -pix_fmt yuv420p \
    -aspect 4:3 -f mpeg1video -y "$output_file"
printf 'Created silent 320x240 30 FPS MPEG-1 test: %s\n' "$output_file"

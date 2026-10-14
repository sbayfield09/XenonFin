#!/usr/bin/env bash
set -euo pipefail
# Rootless Debian 13 setup. Uses the distribution signing key and normal TLS;
# no application source, manifests, or lockfiles are modified.
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
setup_dir=${XENONFIN_SETUP_DIR:-/workspace/setup}
mkdir -p "$setup_dir/apt/empty" "$setup_dir/apt/lists/partial" "$setup_dir/debs" "$setup_dir/sysroot"
cat > "$setup_dir/apt/apt.conf" <<EOF
Dir::Etc::Parts "$setup_dir/apt/empty";
Dir::Etc::main "$setup_dir/apt/empty.conf";
Dir::Etc::sourcelist "$setup_dir/apt/sources.list";
Dir::Etc::sourceparts "$setup_dir/apt/empty";
Dir::State::lists "$setup_dir/apt/lists";
Dir::Cache "$setup_dir/apt/cache";
APT::Get::List-Cleanup "false";
EOF
printf 'deb https://deb.debian.org/debian trixie main\n' > "$setup_dir/apt/sources.list"
APT_CONFIG="$setup_dir/apt/apt.conf" apt-get update
(
    cd "$setup_dir/debs"
    APT_CONFIG="$setup_dir/apt/apt.conf" apt-get download \
        libsdl2-dev=2.32.4+dfsg-1 libavcodec-dev=7:7.1.5-0+deb13u1 \
        libavformat-dev=7:7.1.5-0+deb13u1 libavutil-dev=7:7.1.5-0+deb13u1 \
        libswscale-dev=7:7.1.5-0+deb13u1 libswresample-dev=7:7.1.5-0+deb13u1 \
        libfreetype-dev=2.13.3+dfsg-1+deb13u1 nlohmann-json3-dev=3.11.3-2.1
    for package in ./*.deb; do dpkg-deb -x "$package" "$setup_dir/sysroot"; done
)
# This environment already supplies g++, make, libcurl development files,
# FFmpeg/SDL/FreeType shared runtimes, ffmpeg, OpenSSL, a font, and Python 3.12.
# On a regular machine install the complete packages listed in README.md.
python3 "$project_dir/scripts/fetch_devkit.py" "$setup_dir/devkit"
python3 "$project_dir/scripts/fetch_test_server.py" "$setup_dir/jellyfin-server"
make -C "$project_dir" -j3 DEPS_ROOT="$setup_dir/sysroot" all
make -C "$project_dir" DEVKITXENON="$setup_dir/devkit" xenon
make -C "$project_dir/platform/xenon/tests" DEVKITXENON="$setup_dir/devkit" all test
make -C "$project_dir" DEPS_ROOT="$setup_dir/sysroot" test

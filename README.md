# XenonFin

Jellyfin client development for Xbox 360. **The requested finished Xbox 360 client is not complete.** This repository delivers a functioning C++ desktop client and a separately cross-compiled libXenon boot diagnostic. There is no `default.xex`, and no Xbox playback or HD benchmark has been performed.

The desktop program actually authenticates, browses and searches a Jellyfin server, negotiates playback, streams through a bounded HTTP buffer, demultiplexes and decodes with FFmpeg, presents video with SDL, queues stereo PCM, and reports playback to Jellyfin. Tests exercise a disposable real Jellyfin 10.10.7 server, including server-side HEVC-to-H.264 transcoding. The desktop backend is **not** an Xbox implementation.

See [platform investigation and remaining work](docs/PLATFORM.md), [test evidence](docs/VALIDATION.md), [hardware checklist](docs/HARDWARE.md), and [troubleshooting](docs/TROUBLESHOOTING.md).

## Build the desktop client

Tested on Debian 13 amd64: GCC 14.2.0, C++17, FFmpeg 7.1.5, SDL 2.32.4, FreeType 2.13.3, libcurl 8.14.1, nlohmann/json 3.11.3; Python 3.12 for integration tests. Requires a TrueType font and an SDL video/audio driver. Other operating systems and FFmpeg major versions have not been validated.

On a normal Debian machine with administrator access:

```sh
sudo apt-get update
sudo apt-get install build-essential pkg-config libcurl4-openssl-dev \
  libsdl2-dev libfreetype-dev nlohmann-json3-dev libavcodec-dev \
  libavformat-dev libavutil-dev libswscale-dev libswresample-dev \
  ffmpeg fonts-dejavu-core python3 openssl ca-certificates
make -j3
make test
./build/xenonfin
```

In the prepared cloud environment:

```sh
bash scripts/install_cloud.sh
make -j3 DEPS_ROOT=/workspace/setup/sysroot
./build/xenonfin --headless --seconds 1
```

`install_cloud.sh` uses signed Debian metadata and pinned package versions to extract development headers without root. It relies on the base environment's existing shared runtimes and compilers. It also installs a checksum-verified Free60 toolchain under `/workspace/setup/devkit` and builds the diagnostic. It does not install a VPN or change system certificates. A new desktop machine should use the full package installation above. If pinned Debian versions leave the current mirror, use an authenticated Debian snapshot or update and revalidate the dependency pins; do not disable verification.

## Use the desktop client

Connect a supported SDL game controller before or during execution. Start with the server keyboard; enter an address, then a username and password. A bare IPv4 address or hostname uses HTTP port 8096; an explicit `https://` URL uses port 443 by default. Non-root Jellyfin base paths are supported. For example `192.168.68.202:8096` becomes `http://192.168.68.202:8096`. A public reverse proxy may instead require `https://jellyfin.stanleybayfield.co.uk`. These are user-supplied examples, not compiled defaults. HTTP sends login/session traffic in plaintext; use validated HTTPS on untrusted networks.

| Controller | Behavior |
|---|---|
| D-pad / left stick | Move focus; hold to repeat |
| A | Select; pause/resume in playback |
| B | Back; cancel a request; stop playback |
| X | Search; delete a character on the keyboard |
| Y | Item details; switch keyboard symbols |
| Start | Finish text entry; cycle library sort; toggle player controls |
| LB / RB | Change sections; previous/next queued music track |
| LT / RT | Seek 10 seconds |
| Up / Down in player | Volume |
| Back/View in Music | Cycle albums, artists, tracks |

Keyboard testing equivalents: arrows, Enter, Escape, F1 (Y), F2 (X), F3 (Start), Tab (RB), Backspace (delete/music view), Page Up/Down (seek). Native desktop text input also accepts UTF-8. The controller keyboard covers printable ASCII, with SHIFT and a symbol page. Unicode titles render through FreeType; controller entry of non-ASCII text remains unsupported.

Home lists actual accessible libraries. Movies, TV, and Music are server queries; folder navigation traverses shows, seasons, episodes, artists and albums. Search results are grouped by type within each 40-item page. Move right beyond the last card for the next page, left before the first for the previous one. Start cycles name, date added, release date, and recently played sorting. Sorting applies to library queries; Home is the server's view list and search retains server relevance before client grouping.

On album details, A queues and starts its tracks. Individual audio details offer Play, Add to queue, and Play from beginning. Queue size is capped at 200 tracks, including albums; larger albums report an error rather than silently truncating. Settings provides logout, server editing, remember-login, bitrate, shuffle, repeat, clear queue, and exit. Playback returns to details when stopped or finished. Track changes reopen the stream; playback is not gapless. Queues and shuffle/repeat preferences are in-memory.

Settings are stored atomically as a mode-0600 JSON file in `$XDG_CONFIG_HOME/xenonfin/settings.json` or `~/.config/xenonfin/settings.json`. Override with `XENONFIN_SETTINGS`. Passwords are never saved. Remember-login stores a bearer token in this private, unencrypted local file; turn it off to omit the token on disk. Logout clears the local token; it does not revoke all sessions on the server. `XENONFIN_FONT` selects a TrueType font. Do not share the settings file or place it in Git.

## Playback

The **desktop** profile advertises H.264/AAC MP4 up to 1280×720, 30 fps, 8-bit SDR, stereo AAC, and the configured bitrate (default 4 Mbps). MP3, AAC and FLAC audio are supported. Sources are checked again before using a negotiated Direct Play capability. Other video requires Jellyfin's progressive HTTP H.264/AAC MPEG-TS transcode; other audio requests MP3. The program consumes the returned `TranscodingUrl`; a documented static-stream endpoint is used only after positive Direct Play negotiation. HDR requests require the server's own tone-mapping/transcoding support and have not been tested with HDR material.

The player uses a 2 MiB compressed-data ring, 64 KiB AVIO buffer, at most six converted video frames, a bounded PCM queue, two decoder threads, and audio-clock presentation. The decoder is FFmpeg software decoding on the desktop CPU. Artwork loads on a separate worker, with bounded response size, resizing, an LRU cache, and reusable textures. This does not establish memory use or decoding speed on an Xbox.

Pause, seek, volume, resume, start/progress/stop reporting and transcoder cleanup are implemented. A transcode seek stops its session and asks Jellyfin to negotiate from the new timestamp. Direct Play seeks require HTTP byte-range support. HLS, DASH, external media origins, nested media URLs, subtitles, live TV, optical media and DRM are intentionally unsupported. TLS certificate/hostname checks stay enabled. Credential-bearing redirects to different origins are refused.

## Tests

```sh
make DEPS_ROOT=/workspace/setup/sysroot test
python3 tests/integration.py
python3 scripts/fetch_test_server.py /workspace/setup/jellyfin-server
python3 tests/real_server.py
```

The fixture tests generate synthetic media with the installed FFmpeg, test HTTP failures/TLS rejection/cancellation, decode local and streamed 720p H.264/AAC, MP3/AAC/FLAC, artwork resizing, seek/pause and controller keyboard UI flow. The real-server test starts a temporary official Jellyfin 10.10.7 binary, creates a temporary account and synthetic libraries, and terminates it after testing. It never modifies your server. All cloud rendering/audio tests use SDL dummy outputs.

For an existing server, `--probe URL` tests public connectivity only. `--integration URL` uses credentials from secure environment bindings or an existing settings file. `JELLYFIN_USERNAME` / `JELLYFIN_PASSWORD` or `JELLYFIN_ACCESS_TOKEN` / `JELLYFIN_USER_ID` are recognized; values must not be passed on command lines or pasted into chat. `--integration-play URL --headless --seconds 15` also streams a real item and reports playback; set `JELLYFIN_TEST_ITEM_ID` to select the item. This changes that test account's watch history. `--play FILE_OR_URL`, `--artwork URL`, `--headless`, `--seconds N`, and `--exercise` support local diagnostics. CLI playback prints measured desktop counters, never URLs or tokens.

## Build and launch the Xbox diagnostic

```sh
python3 scripts/fetch_devkit.py /workspace/setup/devkit
DEVKITXENON=/workspace/setup/devkit make xenon
file build/xenon/xenonfin-boot.elf32
```

The installer extracts the pinned official Free60 OCI image without a container daemon. Manifest and layer SHA-256 digests are checked before extraction. The pinned image contains Xenon GCC 16.2.0, libXenon, newlib and its upstream link rules. No Microsoft SDK files are used.

For a console that already has working XeLL, copy `build/xenon/xenonfin-boot.elf32` to the root of a FAT32 USB device as `xenon.elf`, boot into XeLL using the console's existing setup, and let XeLL load that ELF. The diagnostic offers controller/display checks, Ethernet DHCP and a short PCM tone. B returns from `main`; the exact return behavior depends on the existing loader. **These launch instructions still require physical verification.** Do not flash a bootloader as part of this project. Aurora and XeXMenu do not directly launch this ELF; no dashboard chainloader is included.

## Packaging and project layout

`bash scripts/package.sh` creates separate source, Linux desktop and Xbox **boot-diagnostic** archives under `dist/`, with checksums. Desktop runtime dependencies remain system packages. The source archive includes the original source, build scripts, documentation and tests, not account data, dependencies or credentials. There is no XEX conversion step.

`main` handles CLI/lifecycle; `ui` and `keyboard` handle screen navigation; `graphics` and `input` implement SDL/FreeType/controller support; `network` provides bounded verified HTTP; `jellyfin` contains authentication, library parsing, device profiles, negotiation and session reporting; `settings` owns persistence; `images` owns asynchronous artwork; `playback` owns demux, buffering, decoding and stereo audio output. `Reporter` sends ordered session events on a worker. Console-specific startup is isolated in `platform/xenon`. There is no logging of request bodies, authorization headers or media URLs; libav's URL-bearing diagnostics are suppressed in the player.

Original source is GPL-2.0-or-later. See [third-party information](docs/THIRD_PARTY.md) before redistributing linked binaries.

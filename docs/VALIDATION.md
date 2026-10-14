# Validation — 14 October 2026

This page records the initial desktop/baseline validation. **The user subsequently confirmed 0.3 on real Xbox hardware:** boot, graphics/controller/audio diagnostics, networking/authentication, libraries, movie pagination and metadata/details. Version 0.4 adds the native MP3 pipeline documented in [NATIVE_MUSIC.md](NATIVE_MUSIC.md): build, waveform/resampling, sanitizer and real Jellyfin direct/transcoded playback/reporting tests passed on the host. **The user subsequently confirmed actual audible MP3 playback works in 0.4, with text-only flicker.** The 0.4.1 fix builds with the same toolchain and passes native/sanitizer renderer and existing protocol suites, plus real Jellyfin music integration. See [NATIVE_RENDERING.md](NATIVE_RENDERING.md) for the source-level cause and pending console presentation checks. Earlier hardware-unverified statements below refer to the original cloud validation.

The runnable target verified here is the **Debian amd64 desktop client**. The Xbox artifact was cross-compiled and inspected, not booted. No XEX or Xbox playback benchmark exists.

## Completed checks

| Check | Result |
|---|---|
| `make -j3 DEPS_ROOT=/workspace/setup/sysroot all` | Desktop app, core tests and UI test harness compile and link |
| `make DEPS_ROOT=/workspace/setup/sysroot test` | 57 passed, 0 failed |
| `make DEPS_ROOT=/workspace/setup/sysroot sanitize` | 57 passed, 0 failed with AddressSanitizer/UndefinedBehaviorSanitizer and leak detection |
| `python3 tests/integration.py` | 63 core/network cases passed, plus codecs, artwork, UI and TLS checks below |
| Controller input | SDL virtual controller A, D-pad focus, initial hold delay and repeat verified |
| Controller keyboard UI flow | Server entry, masked login, browsing, details, video render, return to details, search, settings and persistence passed through the actual application state machine |
| HTTP behavior | Bounded response, cancellation, connection-reset recovery, same-origin redirects, cross-origin credential protection and POST redirect refusal passed |
| Credential origin isolation | Saved credentials are not forwarded when a different server is selected; public probes do not send a saved token |
| TLS | Self-signed server rejected; supplied public server reached with certificate/hostname verification enabled |
| Artwork | Asynchronous PNG download, 600×900 → 240×360 resizing and repeated texture rendering passed |
| Music | HTTP MP3, AAC/M4A and FLAC decoded to stereo PCM; 48,000 / 48,128 / 48,000 output samples respectively for one-second synthetic clips |
| Player controls | Byte-range HTTP seek and pause/resume exercised against a real generated MP4 stream |
| `python3 tests/real_server.py` | Passed against a disposable official Jellyfin 10.10.7 server and temporary account |
| Real-server workflow | Login, two library views, four media items, real search, playback negotiation, static stream, server-side HEVC transcode, MP3 and start/progress/stop reporting passed |
| Session persistence | Saved token reused for real-server UI startup; private settings roundtrip/logout/remember-login opt-out tested |
| `DEVKITXENON=/workspace/setup/devkit make xenon` | Native diagnostic cross-compiles and links |
| Native artifact inspection | ELF32, big-endian, PowerPC, static, stripped; upstream-adjusted entry point `0x2300` |
| Setup repeatability | Rootless install script rerun successfully with cached, checksum-verified toolchain/server layers; core tests passed |
| Cloud launcher | `bash scripts/run_cloud.sh --headless --seconds 1` renders and exits successfully |

The standalone real-server test creates only synthetic movies/music and its own temporary account. It verifies actual server watch history updates after playback. The HEVC case asserts that the negotiated method is `Transcode` and the **received** codecs/container are H.264/AAC MPEG-TS, rather than merely trusting a requested profile. Local test servers terminate on completion.

## Measured desktop sample results

These are short, synthetic, headless runs using SDL **dummy video and audio**. The cloud machine has an amd64 CPU, not a Xenon CPU. Clock error is the difference between video presentation timestamps and the queued-audio clock; it is not a measurement of physical speaker/display latency. Dummy audio scheduling and startup time affect wall-clock FPS. A report of zero dropped frames for one run is not a universal performance guarantee.

| Source | Received format | Decoded / displayed frames | Dropped | Audio underruns | Max video/audio-clock error | Wall time |
|---|---|---:|---:|---:|---:|---:|
| Local generated 720p30 clip | H.264/AAC MP4, 1280×720 | 90 / 90 | 0 | 0 | 20 ms | 2.885 s |
| HTTP fixture 720p30 clip | H.264/AAC MP4, 1280×720 | 90 / 89 | 1 | 0 | 20 ms | 2.903 s |
| Real Jellyfin static stream | H.264/AAC MP4, 1280×720 | 90 / 88 | 2 | 0 | 20 ms | 3.055 s |
| Real Jellyfin HEVC conversion | H.264/AAC MPEG-TS, 640×360 | 90 / 90 | 0 | 0 | 20 ms | 4.192 s including transcode startup |

The static 720p samples generated approximately 144,384 stereo sample frames. The real-server two-second MP3 test generated 96,000 stereo sample frames. Complete machine-readable measured counters are in [validation-metrics.json](validation-metrics.json).

## User's server

After the environment network update/reconnect, `--probe https://jellyfin.stanleybayfield.co.uk` succeeded over verified HTTPS and returned the server name `Stan-Jellyfin`. Only the public information endpoint was used. No account credentials were configured, so authentication, library access or playback on that server was not tested. The earlier cloud proxy rejection was resolved; it was not treated as a Jellyfin authentication failure.

The alternate `192.168.68.202:8096` address requires a real LAN/VPN route from the cloud. It has not been validated. No VPN, router, server account, library or watch history on the user's server was modified.

## Not verified / remaining

- Xbox launch, display/controller/Ethernet/PCM behavior, console HTTP/TLS/UI integration, and a console decoder/player are not verified or complete.
- No Xbox 480p, 720p or 1080p playback, real A/V drift measurement, sustained bitrate, CPU/memory measurement, or long-running stability test was performed.
- Physical desktop sound/display output, desktop 1080p decoding, HDR tone mapping, every Jellyfin version, and every codec combination were not tested.
- HLS/DASH, cross-origin media, subtitles, gapless audio and transparent midstream reconnection are unsupported. Queue length is capped at 200 tracks.
- The first validation ran in an initially empty working repository. Subsequent commits and pushes are recorded in Git history; source archives omit Git metadata.
- Installation/startup instructions are saved in the environment draft. Saving them does not publish a snapshot. Review/save and publication remain product actions; a fresh task restore has not been tested.

The requested native Xbox 360 definition of done is **not satisfied**. PLATFORM.md describes the substantive native integration and hardware work required next.

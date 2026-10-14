# Xbox 360 acceptance checklist

**User-reported version 0.3 results:** XeLL launch, graphics/controller, audio diagnostics, Ethernet/HTTP, authentication, library discovery, movie pagination and metadata/details work on a real console. Version 0.4 adds a native MP3 pipeline, controls and reporting; **actual decoded music has not yet been heard on Xbox by this development session**. See [NATIVE_MUSIC.md](NATIVE_MUSIC.md) for the six core music test steps and fault checks. Record console model, output route, video mode, source revision, server version and track format. Video/artwork rows remain future targets.

| Check | Expected behavior | Status |
|---|---|---|
| XeLL ELF launch on Fat / Slim / E | Each supported model boots a labeled native client | Baseline boot confirmed by user; new ELF/model matrix unrun |
| 720p / 1080p output and safe area | Correct 16:9 display, readable text, no cropping | Unrun |
| Controller, sticks, hold repeat, hotplug | Correct focus and controls without keyboard | Baseline navigation confirmed; new keyboard/repeat/hotplug unrun |
| Ethernet DHCP, link loss, reconnect | Bounded timeout and recoverable connection | Baseline init/link/IP confirmed; actual DHCP ACK and new timeout/retry unrun |
| Stereo 48 kHz little-endian PCM | Clean audio on HDMI/analog routes used | Baseline audio confirmed by user; route matrix unrun |
| Initial server / hostname / base path | RAM-only numeric IPv4 HTTP in native 0.3; hostname/base path remain unsupported | Native HTTP communication confirmed in 0.2 |
| Username/password / invalid credentials | Masked entry, safe errors, no secrets in logs | Successful native login/token confirmed in 0.2; failure matrix unrun |
| Valid token restart / expiry / logout | Session reuse and reauthentication | Unrun |
| Movies / shows / seasons / episodes | Real hierarchy, metadata and 10-item pages | Movies confirmed in 0.3; TV hierarchy host-tested, console matrix unrun |
| Artists / albums / tracks | Real hierarchy, metadata and bounded neighbor fetching | Native 0.4 host-tested; console acceptance pending |
| Search / cancel / empty result | Real grouped server results, UI remains responsive | Unrun |
| Artwork / malformed image / cache churn | Correct resized images, stable memory | Unrun |
| MP3 / mono / stereo / rate conversion | Native MP3 → 48 kHz stereo S16LE; optional server MP3 transcode | Actual compressed decode and real Jellyfin host tests pass; audible Xbox output pending |
| Music pause / resume / stop / skip / natural advance | Responsive UI and accurate queued-PCM progress | Native 0.4 host-tested; console pending; shuffle/repeat absent |
| 480p H.264/AAC local then streamed | Sustained measured decode and A/V synchronization | Unrun |
| 720p30 Baseline / Main | Sustained measured throughput at 3–5 Mbps | Unrun |
| High profile / 1080p | Only enable if supported by measurements | Unrun |
| HEVC / multichannel / HDR transcode | Server returns decoder-compatible SDR stream | Unrun |
| Pause / seek / stop / restart | Decoder/audio/buffer state resets correctly | Unrun |
| Resume / progress reports | Position matches server within reporting interval | Unrun |
| Cable unplug / server restart / timeout | Bounded buffers and actionable recovery | Unrun |
| HTTPS valid/expired/wrong-host cert | Strict validation; no verification bypass | Unrun |
| Two-hour playback / repeated stop / library churn | No leaks, memory exhaustion or gradual drift | Unrun |

For each codec/resolution, record decoded/presented FPS, dropped/late frames, audio underruns, average/maximum video-to-audio clock error, startup and rebuffer durations, compressed-buffer occupancy, CPU utilization, peak/steady memory and network throughput. Include at least ten minutes of motion/detail-heavy material and a two-hour stability run for the chosen supported mode. Dummy audio timing on a cloud machine is not a hardware A/V measurement. A 1080p display mode does not establish 1080p decoding.

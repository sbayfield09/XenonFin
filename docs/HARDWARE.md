# Xbox 360 acceptance checklist

**User-reported version 0.2 results:** XeLL launch, graphics, controller navigation, audio, Ethernet, Jellyfin HTTP communication, username/password login, HTTP 200 and a token in RAM work on a real console. Exact model/modes were not supplied. Version 0.3 adds authenticated library browsing and still needs console acceptance; see [NATIVE_BROWSING.md](NATIVE_BROWSING.md). Record model, video cable/mode, XeLL version, exact source revision, toolchain digest, server version and wired route with each result. The later playback rows below are future targets, not features of 0.3.

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
| Movies / shows / seasons / episodes | Real hierarchy, metadata and 10-item pages | Native 0.3 implemented; console unrun |
| Artists / albums / tracks | Real hierarchy and alternate music views; no playback queue yet | Native 0.3 implemented; console unrun |
| Search / cancel / empty result | Real grouped server results, UI remains responsive | Unrun |
| Artwork / malformed image / cache churn | Correct resized images, stable memory | Unrun |
| MP3 / AAC / FLAC conversion | Real synchronized audio output | Unrun |
| Queue / skip / previous / shuffle / repeat | Correct sequence without resource growth | Unrun |
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

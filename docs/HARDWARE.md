# Xbox 360 acceptance checklist

**No entries below have been run on hardware.** Record console model, video cable/mode, XeLL version, exact source revision, toolchain image digest, server version, codec/profile/level/resolution/FPS/bitrate, wired network route and sample checksum with each result. The boot diagnostic currently supports only the initial platform checks. Client checks wait for the native integration described in PLATFORM.md.

| Check | Expected behavior | Status |
|---|---|---|
| XeLL ELF launch on Fat / Slim / E | Each supported model boots a labeled native client | Unrun |
| 720p / 1080p output and safe area | Correct 16:9 display, readable text, no cropping | Unrun |
| Controller, sticks, hold repeat, hotplug | Correct focus and controls without keyboard | Unrun |
| Ethernet DHCP, link loss, reconnect | Bounded timeout and recoverable connection | Unrun |
| Stereo 48 kHz little-endian PCM | Clean audio on HDMI/analog routes used | Unrun |
| Initial server / hostname / base path | Saved valid URL and server information | Unrun |
| Username/password / invalid credentials | Masked entry, safe errors, no secrets in logs | Unrun |
| Valid token restart / expiry / logout | Session reuse and reauthentication | Unrun |
| Movies / shows / seasons / episodes | Correct hierarchy, metadata, paging and sorting | Unrun |
| Artists / albums / tracks | Correct hierarchy and complete bounded queues | Unrun |
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

# Xbox 360 acceptance checklist

**User-reported baseline results:** XeLL launch, rendering, controller navigation, audio output, Ethernet initialization and link/IP display (`192.168.68.112`) work on a real console. Exact model/modes were not supplied. DHCP ACK/lease verification and the new HTTP/authentication build are still untested on that console; see [NATIVE_HTTP.md](NATIVE_HTTP.md). Record model, video cable/mode, XeLL version, exact source revision, toolchain digest, server version, wired route and sample checksum with each result.

| Check | Expected behavior | Status |
|---|---|---|
| XeLL ELF launch on Fat / Slim / E | Each supported model boots a labeled native client | Baseline boot confirmed by user; new ELF/model matrix unrun |
| 720p / 1080p output and safe area | Correct 16:9 display, readable text, no cropping | Unrun |
| Controller, sticks, hold repeat, hotplug | Correct focus and controls without keyboard | Baseline navigation confirmed; new keyboard/repeat/hotplug unrun |
| Ethernet DHCP, link loss, reconnect | Bounded timeout and recoverable connection | Baseline init/link/IP confirmed; actual DHCP ACK and new timeout/retry unrun |
| Stereo 48 kHz little-endian PCM | Clean audio on HDMI/analog routes used | Baseline audio confirmed by user; route matrix unrun |
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

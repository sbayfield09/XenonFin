# XenonFin 0.7 — movie audio and an audio-master clock

The user confirmed **0.6 on Xbox 360**: real Jellyfin progressive MPEG-PS, MPEG-1 decode, GPU presentation at approximately 30 FPS / 720×414, zero drops. MP3 music output is also hardware-confirmed. Version 0.7 adds movie audio to those components. **The new movie audio and physical A/V synchronisation have not been tested on a console by this environment.** Hearing both sound and pictures is insufficient to establish synchronisation.

## Install and test on Xbox

1. Download the [XeLL ELF](../dist/xenonfin-xell-video.elf) or [boot archive](../dist/xenonfin-xbox-boot-diagnostic.tar.gz). Copy the ELF as **xenon.elf** and boot using your existing XeLL procedure. Verify **XENONFIN 0.7**. The unchanged [confirmed 0.6 build](https://github.com/sbayfield09/XenonFin/tree/ae879e810ea103e60fda50eefc351e3b5340dcf5/dist) is available for rollback.
2. Initialise Ethernet, verify the DHCP ACK lease, connect to your numeric IPv4 HTTP server (for example **192.168.68.202:8096**) and sign in. Stop MP3 music first and let its DMA queue drain. Native DNS/HTTPS/proxy base paths remain unsupported. No test contacts or changes your personal server from the cloud environment.
3. Open **Libraries → movie/episode → details → A**. Verify real moving movie frames **and the actual movie soundtrack**. A tone or the existing music diagnostics do not count. The request now targets MPEG-1 video plus MP3 audio, not 0.6's discarded MP2 packets. Original formats still require server transcoding; unsupported originals never reach the Xbox video decoder.
4. Use **Left/Right** to select ten statistics pages. **Page 9** shows the detected MP3 sample rate/channels, output PCM format, PCM/DMA/compressed audio occupancy, audio underruns, gap events and audio/video PES counters. **Page 10** shows consumed-audio position, video-minus-audio phase, maximum presentation lateness, initial PTS offset, timestamp error and audio decode time. Pages 1–8 retain video/render/network/clock/drop diagnostics. The timer and all statistics retain the stable compositor and approximately four-Hz UI updates.
5. **A or Start** pauses/resumes. On pause, input submission stops; audio and video continue together until the **at-most 8 KiB hardware queue drains (42.667 ms at 48 kHz)**, then both hold. The UI may briefly say **PAUSING**, then **PAUSED**. Check sound, image and consumed-sample position freeze, including a 20–30 second pause. Resume must retain alignment without a burst of catch-up frames.
6. **X** stops. **B** stops and returns to the current browser/details location; changing sections also stops. Already submitted DMA may remain audible for at most the same short queue duration. Cleanup/reporting remains asynchronous and waits for consumption before recording the final position. Wait for cleanup before another movie/music session. A DMA queue that fails to drain within two seconds produces an explicit error; reboot before more playback rather than resetting unknown registers.
7. Play the flash/beep fixture below for at least **five continuous minutes**. Check the flash coincides with the beep near the beginning and at 30 s, 60 s and 300 s. Repeat around pause/resume and a short Ethernet interruption. Also check normal dialogue/lip movement in a familiar film. Record the TV/receiver output if possible to compare flash onset and beep onset in milliseconds. A fixed output-chain delay differs from a steadily growing drift. The overlay measures application/DMA timing, **not** TV, receiver, HDMI/DAC or recording latency; use the same output route and game mode for comparisons.
8. Record console model, revision, display/refresh, HDMI/analogue/receiver route, server/FFmpeg versions, source codecs, detected audio rate/channels, rendered FPS, drops, audio/video underruns, gap/late-PCM events, A/V phase and buffer occupancy at start and after five minutes. Check Jellyfin progress/pause/stop and encoder cleanup. Afterwards verify login/library browsing, actual MP3 music sound, pause/skip/stop, local CPU/GPU clips and controller/menu responsiveness.

Use this **server-library source fixture**, not a local Xbox decoder file. Its white flash and 1 kHz beep start together every second. Import it into a Jellyfin movie library and browse/play it so real transcoding, networking, demux and decoding are exercised:

```sh
ffmpeg -hide_banner -f lavfi \
  -i "color=c=black:s=720x414:r=30,drawbox=color=white:t=fill:enable='lt(mod(t,1),0.1)'" \
  -f lavfi -i "aevalsrc='if(lt(mod(t,1),0.05),0.4*sin(2*PI*1000*t),0)':s=48000" \
  -t 300 -c:v libx264 -preset veryfast -pix_fmt yuv420p \
  -c:a aac -b:a 128k 'XenonFin AV Sync (2026).mp4'
```

The display/audio model and five-minute host results below cannot replace these physical acceptance steps. Do not call 0.7 synchronised on Xbox until the real flash/beep/dialogue measurements pass.

## Server requirements and codec verification

The documented 0.6 `/Items/{id}/PlaybackInfo` and `/Videos/{id}/stream.mpeg` routes remain; authentication stays in headers, not URLs. The separate native profile now advertises **MPEG-1 / MP3 / MPEG-PS**, direct play/copy disabled, video at most 720×480 and 30 FPS. It requests `AudioCodec=mp3`, `AudioBitRate=128000`, `AudioSampleRate=48000`, `MaxAudioChannels=2` and the chosen source `AudioStreamIndex`. The original video constraints/session/device IDs and playback reporting/encoding cleanup are retained. Requires permission to transcode both video and audio and a configured FFmpeg with MPEG-1, MP3 (normally libmp3lame) and MPEG-PS support. No custom server service or undocumented endpoint is used.

Verified against a disposable **Jellyfin 10.10.7 / FFmpeg 7.1.5** installation: actual returned bytes contain MP3 at **48 kHz**, mono for a mono source and stereo after a 5.1 source downmix. A channel maximum is not a promise that Jellyfin will upmix mono. The native decoder also checks the actual MPEG audio header/layer, decoded rate and channels; it rejects MP2, free-format MP3, corruption and unsupported format changes instead of interpreting them as MP3. Valid mono/other supported MP3 rates reuse the existing channel conversion/linear sample-rate conversion. Xbox output is always **48 kHz, stereo, signed 16-bit little-endian PCM**, the pinned libXenon contract. No new MP2 decoder is needed.

A source positively advertised as video-only retains the working monotonic video-only scheduler. A failed/missing audio stream in an audio-bearing negotiation is an error or a reported gap, not an unannounced switch to a second free-running clock. Unrecognised initial timestamps, backward discontinuities beyond 100 ms, forward gaps beyond ten seconds and unsupported codec/sequence changes produce a visible error. Seeking, subtitle rendering, alternate source/audio selection, HD codecs and DSP/volume changes are not part of 0.7.

## Demux, timestamps and PCM ownership

The bounded MPEG-PS parser now routes **E0 video** and **C0 audio** independently; Jellyfin/FFmpeg produces one selected stream of each type. It handles fragmented pack/PES headers/payloads, MPEG-1 and MPEG-2 PES framing, optional PTS/DTS headers, and **33-bit timestamp wrap on a shared epoch**. Subsequent packets may omit PTS; initial video/audio anchors are required for an audio-bearing stream. PTS callbacks record elementary byte offsets. An audio PES timestamp applies to the first MP3 frame **starting** in that PES, even if its first bytes continue a previous frame. A bounded marker queue preserves that relationship across partial reads.

The video timeline is normalised to the first video PTS. The forced constant-rate MPEG-1 stream retains the proven exact rational display-order frame timestamps. Audio's first decoded-sample PTS is compared with video's initial PTS. Early audio is trimmed sample-exactly; late audio receives leading silence. The measured **−23.022 ms** in the host MP3 transcode is an observed encoder offset, **not a hardcoded correction**. Periodic audio PTS are checked against decoded sample counts. Differences beyond 1 ms acquire PCM timeline mappings (90 kHz rounding is much smaller), instead of allowing cumulative drift toward a large discontinuity threshold; late PCM is trimmed to its mapped position. The sparse mapping queue avoids inserting silence between normal frame/descriptor boundaries.

MP3 uses the same vendored scalar minimp3, endian-safe PCM emitter and resampler as music. Music's existing decoding defaults remain unchanged. Movie mode performs **at most one MP3 frame decode per service call**, accepts a validated exact complete MPEG frame through minimp3's supported interface, and retains incomplete compressed bytes. It does not wait for a large decode window or whole HTTP EOF to flush complete audio frames. No thread, blocking HTTP read, sleep in audio code or replacement toolchain was introduced.

Data remains bounded: **512 KiB PS ring**, one **65,542-byte PES scratch packet**, **256 KiB video ES** inside the existing 2 MiB video arena, **128 KiB audio ES**, **128 KiB software PCM**, bounded 128-entry timestamp/mapping queues, the existing **four YUV slots plus one prepared picture**, and **8 KiB maximum queued DMA**. Accepted lwIP pbufs are copied before ACK/free; an insufficient ring returns `ERR_MEM` with no partial ownership transfer. HTTP lengths/totals remain 64 bit. Neither movie nor audio is downloaded in full.

## One clock, buffering and controls

`video_audio.c` computes position from **submitted PCM frames minus actual unplayed DMA frames**, read through `xenon_sound_get_unplayed()`. The pinned driver reads descriptor position and current remaining length; submitted bytes alone never advance the clock. Position is clamped monotonically across queue snapshots. This is hardware consumption position, not a measurement of the downstream speaker/DAC pipeline.

`xf_video_master()` supplies that position to the existing scheduler. Every presentation/decode slack/deadline calculation uses audio time while audio is enabled; the wall clock still measures work, watchdogs, VBlank and sampled FPS. Initial presentation waits for decoded video preroll and PCM. The original late-drop rule remains: only beyond the configured threshold **and** with a newer frame due. Queue fullness does not discard frames. Pause stops submission and keeps video following consumption until DMA empties, then holds; resume restarts both from that same sample position. Reports use consumed audio position and the joint pause/buffering state.

Network/audio starvation normally drains the short DMA queue, freezes the audio clock and holds video. Video starvation likewise stops further audio submission and holds both after the bounded drain. Recovery waits for usable PCM and decoded video, keeping PTS/sample accounting. A no-progress watchdog errors after eight seconds outside user pause; queue and marker backpressure never causes an unlimited UI wait.

A separate multiplexing case needs care: an audio tail/gap can leave the PS parser blocked on a **full video ES ring**, preventing access to later audio or whole-program EOF. Waiting for network data cannot release that video. Only in that blocked condition, the client can clock silence through the **same DMA device** to release video. PCM timeline mappings place any later audio at its correct timestamp, trimming genuinely late data; gap/late counters expose this recovery. This handles shorter audio tracks without inventing an independent wall-clock video tail. When no decoded video is available the ordinary hold/rebuffer rule still applies. It is not a claim that silent concealment equals uninterrupted sound.

The pinned sound backend has no pause API. No undocumented register bit, DMA reset, reinitialisation or guessed hardware pause command was added. Controller/UI service remains cooperative; output queues are capped so pause/stop latency stays bounded. The working GPU upload/cache/presentation code and stable text renderer were not replaced.

## Reproduce build and host checks

```sh
DEVKITXENON=/workspace/setup/devkit make xenon
make native-test
make -C platform/xenon/tests SANITIZE=1 all test
python3 tests/native_video.py
python3 tests/native_video_server.py --extended --long
python3 tests/native_server.py --music
```

`--case TEXT` selects a test label during diagnosis. `JELLYFIN_VIDEO_STREAM_PROBE` selects a sanitized probe; the existing server/FFmpeg path overrides still apply. Temporary real Jellyfin instances/accounts and generated fixtures are removed after testing; no personal credentials or server changes are required. Only host TCP/final display/audio-device adapters are simulated; protocol, PS framing, actual MPEG-1/MP3 decode, conversion, PCM assembly, playback controls and video scheduling are production C. Host model rate changes and injected network stalls are fault tests, not Xbox performance measurements.

The tests distinguish actual decoded PCM, queued versus consumed samples, sample-exact initial offsets, shared PTS wrap, partial packets, known flash/beep pairs, frozen/slow output clocks, long pause/backpressure, video-only sources, early audio end and network recovery. Existing music decoding/output API tests remain active. **Physical movie sound and A/V synchronisation remain pending regardless of host test results.**

Recorded host evidence ([counters](native-av-metrics.json)): the continuous five-minute real Jellyfin transcode decoded/displayed **9,000/9,000 video frames**, decoded **12,502 MP3 frames**, consumed **14,401,536 PCM frames**, with **zero video drops, audio underruns, silence bridging, late-audio trims or timestamp corrections**. PS occupancy peaked at 524,287 bytes (512 KiB limit); maximum video presentation lateness against consumed audio was **11.271 ms**. Eight known flash/beep pairs passed the independent source-event check; maximum model error was **438 µs**. Injected interruption produced one reported audio underrun and recovered without concealment or late samples; the deliberately gapped/short-audio fixtures report their expected mapping/silence events. Native/sanitizer and existing real music/browse/auth tests pass. These are host/device-model measurements, not physical Xbox results.

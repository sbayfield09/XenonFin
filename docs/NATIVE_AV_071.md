# XenonFin 0.7.1 — A/V recovery and hardware diagnostics

Historical 0.7.1 record. For the current receive/rebuffer build and the user’s subsequent console findings, see [0.7.2](NATIVE_AV_072.md).

The user confirmed 0.6 silent Jellyfin video at approximately 30 FPS, then reported **severe stutter, intermittent soundtrack, approximately 18 FPS and a client-generated A/V error after 30–60 seconds in 0.7**. The changes below fix reproducible scheduling defects. **Uninterrupted, synchronised ten-minute playback on Xbox is still awaiting hardware acceptance.** Cloud tests cannot measure the console DMA engine, PowerPC decoder performance, HDMI sound or television latency.

## Cause established from code and regression tests

In 0.7, `jvideo.c` allowed audio recovery only when a GPU picture was ready **and all four video queue slots were occupied**, unless video EOF had arrived. Meanwhile `video.c:slack()` refused a decode when the next picture's deadline was closer than the estimated decoding cost plus 3 ms. If DMA drained just before that deadline, its clock froze: video could not decode ahead, audio could not restart without a full video queue, and neither could release the other. The unconditional eight-second “timestamps/preroll” watchdog then converted this application deadlock into ERROR even with healthy buffered HTTP data.

A deterministic regression freezes consumption **2 ms before a pending video deadline with an empty successor queue**. Compiling that test against published 0.7 (`d2286fc`) fails `calls==before+1`: no decoding occurs. It passes with 0.7.1. A second test exercises audio recovery with only a prepared picture and **zero** successor pictures; 0.7's full-queue gate could not admit it.

There was also a refill scheduling weakness: one MP3 frame was decoded and DMA serviced once before each main-loop video decode, controller/USB work and UI work. A 48 kHz MPEG-1 Layer III frame contains 1,152 samples, **24 ms**; an 8 KiB DMA reservoir contains **42.667 ms**. Those are finite service budgets, not permission to postpone audio until another video frame. The new diagnostics measure both MP3 cost and the maximum interval between audio services. Existing host tests without realistic non-preemptible video costs did not expose these dependencies.

These facts establish defects in the old code. The hardware report does not include the final queue/register snapshot, so it cannot prove that every observed audio cut or low-FPS interval had that same cause. Modes and traces below isolate any remaining DMA, CPU or multiplexing issue instead of assuming it is fixed.

## Changes

- Audio has a separate cooperative `xf_jvideo_service()`, invoked before networking, after video decoding, around controller/UI work and before the normal deadline wait. It never performs HTTP reads, video decoding or GPU waits. Low software PCM triggers at most **four MP3 frames / 6 ms per call**, checked between complete non-preemptible decoder calls; DMA is serviced between them. A single MP3 decode can exceed that budget and is measured, never interrupted unsafely. The existing 128 KiB PCM allocation, four YUV slots, 8 KiB DMA cap and all network/elementary ring capacities are unchanged.
- Recovery requires usable PCM and **one available video picture**, rather than a full queue. A drained, rebuffering audio master returns “held”; video deadline slack then allows decode-ahead while presentation remains held. Audio-master startup/recovery no longer also waits on the video-only full-queue gate. Video-only mode retains the original monotonic, full-preroll scheduler.
- Steady-state DMA refill does not require an occupied picture queue between video decode calls. Only actual video input buffering holds ongoing output; picture readiness is a separate startup/recovery gate. A regression test leaves 2 KiB queued and no pending picture, with valid video input, and checks that DMA is refilled to 8 KiB. The overly strict gate fails that test.
- At the requested 48 kHz rate, channel conversion emits the exact samples directly. It avoids two signed 64-bit interpolation divisions per sample on 32-bit PowerPC. Comparison against the previous decoder produced **byte-identical mono and stereo PCM**; other rates retain the existing resampler. No estimated CPU speedup is reported as a hardware measurement.
- The catch-all eight-second application A/V error is removed. Current wait cause and stage progress remain visible, and X/B remain usable. Actual HTTP idle/connect failures, malformed PES/MP3, missing required initial PTS, invalid DMA readings, GPU fence failure and unsupported formats retain specific errors. An unidentified local stall remains diagnosable and cancellable; it is not relabelled as a server disconnect. Stop cleanup retains its existing two-second DMA-drain safeguard.
- Final error origin, pre-cancellation HTTP state and PCM/DMA snapshot survive cleanup and repeated stop/section changes. Authentication secrets and request URLs are never exported.

The libXenon task-mailbox APIs (`xenon_run_thread_task`, `xenon_is_thread_task_running`) were inspected. They are bare hardware-thread dispatch facilities, not an automatically synchronised audio scheduler. lwIP raw callbacks, PS rings, scalar decoders and the sound write pointer currently have one owner. Keeping this cooperative arrangement avoids races and allocator/cache changes in the working application. No new thread or sleep in the audio path was introduced.

## States and clock contract

| State | Entry / exit |
| --- | --- |
| BUFFERING | Negotiation or HTTP headers pending; no PCM submitted. |
| PREROLL | Headers arrived; await required initial PTS, decoded PCM and one video picture. Audio-only mode needs no picture. |
| PLAYING | PCM submitted/consumed and video scheduled against consumption. |
| REBUFFERING | Unexpected DMA depletion or joint video starvation; held consumption position, decode/refill ahead, resume with at least 16 KiB software PCM and one picture (or genuine audio EOF/tail). The higher refill target is 32 KiB, providing hysteresis without changing allocation sizes. |
| PAUSED | User pause and DMA drained. Sample counter and image hold; resume through the same preroll gate. |
| ERROR | A specific transport, format, decoder, DMA or GPU fault; retain diagnostics while asynchronously stopping/reporting. |
| ENDED | Normal EOF or requested stop, followed by reporting and encoder cleanup. |

At pause request, submission stops; audio and video drain together for at most 42.667 ms, then hold. Resume never creates a second wall clock. The pinned sound backend has no public pause operation; no reset or guessed register write was added.

Clock units are **stereo output frames at 48,000 Hz**, four bytes per frame. Position is `max(previous_consumed, submitted_frames - queued_bytes / 4)`, converted with 64-bit arithmetic to microseconds. Empty compressed/software PCM cannot advance it. Device queue reads come from the existing `xenon_sound_get_unplayed()`, whose pinned implementation reads `0xea001604`: current descriptor bits 0–4, last-valid descriptor bits 8–12 and remaining bytes bits 16–31. Its modulo-32 descriptor arithmetic handles the 65,536-byte ring. The overlay additionally samples that register **read-only**, shows pointer wrap counts and preserves it in the CSV; clock computation still uses the public driver. Host tests check consumed versus submitted, partial/stale reads, empty-buffer holds, counters above 2^32 samples and stream-reset counters. Actual hardware register semantics remain subject to the comparison below.

PES PTS remain 90 kHz, unwrapped at 33 bits onto a common epoch; MPEG video PTS may reorder with B pictures. Video display order retains the tested constant-rate frame timeline. Audio timestamps map to the first MP3 frame beginning within each PES; later omitted timestamps continue sample counting, while a missing **initial** A/V anchor is an immediate, explicit parser error in audio modes. MPEG PTS and DTS header lengths remain distinguished. Early audio is trimmed, late audio receives mapped silence, and real discontinuities are counted/rejected according to the existing bounds. First **30 video and 30 audio PTS plus ES byte offsets**, and last valid A/V PTS, are retained for inspection. Sparse tail/gap bridging from 0.7 remains explicitly counted; bridging or late trimming is not evidence of uninterrupted original sound.

## GPU measurement

The reported **10.79 ms “poll” was the interval between GPU fence observations**, often containing the non-preemptible video decode. It was not 10.79 ms spent busy polling. `video_native.c:poll()` reads the fence once and returns pending; it already uses asynchronous private targets and retains textures until completion. That working backend is unchanged. The HUD now calls this **poll-gap**. `GPU<=` remains an upper bound on GPU completion latency, not a precise GPU timer. Required fence/cache synchronisation remains enabled. No texture still in use is overwritten. Refill maximum duration/gap, decoder time, upload/command/copy timings and sampled clock/DMA call costs distinguish real CPU service cost from fence observation latency.

## Exact hardware procedure

1. Copy [the ELF](../dist/xenonfin-xell-video.elf) as **xenon.elf** to your existing XeLL boot device. Verify **XENONFIN 0.7.1**. Keep [confirmed 0.6](https://github.com/sbayfield09/XenonFin/tree/ae879e810ea103e60fda50eefc351e3b5340dcf5/dist) for comparison. Initialise Ethernet and sign into your normal numeric IPv4 HTTP server, e.g. `192.168.68.202:8096`.
2. Select an audio-bearing movie/episode and start from details with A. **X** stops; wait for reporting/cleanup to finish. In the Video section, click the **left stick** while stopped to cycle **C → A → B → C**; the mode appears in the heading/page 14. **Y** replays the same selected item. Mode cannot change during playback or cleanup. The mode lasts in RAM until reboot.
3. **Mode A: video only.** Request the same MPEG-1/MP3 PS but discard audio, disable its decoder/output and use the 0.6 monotonic video clock. Verify moving pictures near 30 FPS, no soundtrack, stable controls and no new drops. This isolates the audio/sync regression without changing server output.
4. **Mode B: audio only.** Same PS and timestamp parser, discard video ES payload without decoding/rendering. Hear the actual soundtrack continuously; MP3/PCM/DMA counters must progress, while video decoded/presented counters stay zero. Test A/Start pause/resume. A genuinely video-only item is rejected in this mode.
5. **Mode C: both.** Verify actual movie sound, moving pictures, continuous sample consumption and sensible A/V phase. **A/Start** pauses/resumes; **X** stops; **B** returns to the existing library. Test a 20–30 second pause and a short network interruption, then verify recovery rather than rapid PLAYING/REBUFFERING oscillations. The normal HTTP idle timeout remains eight seconds; a longer cable outage can produce an actual transport timeout.
6. **Left/Right** selects **14 pages**, updated approximately four times per second. Pages 1–8 retain video/GPU/drop/UI/network timing. Page 9: MP3 format, software PCM, DMA/audio ES occupancy, underruns, gaps and PES counts. Page 10: consumed position, V−A phase, timestamp alignment/error and MP3 decode cost. Page 11: explicit state/wait/error origin, no-progress duration, refill average/max/gap and sampled DMA/clock maximum cost. Page 12: last HTTP read result (lwIP code), receive callback count, remote EOF, response flags, bytes/backpressure, raw descriptor/remaining/wrap. Page 13: first 30 PTS; Up/Down selects a timestamp pair during playback. Page 14: elapsed milliseconds since each pipeline stage progressed; after stopping it shows recent state transitions. A high RX age while backpressured is expected, whereas queued DMA with no consumed-sample progress indicates an output problem.
7. For a trace, keep the packaged **xenonfin-test.m1v** at the root of a FAT USB volume so the existing mount/path discovery can locate it. After stop/error and cleanup, **click the right stick** in Video. This writes **xenonfin-video.csv** and **xenonfin-av.csv** to that root. Save the files **before changing mode or starting another clip**. The AV file records the first PTS pairs, last 32 state transitions, numbered stage counters, error/HTTP termination snapshot, byte counts, last PES PTS, PCM/DMA occupancy, consumed/submitted frames and raw DMA pointer/wrap data. Stage numbers: 0 HTTP reception; 1 demux bytes; 2 compressed audio received; 3 MP3 frames; 4 produced PCM frames; 5 submitted PCM frames; 6 consumed PCM frames; 7 decoded video; 8 video scheduling clock; 9 presented video. Do not initialise or format storage for this test.
8. Play the fixture below in **Mode C for at least ten minutes**. Record audible cuts, real displayed FPS, drops, audio underruns, PCM/DMA levels, MP3 maximum decode cost, maximum refill gap, raw remaining bytes/pointer wraps and V−A phase near 0/30/60/300/600 seconds. Compare flash/beep onset using a recording of the actual display and sound output; also test familiar dialogue. Record console revision, display refresh, HDMI/analogue/receiver route, Jellyfin/FFmpeg version and any fixed output-chain latency. Count success only when actual console sound/pictures remain continuous and measured synchronisation does not drift. Finally retest MP3 music, login/library navigation and local CPU/GPU video.

Generate this **server-library source** and import it into Jellyfin; it is not an Xbox-local decoder clip:

```sh
ffmpeg -hide_banner -f lavfi \
  -i "color=c=black:s=720x414:r=30,drawbox=color=white:t=fill:enable='lt(mod(t,1),0.1)'" \
  -f lavfi -i "aevalsrc='if(lt(mod(t,1),0.05),0.4*sin(2*PI*1000*t),0)':s=48000" \
  -t 600 -c:v libx264 -preset veryfast -pix_fmt yuv420p \
  -c:a aac -b:a 128k 'XenonFin AV Recovery (2026).mp4'
```

Jellyfin configuration and documented playback/stream endpoints remain as described in [NATIVE_AV.md](NATIVE_AV.md): HTTP progressive MPEG-PS, MPEG-1 ≤720×480/30, MP3 ≤2 channels at requested 48 kHz, transcoding permissions/FFmpeg encoders, token authentication in headers. No changes to your personal server were made here.

## Reproduce build and checks

```sh
DEVKITXENON=/workspace/setup/devkit make xenon
make -C platform/xenon/tests all test
make -C platform/xenon/tests SANITIZE=1 all test
JELLYFIN_VIDEO_STREAM_PROBE=build/native-tests-sanitized/jvideo-probe \
  python3 tests/native_video_server.py --extended --metrics /tmp/av-recovery.json
python3 tests/native_video_server.py --long --long-seconds 600 \
  --case long-duration --metrics /tmp/av-long.json
python3 tests/native_video.py
python3 tests/native_server.py --music
```

Extended probes use production HTTP/PS/MP3/MPEG-1/player code with host TCP/output adapters. They include A/B/C, 11.88 ms injected non-preemptible video work, asynchronous 1.4 ms GPU completion and 2.88 ms copy work, forced DMA depletion, 20-second pause, five-second network interruption, slow device clock, short/delayed/gapped audio and known flash/beep alignment. Injected waits exist **only in the host test adapter**, never in the ELF's audio path. These are fault models, not a PowerPC benchmark. Original 0.7 evidence remains historical in [native-av-metrics.json](native-av-metrics.json); current 0.7.1 evidence is recorded in [native-av-recovery-metrics.json](native-av-recovery-metrics.json).


## Recorded 0.7.1 host results

[Recorded counters](native-av-recovery-metrics.json) contain all **15 ASan/UBSan real-server cases** plus the **600-second real-time host run**. The long run decoded/presented **18,001/18,001** frames, with **zero video drops, audio underruns, bridge silence, late audio trims or PTS gap events**. It received 198,105,088 MPEG-PS bytes, consumed 28,802,048 PCM frames and ended at 600.042666 seconds; PS storage peaked at 524,288 bytes of its 524,288-byte capacity. Maximum presentation lateness against the model's consumed audio was 5.645 ms. Progress/stop/encoding cleanup completed successfully.

The final sanitizer run displayed all 240 frames with zero audio underruns under injected console-like costs; maximum audio service gap was 30.682 ms. A deliberately injected 60 ms CPU interruption and a five-second network interruption each reported one audio underrun and recovered all frames. Eight independently detected flash/beep pairs had a maximum model error of **438 µs**. Audio-only mode decoded real soundtrack PCM with zero video frames; video-only mode displayed 240/240 without decoding audio. Format/PTS/DMA/error/export tests, existing real music/auth/library checks and native colour/decoder/renderer regressions passed. The final packaged source cross-built to the same PowerPC ELF bytes.

**These results are host models and build evidence. They do not establish actual Xbox soundtrack continuity, real GPU/PowerPC performance or physical A/V synchronisation. The ten-minute console test above remains required.**

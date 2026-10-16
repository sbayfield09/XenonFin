# XenonFin 0.7.4 — shared audio output and complete playback integration

The user confirmed correct sound from the **0.7.3 standalone PCM test on a real Xbox 360**. That confirms the basic output path, not movie/music continuity or A/V synchronisation in 0.7.4. This release completes the software integration and automated validation in one pass; there are no intermediate builds to accept. **Physical 0.7.4 movie playback remains untested here.**

## What was integrated

0.7.2 replaced the working SDK sound object and altered its control-register read/reset order. 0.7.3 removed that override in both ELFs, and its standalone test subsequently produced correct sound on hardware. The 0.7.3 main application already linked that original SDK object, but still had its own output callbacks and its music/movie integration had not been verified on console.

`audio_native.c` is now the **single shared adapter** for the standalone test, main application's sine diagnostic, MP3 music and MPEG-PS movie audio. Both programs initialise it in the same established order: display, console, USB, then the original SDK sound initialisation. It initialises once; track changes, pause/resume, stop and rebuffer never reset the device. No new MMIO control writes, cache implementation or descriptor code were introduced.

The unchanged pinned `libxenon.a(sound.o)` is the only hardware/DMA implementation. Its object SHA-256 is `9a569f3a768d93da6a5ea9e4d3091a12b62b9ea1a9f41d0d5fc2d16b713d9692`. Link-map/symbol inspection verifies its provenance in both ELFs, the shared adapter in both, and 256-byte-aligned DMA descriptors/ring inside the first 32 MiB. The original GPIO/HDMI/I2C configuration remains untouched.

The common output contract is **48,000 stereo sample frames/s, signed 16-bit little endian, L low/L high/R low/R high, four bytes per frame**. The adapter accepts only complete 2,048-byte descriptors. It validates queue/free-space bounds, caps outstanding data at 32 KiB and reserves a descriptor of free space before calling the SDK. The SDK synchronously copies caller bytes into its own static 64 KiB ring and performs its original cache stores. Scratch buffers can be reused after that return; hardware never points into decoder scratch or memory that is subsequently freed.

Submission now returns success/failure. Tone/music/movie counters advance only after acceptance; a rejected write is a clear error, not a fictitious submitted sample count or a retry that replays audio. Device faults latch without automatic resets. An extra watchdog detects **queued DMA with no consumption for two seconds**. Empty DMA during pause/rebuffer is exempt. Returning to XeLL waits for playback cleanup and DMA drain.

The standalone regression tool is retained as [xenonfin-xell-audio-test.elf](../dist/xenonfin-xell-audio-test.elf). Its waveform/format code is unchanged: verified 440/880/1000 Hz sines, silence, independent/split channels and 1/60-second tests. It uses exactly the same adapter as the integrated application, and remains free of network/codecs/video-player code.

## Music and movie data paths

Music retains its existing minimp3 decoder, sample-rate/channel conversion, authenticated HTTP streaming, browsing/queue, pause/resume, skip/advance and Jellyfin progress/stop/cleanup requests. Mono and 44.1 kHz sources are converted by that existing code to the common 48 kHz stereo S16LE output. The backend itself does not resample or reinterpret PCM. No second MP3 decoder was added.

Movie playback retains the production incremental MPEG-PS parser, separate bounded video/audio elementary streams and persistent minimp3 state across PES boundaries. Jellyfin negotiation still requests progressive MPEG-PS, MPEG-1 video up to 720×480/30 and MP3 audio at requested 48 kHz/max two channels. Unsupported source audio is transcoded by the server; an unexpected Layer II/unsupported/changing decoded format is rejected instead of being sent to the output device. Actual Layer III headers/sample format are checked; HTTP 200 alone is not a playback result.

The packet regression generates a real 20-second MPEG-1/MP3 program stream, fragments it into varying 1–997-byte reads, exercises production demux/decoder ring wraps, and compares **every elementary-stream byte** with FFmpeg's copy-demux output and **every PCM byte** with standalone native MP3 decoding. It covers 320,640 compressed bytes, 835 MP3 frames and 961,920 stereo output frames. The ELF never downloads an entire movie into memory.

## Consumed-audio master and timestamps

The clock uses the SDK's actual unplayed DMA byte count:

`completed_frames = submitted_frames - ceil(unplayed_bytes / 4)`

The displayed position is monotonic; partial stereo frames are not prematurely counted. Raw queue bounds, backward observations and stalls have explicit diagnostics. Wall time is not substituted for audio consumption. This is a **DMA-derived clock**, not a measurement of television/HDMI latency. It must still be checked against real hardware; invalid readings fail explicitly rather than silently switching to an estimated clock.

MPEG PTS remain 90 kHz, unwrapped across the shared 33-bit timeline by the existing demuxer. The initial media origin is the earlier of the first audio/video timestamps. **Audio preceding video is played intact**; video waits until consumed audio reaches video time zero. No initial PCM trimming is needed. If audio begins later, mapped leading silence represents that declared source interval. Video presentation follows this timestamp-adjusted consumed-audio master, using the existing scheduler and its controlled late-picture dropping policy.

Positive declared timestamp gaps retain sample-exact mapped silence. Backward timestamps outside the existing bound fail. An overlap with PCM already submitted now fails explicitly **without discarding real decoded samples**. No runtime pitch adjustment, PCM replay, duplicate submission, or PCM removal is used to catch a video deadline. Legacy late/trim trace counters remain zero for compatibility.

The existing explicitly counted silence bridge for multiplex head-of-line blockage/source audio tails is retained. It is not enabled merely because HTTP is waiting: the video ES must be full while the current video PES blocks access to later audio. Source gaps/tails and final descriptor padding are separately counted. Once video EOF is decoded, generated tail silence is capped at the known video end, with only final descriptor padding; actual decoded PCM is never truncated. A bridged/tail test is not evidence of uninterrupted original soundtrack. Normal continuous-source validation requires zero bridge/late-trim events. If later audio overlaps already-submitted media time, it errors rather than silently trimming that PCM.

## Buffering and cooperative servicing

| State | Behaviour |
|---|---|
| INITIAL_BUFFERING | Negotiate/validate stream, establish timestamps, prepare a picture and PCM preroll |
| PLAYING | Refill bounded PCM/DMA ahead; present video against consumed audio |
| PAUSED | Stop new submissions; queued audio and matching video drain together, then both clocks hold |
| REBUFFERING | Latch one drain event, retain compressed/PCM/partial-block state, hold after DMA drains |
| STOPPED | Explicit user stop; drain and report/clean up before releasing ownership |
| ERROR | Preserve diagnostics; stop producing PCM, drain/report/clean up without resetting hardware |
| ENDED | Natural stream completion followed by the same cleanup |

Initial startup requires 32 KiB PCM; a real recovery requires **64 KiB**, preventing a restart on the same nearly-empty threshold. Priority refill targets **64 KiB** inside the existing 128 KiB PCM ring. The movie DMA window is **32 KiB / 170.667 ms**, compared with the former 8 KiB / 42.667 ms. The first ten-minute 16 KiB trial completed every video frame but recorded two audio underruns at service gaps up to 102.907 ms. The final 32 KiB policy is the existing music window and covers that measured gap without enlarging the SDK ring. The initial trial is retained in the evidence record rather than labelled uninterrupted. Music retains its established 32 KiB maximum and the diagnostic uses 8 KiB. All queues remain bounded: PS 512 KiB, audio ES 128 KiB, PCM 128 KiB, video ES 256 KiB and the existing four-frame video queue.

Audio service runs before networking, after video decode and around controller/text work. Each call decodes at most four MP3 frames and checks a 6 ms budget between complete frames; no audio submission sleeps or waits on HTTP/GPU. Networking retains bounded RX batches and receive-window recovery. GPU work remains asynchronous. A codec call itself cannot be preempted by this cooperative design; a stall beyond DMA coverage must be counted/recovered, not hidden. Tests cover both a tolerated 60 ms interruption and a 250 ms interruption requiring recovery.

LibXenon's bare hardware-thread task/mailbox APIs were considered, not assumed to be POSIX threads. The existing raw-lwIP callbacks, PS/PCM rings, decoder state and SDK write pointer have one owner. This release preserves that ownership rather than introducing unverified cache handoffs or concurrent driver calls. The increased bounded device coverage plus priority service addresses the measured gap without a new thread or an audio-path delay.

Ordinary starvation never clears decoder buffers, rewinds timestamps or resets DMA. A held master explicitly permits existing video decode-ahead, so recovery needs one usable picture instead of a full-picture-queue deadlock. An initial-preroll stall without MP3/PTS progress has its own 20-second error; it is not mislabelled as an HTTP failure. HTTP connect/idle deadlines remain eight seconds, control/header deadlines 20 seconds, with capacity-blocked streaming pause excluded as before.

## Diagnostics

Left/Right cycles **17 pages** in Video. Existing video timings/FPS/drops remain. Useful audio pages:

- **9:** actual MP3 format, PCM/audio ES fill, DMA queued/limit, underruns, gap and PES counts.
- **10:** timestamp-adjusted consumed-audio position, V−A drift, initial PTS offset and MP3 decode time.
- **11:** playback state/wait reason, refill timing and maximum service gap.
- **12:** HTTP result/EOF/backpressure plus raw DMA pointer/remaining/wrap information.
- **15:** retained last rebuffer reason, buffer levels, hold/service/submission intervals.
- **16:** TCP window, refused packet, timeout age and RX batching.
- **17:** shared output overruns/counter faults, MP3 errors/resync bytes and last playback error.

Device counters/pointer wraps are lifetime values since boot; player submitted/consumed frames are per playback session. CSV adds output errors and decoder counters. Tokens/passwords/full authenticated URLs are never rendered or exported. After stop/error and cleanup, right-stick click saves existing video/A/V CSVs to the discovered FAT USB root; retain the packaged local test clip there for the existing mount/path discovery.

## Completed software results

Sequential native tests and ASan/UBSan tests passed, followed by packet reconstruction, real-server music checks and all 16 real-server A/V cases. The final ten-minute host run decoded and displayed **18,001/18,001 pictures**, with **zero video drops, audio underruns, rebuffer events, silence bridges or PCM trims**. It consumed 28,803,072 stereo output frames and completed Jellyfin reports/transcoder cleanup. The maximum measured audio service gap was 117.948 ms, below the 170.667 ms device window. These are host measurements with the shared adapter's SDK boundary simulated, not console benchmarks.

The deliberate 250 ms CPU stall exceeded that window: it caused one counted audio underrun/rebuffer and two controlled video drops, then recovered without PCM trimming. The five-second network interruption also recovered. The 60 ms stall and 20-second pause tests had no audio underruns. Initial 16 KiB trial failures remain recorded separately. Physical pitch, HDMI latency, uninterrupted soundtrack and synchronisation are acceptance checks for the single final ELF below.

## Single final hardware test session

1. Download [the integrated ELF](../dist/xenonfin-xell-video.elf), name it **xenon.elf** on the existing XeLL boot device and cold-boot. Verify **XENONFIN 0.7.4**. The boot archive's default xenon.elf is the full application again; the isolated audio ELF is optional regression tooling.
2. On Audio output, A runs the verified sine through the shared adapter. X selects waveform/channel tests; Y selects 1/60 seconds. This is a quick comparison with the already confirmed standalone tone, using this one final build.
3. Initialise Ethernet and sign in, for example using the configured `192.168.68.202:8096`. Browse an album, play a real MP3, listen for correct pitch/continuity, pause/resume, change tracks, stop and return to Libraries. PCM/DMA counters alone do not prove sound.
4. Select a movie/episode, open details and A to play in **mode C (both)**. Hear the actual soundtrack with moving pictures. Check dialogue or the known flash/beep fixture from NATIVE_AV_071.md. A/Start pauses/resumes; queued audio/video drain together within about 171 ms before holding. Test a 20-second pause and resume.
5. Play a continuous source for **at least ten minutes**. Aim for approximately 30 FPS, zero sustained drops/underruns/rebuffering, stable V−A phase and bounded buffers. Record pages 9–11/15/17 near start/end. Only a recording of actual console sound/display can establish physical synchronisation and drift.
6. Optionally interrupt Ethernet for approximately five seconds and verify rebuffer/recovery without PCM replay or resets. An outage over eight seconds may correctly terminate with the unchanged HTTP idle timeout. Save diagnostics before replaying if any failure occurs.
7. X stops and completes Jellyfin cleanup; B returns to the browser. Play MP3 or the tone again, verifying device reuse without reinitialisation. Retest local CPU/GPU video. While stopped, left-stick click selects mode A video-only or mode B movie-audio-only; Y replays. These comparison paths remain available.

This is one completed software build for final hardware testing. No intermediate builds or approvals are needed. If any hardware failure remains, preserve the A/V CSV and relevant diagnostic pages rather than guessing channel swaps or changing the tone frequency.

## Build and sequential verification

```sh
DEVKITXENON=/workspace/setup/devkit make xenon
make -C platform/xenon/tests all test
make -C platform/xenon/tests SANITIZE=1 all test
XENONFIN_AUDIO_PS_PROBE=build/native-tests-sanitized/audio-ps-probe \
  XENONFIN_DECODE_PROBE=build/native-tests-sanitized/decode-probe python3 tests/native_audio_packets.py
JELLYFIN_MUSIC_PROBE=build/native-tests-sanitized/music-probe python3 tests/native_server.py --music
JELLYFIN_VIDEO_STREAM_PROBE=build/native-tests-sanitized/jvideo-probe \
  python3 tests/native_video_server.py --extended --metrics /tmp/av-074.json
JELLYFIN_VIDEO_STREAM_PROBE=build/native-tests-sanitized/jvideo-probe \
  python3 tests/native_video_server.py --long --long-seconds 600 --case long-duration --metrics /tmp/av-074-long.json
DEVKITXENON=/workspace/setup/devkit python3 scripts/check_native_audio.py
```

The real-server music/movie probes now use **the production shared adapter**, with only the original SDK/device boundary simulated on the host. Decoder, parser, API, clock, buffering and controller-state actions are production C. The MPEG-1 decoder, colour conversion, GPU texture/rendering, framebuffer presenter, video frame scheduler, HTTP transport and MPEG-PS parser sources are unchanged. [Recorded 0.7.4 test results](native-av-074-metrics.json) distinguish host evidence from the user's earlier standalone hardware confirmation. Successful compilation/HTTP status or host flash/beep measurements are not claims of physical movie playback.

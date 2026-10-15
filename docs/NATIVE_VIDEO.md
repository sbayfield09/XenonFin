# XenonFin 0.5.2 — frame timing and drop diagnosis

The user confirmed corrected CPU colours and working MPEG-1 decoding/GPU rendering on a real Xbox 360 in 0.5.1. At 720×480/30 FPS the reported result was 240 decoded, 218 presented, 22 dropped, 27.23 average FPS, approximately correct clip duration; decode 10.54 ms, upload 1.34 ms, GPU observation approximately 1.39 ms, copy 2.88 ms, VSync off. **0.5.2 fixes verified software defects and adds diagnostics; sustained 30 FPS and its new VBlank modes still require console testing.** Jellyfin login/browsing, MP3/network streaming, audio, controller and the working text/video graphics paths remain in place.

## Downloads and first hardware comparison

- [XeLL-compatible PowerPC ELF](../dist/xenonfin-xell-video.elf)
- [Boot archive, containing xenon.elf and the exact test filename](../dist/xenonfin-xbox-boot-diagnostic.tar.gz)
- [320×240, 640×360 and 720×480, eight-second 30 FPS clips](../dist/xenonfin-video-benchmarks.tar.gz)
- [Complete source archive](../dist/xenonfin-source.tar.gz), checksums in `dist/SHA256SUMS`

Extracting the archive preserves `.m1v` if a browser renames a raw download `.mpeg`. Copy the ELF as **xenon.elf** and one profile's clip as **xenonfin-test.m1v** to the FAT32 USB root. Keep the USB inserted. Boot through the established XeLL procedure and check the title **0.5.2**. Keep the previous build from commit `4386dcf` for a direct comparison, and the unchanged [0.4.2 music rollback](../dist/xenonfin-xell-music-fix.elf).

1. Open **Local video test** with LB/RB. Stop music first. Initially use **sync 0/off**, **late 100 ms**, the default settings shown on statistics page 1. Start the supplied 720×480 clip using **Y** to repeat the reported GPU workload. A starts the CPU path; Up/Down shows CPU/GPU known colour bars from STOP/END.
2. Let all eight seconds finish without pausing or navigating. Target approximately 30 FPS, 240 presented, minimal/zero late drops, and a small final phase error. Record **all six statistics pages** using Left/Right. The final frame and totals remain at END.
3. Press the **right stick (R3)** to save **xenonfin-video.csv** to the USB root. Copy it to your development computer and give it a distinct name for each run. Saving deliberately overwrites that diagnostic file and is permitted only with video/music stopped. No file logging or serial output is performed by the application UI during playback.
4. While stopped, **LT** cycles the *next run's* lateness setting: 100 → 200 → **0/never drop late** → 66.667 → 100 ms. Run sync-off once at 0 to test whether every frame can be shown without accumulating drift or extending the clip. A zero drop counter with a slow clock/long clip is not success. Then compare 66.667, 100 and 200 ms if there are still outliers. Options do not reset the completed run's measurements or CSV metadata.
5. On a **60 Hz display**, use **RT** while stopped to cycle the next run's sync mode **0/off → 1/on → 2/every second refresh → off**. Replay the same clip and save a separate CSV after each mode. Record sampled refresh, VSync fault/halfReject, interval statistics, phase error and drops. A fallback is not a successful VSync measurement. Modes and caveats are explained below.
6. Repeat at 320×240 first if 720×480 remains inconsistent, then 640×360. Each supplied clip has 240 frames at 30 FPS. Record console model, cable, actual display mode/refresh and ELF revision. Test pause/resume separately with A/Start; X stops, B returns to Libraries. Verify Jellyfin login/library navigation and audible MP3 playback, pause/skip/stop and reports after the video runs.

| Source, 30 FPS | Sync | FPS / dec / shown / reasoned drops | CPU / preparation max and p95 | UI / interval max and p95 | Phase / VWait / refresh / faults |
|---|---|---|---|---|---|
| 320×240 | off / on / half at 60 Hz | Hardware pending | Pending | Pending | Pending |
| 640×360 | off / on / half at 60 Hz | Hardware pending | Pending | Pending | Pending |
| 720×480 | off / on / half at 60 Hz | Hardware pending | Pending | Pending | Pending |

## Two verified defects in 0.5.1

**Timer scaling:** the pinned `ppc/timebase.h` defines `PPC_TIMEBASE_FREQ = 3192000000 / 64 = 49,875,000 Hz`. The old expression divided the timebase by `PPC_TIMEBASE_FREQ / 1000000`, which integer-truncated to **49**, rather than 49.875. One real second therefore became **1,017,857 microseconds**, a clock approximately **1.7857% fast**. It made deadlines early and reported durations slightly high. The coherent high/low/high timebase read was already correct and is retained. `xf_ticks_to_us()` now uses quotient/remainder scaling with the complete frequency; it avoids multiplication overflow for long uptimes. Tests cover this exact frequency and 100 days of uptime. This uses libXenon's nominal hardware frequency, not an independent oscillator calibration.

**Blocking diagnostic output:** the pinned `drivers/newlib/newlib.c::vfs_console_write()` calls the framebuffer hook, then sends **every byte to UART `putch()`**. `drivers/xenon_uart/xenon_uart.c::putch()` waits for transmitter readiness. Capturing `stdout_hook` did not bypass those waits. Four-Hz multi-line UI output could therefore interrupt presentation outside the previously reported per-frame timings. The application now calls `xf_console_printf()`: inside an active composed text frame it formats into a bounded 2 KiB buffer and feeds the character compositor directly. Outside a frame it retains normal vprintf/serial behavior. Other driver logging is unchanged. Host tests model the serial path and verify that composed application text never enters it. The existing main-page format strings fit the bound; library metadata still uses its existing bounded character/wrap helpers.

These defects are established by source inspection and tests. **They do not prove a complete allocation of the 22 hardware drops to individual causes.** Old totals lacked per-drop timestamps, processing tails and UI latency; the next hardware CSV establishes remaining causes.

## Absolute timeline and scheduling

The playback clock is computed as **origin PTS + (current monotonic time − origin time − accumulated user pauses)**. It is not advanced by integer frame durations, accumulated sleep requests or loop counters. The origin is set after the first publication, after file loading, GPU initialization and preroll; those initial costs never advance playback. Pauses during preroll are also excluded. Backward clock samples are counted and clamped rather than causing an unsigned jump.

MPEG-1 rate codes are mapped to exact rational rates: 24000/1001, 24/1, 25/1, 30000/1001, 30/1. Each output PTS is independently calculated from its frame index and rate, rounded once to microseconds. At 30 FPS the sequence is 0, 33333, 66667, 100000 …; there is no accumulation of rounded 33 ms or 33333 µs durations. The wrapper no longer uses pl_mpeg's rounded `29.970`/`23.976` decimal rate for timestamps. Decoded and presented PTS are checked for strict ordering, including reordered I/P/B clips.

The queue stays at **four decoded slots**, plus one private prepared picture. There is no blind buffer-size increase or new decoder thread/toolchain. `xf_video_service()` performs presentation-only work and never invokes decoding or waits for hardware. The main loop services it before and after controller/UI work. `xf_video_tick()` separately performs at most one cooperative decode call when there is queue capacity and sufficient deadline slack. A rolling decode-cost estimate plus a 3 ms margin avoids starting a non-preemptible decode near a ready picture's deadline. The estimate is capped so a single large I-frame cannot prevent all future decoding; actual spikes remain visible in max/p95 statistics. Decoder calls cannot themselves be preempted by this bare-metal application.

UI timer redraws defer near a video deadline using measured UI p95 plus a margin; controller-triggered updates remain immediate. Statistics retain approximately four-Hz cadence and complete-frame composition. The working changed-cache-line presenter, colours, cache operations, planar texture upload, reused textures/private GPU resolve and framebuffer copy are preserved.

The next wait is calculated from the absolute deadline **after work has consumed time**. Due presentation/decode work returns a zero wait. Early frames wait only toward their deadline, with cooperative polling capped at 4 ms (0.5 ms when sampling VBlank); asynchronous GPU waits use bounded polling. There is no compulsory extra sleep after each displayed frame, sub-millisecond deadline spin, or new audio-path sleep. libXenon's `udelay()` is a low-priority hardware-thread timebase wait, not an OS idle/scheduler API; none is claimed.

Late dropping defaults to **100 ms** and can be disabled or changed with LT. A picture is discarded only when **strictly later than the selected threshold and a newer queued picture is already due**. Queue fullness or a few milliseconds of lateness never triggers a drop. Reference pictures continue decoding, the final picture is published, and EOF drains queued/private pictures before END. No clock rebasing or slowing is used to conceal lateness.

## Drop accounting, traces and timing definitions

Six pages share FPS, decoded, presented and total dropped counters. Left/Right selects the remaining information:

| Page | Diagnostics |
|---|---|
| 1 | Decode, conversion, upload, command submission, GPU completion observation bound/poll uncertainty, framebuffer copy, VSync gate wait, queue/peak/private preparation, next-run settings. |
| 2 | Drops by **late, overflow, decode, GPU, EOS**; decode-failure/GPU-stall/EOF events; full-queue backpressure; average/p95/maximum dropped-frame lateness. |
| 3 | Absolute playback clock, last presented PTS, signed **drift/phase error**, backward samples; presented-frame lateness average/p95/max; requested sync, sampled refresh, sync faults and half-rate rejection. |
| 4 | Frame preparation span upper bound average/p95/max; CPU work per frame average/p95/max; actual completed-publication interval average/p95/max. |
| 5 | Last three dropped-frame records (reason 0=late, 1=overflow, 2=decode, 3=GPU, 4=EOS), PTS, lateness and work; trace/cancel/starvation counts. Full records are in CSV. |
| 6 | UI and main-loop work average/p95/max, average cooperative wait, queue starvation/backpressure and cancelled pictures. |

- **FPS** counts completed framebuffer publications; it does not independently count physical panel scanouts. Average FPS excludes startup and pauses. Publication interval statistics use monotonic wall time; a pause appears as a long interval, hence uninterrupted benchmark instructions.
- **CPU/frame** sums measured decoding, queue copies, submit/poll calls and framebuffer publication for a picture. It excludes intentional waits, queue residence and asynchronous GPU execution.
- **Proc<=** adds decode/queue copy to elapsed preparation until observed readiness and final framebuffer copy. It excludes intentional PTS/VSync holding and queue residence, but includes asynchronous completion polling uncertainty and can include overlapping decode/UI work. It is a conservative preparation-span measure, not a GPU hardware timestamp or total CPU utilisation. For a picture discarded before preparation completes, records describe work completed so far.
- **p95** uses an all-run histogram with **0.25 ms upper-edge bins**, not a small rolling sample. Maxima are exact timer measurements. If p95 falls in the histogram's ≥255.75 ms overflow bin, it reports the observed maximum rather than silently capping it. CPU/frame, preparation span, lateness, drop lateness, interval, UI and loop tails are recorded without allocation.
- **Drift** is completed-publication clock minus its source PTS. It measures presentation phase/lag, **not independently calibrated hardware oscillator drift**. Comparing phase across the run, frame intervals and an external stopwatch distinguishes jitter from sustained lag. The final source PTS of 240 frames at 30 FPS is 7.966667 seconds; the last picture remains held.
- **GPU<=/poll** retain the fence observation bound and read-bracket uncertainty. GPU, VSync gate waiting, UI and timebase waiting are separate metrics. No precise hardware GPU query is claimed.
- **Overflow drops remain zero by design:** the producer stops at four slots; `Qfull` counts backpressure, not dropped pictures. Decode/GPU fault events are separate from pictures discarded because of those faults. A failure attributes already decoded queued/private pictures to its reason; an incomplete failed decode does not increment decoded. Normal EOF records one event and drains pictures, so EOS drops are zero. User stop records cancelled pictures separately, outside performance-drop totals.
- The RAM trace retains the last **4096 publication/drop records**, with explicit overwritten-record counts; all events fit for the supplied 240-frame tests. Every retained drop contains its source PTS, absolute timeline and measured lateness. CSV also includes work, preparation span, occupancy, settings, reason counts, faults, phase and percentile summaries. There is no file/serial log per frame. Tests exercise trace wrapping with 5000 pictures and accounting `decoded = presented + dropped + queued + pending + cancelled` as applicable.

## VSync comparison without blocking or double-waiting

The native backend supplies one **read-only** VBlank sample using the same `0x6534 & 0x1000` status used by libXenon's `Xe_IsVBlank()`. It does not initialize Xenos for a CPU test or wait for an acknowledgement. The generic service detects observed rising edges and estimates refresh from a median of valid sampled 12–25 ms intervals. Missed-edge multiples are excluded; it is a sampled estimate, not an exact display-controller timestamp.

- **0/off:** publish a ready picture when its PTS is due. No VBlank status reads, retrace wait or scanout swaps.
- **1/on:** after PTS is due, publish on an observed active VBlank, at most once per observed blank.
- **2/half:** calibrate at least four valid intervals, then require two observed refresh edges between publications. Intended for approximately 60 Hz (15–18.5 ms estimated periods). A detected different refresh sets **halfReject** and explicitly uses mode-1 gating. It does not call a 50 Hz half-rate run “30 FPS”.

If the gate cannot observe sufficient retrace progress for **100 ms**, it increments the sync fault count and falls back to timestamp-only mode for that run. UI/controller service continues and no acknowledgement can permanently freeze rendering. A decoder/UI operation can still make a narrow blank pulse unobservable; fault, interval, refresh and trace data make that limitation visible. Confirm zero sync faults/halfReject before comparing successful modes. First-frame calibration is part of startup, outside the playback clock.

**This is VBlank-start copy gating, not a hardware page flip.** The stable UI uses the original scanout, and a roughly 2.88 ms video copy can exceed a blanking window. Therefore enabled gating does not promise tear-free scanout. No second VSync wait exists inside framebuffer copy or GPU resolve. No display-address swaps or the previous failed flip-acknowledgement path are reintroduced. These modes build and pass simulated retrace tests; their real sampling reliability and cadence require the physical comparison above.

## Preserved format and renderer limits

CPU pixels retain the verified PowerPC word **0xBBGGRR00**, bytes B,G,R,X, following pinned libXenon `console_pset()`. Y, named Cb/Cr pointers, padded/compact strides, 4:2:0 indexing and BT.601 limited-range coefficients are unchanged. Known RGB patches through actual MPEG decoding/tiled conversion still compare to FFmpeg within mean 0.487/255 and max 3/255. GPU textures are reused and no CPU RGB conversion is introduced on that path. Private staging, aligned tile-band copies, cache stores/invalidations and completed-glyph UI publication remain unchanged.

Input is local **MPEG-1 elementary video**, 144 bytes–8 MiB, even dimensions through 720×480, 23.976–30 FPS, compressed picture ≤128 KiB, unchanged geometry/rate/aspect. No video audio, Jellyfin video, artwork, MPEG-2/H.264/MP4 or program streams are added. The actual libfat USB mounts remain uda/udb/udc with `usb2mass_ops_0/1/2`; no FATX/HDD assumption. Keep USB inserted: upstream mount/read/write calls can block. CSV writes occur only after stopping audio/video.

Opt-in GPU initialization still uses upstream `Xe_Init()`/`edram_init()` hardware waits/fatal allocation paths; an initialization failure can require reboot. Per-frame GPU completion retains its 250 ms watchdog and private-resource retention on failure. The new VBlank gate has an independent recoverable timeout. The decoder arena remains 2 MiB, queue remains approximately 2 MiB maximum, and linked static memory remains below 30 MiB, including bounded trace/histograms.

## Generate, build and validation

`make video-benchmarks` generates the three 240-frame clips. For 320×240:

```sh
ffmpeg -hide_banner -loglevel error \
  -f lavfi -i testsrc2=size=320x240:rate=30 -t 8 -an \
  -c:v mpeg1video -bf 0 -g 12 -q:v 5 -pix_fmt yuv420p \
  -aspect 4:3 -f mpeg1video xenonfin-test.m1v
```

Use `size=640x360:rate=30`, `-aspect 16:9` for 640×360, or `size=720x480:rate=30`, `-aspect 3:2` for the square-pixel 720×480 benchmark. Output scales to the existing aspect-fitted maximum 640×384 canvas. Renaming a program stream does not convert it to elementary video.

```sh
DEVKITXENON=/workspace/setup/devkit make xenon
make native-test
python3 tests/native_video.py
make -C platform/xenon/tests SANITIZE=1 all test
UBSAN_OPTIONS=halt_on_error=1 \
  JELLYFIN_VIDEO_PROBE=$PWD/build/native-tests-sanitized/video-probe python3 tests/native_video.py
python3 tests/native_server.py --music
make video-test-file video-benchmarks
bash scripts/package.sh
```

Host tests include the exact old timer regression, fractional-rate PTS, priming/pause/rollover, ordered deadlines, configurable/disabled late drops, reason accounting, 4096-record wrapping, CSV, bounded waiting, simulated 60 Hz gating/half-rate and 50 Hz rejection, stuck-signal fallback, serial-free UI capture, textures and cache-line UI preservation. A model with 10.54 ms decode plus I-frame spikes, 1.34 ms upload, 1.39 ms asynchronous completion, 2.88 ms copy and 20 ms UI stalls at 4 Hz publishes all 240 frames without drops at the expected duration. This is host logic evidence, not a measured Xbox result.

Real codec comparisons retain strict FFmpeg YUV tolerances through 720×480/30, including reordered 29.97 FPS and colour fixtures, normally and under ASan/UBSan. Real Jellyfin authentication, library navigation and MP3 streaming/output/reporting integration passes. The actual ELF is cross-built using the existing toolchain and packaged with source. **Physical 0.5.2 frame rate, tails, retrace behavior and remaining-drop attribution are pending.**

## Provenance

MIT `pl_mpeg` remains pinned to `c871f2be022ece7ef4f64230b4fb8e1fb9eb6023`, with the existing three documented EOF/fixed-memory patches; no new upstream edits in 0.5.2. Exact rational output timing lives in the XenonFin wrapper. The GPL XMPlayer shader arrays remain pinned to `5fc2fb330bfb58bd2806fed6c63f6ae917cb3a3c`; full MPlayer/FFmpeg ports were investigated, not transplanted. See [THIRD_PARTY.md](THIRD_PARTY.md) and vendored `SOURCE.md` for hashes/notices. The original native toolchain and XeLL ELF conversion remain unchanged.

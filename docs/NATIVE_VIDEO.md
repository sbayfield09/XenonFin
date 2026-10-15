# XenonFin 0.5.1 — video colour and timing hardware test

The user tested 0.5 on a real Xbox 360: local MPEG-1 decoding and both CPU and Xenos video paths display 320×240/30 video. CPU colours are incorrect. Reported decode time is 3.1–3.4 ms, old aggregate presentation time 17–18 ms, rendered FPS 15–25, with drops of 18/41 and 36/234. **The 0.5.1 changes below compile and pass host tests; their colour correctness and sustained frame rate still require physical Xbox verification.** Existing Jellyfin login/browsing, HTTP, MP3 decoding/streaming/output and the text flicker fix remain in place. This test has no video audio or Jellyfin video streaming.

## Download and hardware test

Download [XeLL ELF](../dist/xenonfin-xell-video.elf), [boot archive](../dist/xenonfin-xbox-boot-diagnostic.tar.gz), and [three benchmark clips](../dist/xenonfin-video-benchmarks.tar.gz). Check `dist/SHA256SUMS`. Extracting the archives preserves the exact `.m1v` filename if a browser saves a raw MPEG download with a `.mpeg` extension. The unchanged 0.5 build remains available in Git commit `d65c5ef`; [0.4.2](../dist/xenonfin-xell-music-fix.elf) remains the stable music rollback.

1. Copy the new ELF to FAT32 USB as **xenon.elf**, boot through XeLL, and check the title **0.5.1**. Open **Local video test** using LB/RB. Stop music first. An active music/PCM or HTTP request prevents starting video.
2. With the player stopped, press **Up** for synthetic CPU colour bars. No file is needed. Expected order is **black, white, red, green, blue, cyan, magenta, yellow**. These are known BT.601 YUV vectors passed through the normal queue, conversion, tiled staging and framebuffer copy. Check solid colours, black/white levels and borders. **Down** repeats the identical test through the GPU shader. Bars remain visible at END; X stops. Record both results, especially red/blue and green.
3. Extract the benchmark archive. Copy **320x240/xenonfin-test.m1v** to USB root as **xenonfin-test.m1v**. Keep the USB inserted. Press **A** for CPU playback, or **Y** from STOP/END for Xenos. Each clip is eight seconds, 30 FPS, 240 silent frames. Check actual moving video, colours, aspect and timing. Compare both paths using the same clip. Do not judge playback solely from increasing counters.
4. **A/Start** pauses/resumes, **X** stops, **B** returns to Libraries; LB/RB also stops before changing section. Pause freezes publication and the playback clock. An in-flight GPU preparation may finish privately during pause. Resume excludes the paused interval. Confirm stopping and returning leave text stable.
5. **Left/Right** switches the two diagnostic pages. Record both pages at END, after a complete uninterrupted run. Repeat the 320×240 test several times; aim for approximately 30 FPS with zero sustained drops before advancing. “Shown” counts completed framebuffer publications, not independently observed panel refreshes.
6. Only after the first result is recorded, replace the root file with **640x360/xenonfin-test.m1v**, then **720x480/xenonfin-test.m1v**, and repeat CPU/GPU tests. These are increased test limits, not promised console performance. The source can be 720×480 while the reserved aspect-fitted output canvas remains at most 640×384.
7. Return to Libraries, verify existing authentication/navigation, stream an MP3, exercise pause/skip/stop, and check audible output and playback reports. No music/network implementation or audio initialization changes were made.

Record console model, cable, **actual display mode/refresh rate** from the display, ELF revision, renderer and source dimensions. A 50 Hz display cannot show 30 FPS with even refresh cadence; 60 Hz can repeat each source frame twice. This presenter uses timestamped writes into the existing scanout, with **no VSync wait and no framebuffer swaps**. It makes no assumption that display refresh is 60 Hz, and does not claim tear-free publication or measure panel refresh. A changing register or a VSync acknowledgement cannot freeze the menu.

| Source (30 FPS) | Renderer | Average/last FPS | Decoded/shown/dropped | Decode / conversion ms | Upload / commands ms | GPU bound / poll uncertainty ms | Copy / UI / wait ms | Queue peak / starvation |
|---|---|---|---|---|---|---|---|---|
| 320×240 | CPU, then GPU | Hardware pending | Pending | Pending | Pending | Pending | Pending | Pending |
| 640×360 | CPU, then GPU | Hardware pending | Pending | Pending | Pending | Pending | Pending | Pending |
| 720×480 | CPU, then GPU | Hardware pending | Pending | Pending | Pending | Pending | Pending | Pending |

**GPU initialization limitation:** the existing upstream `Xe_Init()`/`edram_init()` can wait on hardware or abort on allocation failure. Initialization is still opt-in (Y or Down) and can require a reboot if it fails. After initialization, scratch-writeback completion is polled cooperatively with a 250 ms watchdog. On timeout, private resources are retained, the test stops, and CPU/menu operation remains available. Reboot before retrying a failed GPU. FAT mount/read calls can still block inside upstream USB drivers; keep the drive inserted.

## Verified colour correction

The pinned libXenon `drivers/console/console.c`, `console_pset()`, writes `(b<<24)+(g<<16)+(r<<8)` into the same original framebuffer used here. **On big-endian Xenon this is the word `0xBBGGRR00`, with memory bytes B,G,R,X.** `XE_FMT_BGRA|XE_FMT_8888` is a component/memory format name; it does not mean a CPU-native `0xAARRGGBB` word. Version 0.5 wrote `0xFFRRGGBB`, giving incorrect component positions. White alone would not expose the error. `xf_video_bgra()` now follows the console's proven packing exactly; black is zero. UI colours/format are untouched.

The plane audit follows pointers rather than assumed storage order: pl_mpeg internally stores Y, Cr, Cb, but exposes named `frame->y`, `frame->cb`, `frame->cr` fields. `video_decode.c` uses those named pointers and padded plane widths. The queue copies active rows into Y, Cb, Cr regions and gives explicit compact strides. CPU chroma indexing uses `sx/2,sy/2`; GPU textures receive Y/Cb/Cr in that order with each surface's actual byte pitch. No U/V swap was necessary.

The coefficients remain BT.601 limited range for SD MPEG-1: subtract 16 from Y and 128 from Cb/Cr; use `298*Y + 409*Cr` for red, `298*Y - 100*Cb - 208*Cr` for green, `298*Y + 516*Cb` for blue, round/shift by eight and saturate. Reference red is Y/Cb/Cr 81/90/240, green 145/54/34, blue 41/240/110. Tests cover all eight bars, poisoned padding, scaling and explicit emulation of PowerPC stores on the little-endian host. A compressed RGB-patch fixture goes through the actual MPEG decoder, CPU tiled converter and unpacked PPC bytes; comparison to FFmpeg RGB24 gives mean error **0.487/255**, maximum **3/255**.

Staging stays private and 128-byte aligned. Finished 32-row tile bands are copied to tile-aligned destinations, cache-stored and ordered with `sync`. GPU targets are invalidated only after the resolve fence completes. YUV upload cache maintenance is handled by the existing surface lock/unlock API. No audio/NIC cache operations or display-address registers are changed.

CPU conversion also avoids repeated divisions/conversion for every scaled pixel: source-column mapping is computed once and an RGB scanline is converted once per source row, then reused during scaling. This is a measured-stage optimisation to validate on hardware, not an assumed frame-rate result.

## Scheduling and presentation diagnosis

The old player started conversion/upload only when a frame was already due. GPU readiness was observed around a fixed **10 ms** main-loop delay. Its single “present” measurement included command construction, asynchronous work, polling delay and final framebuffer copy; **it was not a GPU execution or VSync measurement**. The two-slot queue had little decoded-frame headroom. Separately, each changing UI frame compared whole multi-megabyte CPU surfaces, even though only statistics glyphs changed. These are confirmed code-level costs; their exact share of the reported 17–18 ms and hardware drops cannot be determined from those aggregate numbers alone.

Version 0.5.1 separates three backend operations:

- `submit`: convert into private CPU memory, or upload planar textures and submit a private GPU resolve **ahead of its PTS**.
- `poll`: observe private readiness without touching scanout. GPU completion is asynchronous; the watchdog does not spin.
- `present`: copy the complete ready picture only when its timestamp is due.

The player uses the existing **64-bit monotonic microsecond clock**, sampled again after decode/preparation. It no longer schedules against the main loop's stale millisecond timestamp. Four decoded YUV slots (2,073,600 bytes maximum at 720×480) plus one private prepared picture provide lookahead. Loading/initialization/preroll are excluded from playback time. Preroll fills the queue or reaches EOF before starting the clock; one decode call per iteration keeps other services running. The source decoder supplies ordered PTS from the MPEG header's frame rate, including 29.97 FPS. No early publication, arbitrary timestamp rewriting, slow-motion clock correction, or queue-length-based dropping is used.

A frame can be discarded only if it is **more than two source periods late AND a newer queued frame is already due**. References still decode. The final picture is displayed and held. Full/paused queues stop decoding. The video-only loop uses bounded, deadline-aware **1–4 ms** waits/polls; the original music/menu 10 ms cadence remains. libXenon's `udelay` lowers hardware-thread priority while polling its timebase; this bare-metal runtime supplies no task scheduler for a timed OS sleep. No sub-millisecond spin, unbounded decode catch-up or new audio-path delay is introduced. The wait metric is time spent there, **not proof of idle CPU**.

Text still uses the tested changed-cache-line presenter. Two dirty bitmaps now restrict pixel comparisons to cache lines touched in either CPU source, including reverted/erased glyphs. It no longer reads both whole framebuffers for every 4 Hz statistics update. Complete glyphs remain composed before copy; there is no visible erase-only pass. Existing rendering regressions cover static text/video preservation and page restoration.

GPU input textures, vertex buffer and private resolve target are allocated once per geometry and reused. There is no CPU RGB conversion on that path, no texture creation per frame, no `Xe_Sync()` and no scanout swap. `Xe_pBlockUntilIdle()` emits a GPU ordering packet; despite its name it does **not** spin the CPU. It precedes the completion token so resolved data is safe to copy. The old redundant per-frame `Xe_Clear()` resolve is removed: the normal resolve clears EDRAM for the next frame; the explicit initial clear remains. Output copies and cache invalidation are still required by the stable UI architecture.

## Diagnostic meanings

Both pages update at the established approximately **4 Hz** cadence. Values are accumulated per run and retained at END/STOP.

| Label | Meaning |
|---|---|
| FPS / avg | Last one-second publication rate / `(shown-1)/active timeline`. Priming and pauses are excluded from the average; dropped frames lower it. |
| D / Dmax | Average/maximum codec call time for output frames, excluding queue copies. Elementary MPEG has no container demux: the displayed demux time is explicitly zero; bitstream parsing is part of decode. |
| Cv | CPU colour conversion, scaling and tiled staging per prepared frame. Zero on GPU. |
| Up | YUV texture row copy and surface unlock/cache stores per prepared frame. Zero on CPU. |
| Cmd | GPU vertex/state/draw/resolve/fence command construction and submission, separate from asynchronous completion. |
| GPU<= / poll | Average wall-clock time from GPU submission to observed fence completion, and its polling uncertainty. Actual completion lies between the last negative and first positive poll. These are bounds, not precise GPU hardware timestamps. The libXenon API used here has no timestamp-query support. |
| Copy | Cache invalidation if GPU, completed tile-band publication and cache stores per shown frame. No conversion, frame wait or VSync included. |
| VSync 0 (off) | No VSync API, retrace wait, display-counter wait or framebuffer flip occurs in this path. Zero describes the absence of waiting; it is not measured refresh latency. |
| Q / prep | Decoded queue occupancy/capacity/peak plus a separately prepared/pending picture. |
| Qcopy | Active Y/Cb/Cr rows copied out of padded decoder planes into a queue slot. |
| UI | Whole UI capture/composition/publication average per UI redraw. |
| Wait | Average actual cooperative wait duration per running main-loop iteration, excluding decode, GPU work and UI. |
| service % | Measured main-loop work / (work + wait). **Not total CPU utilisation**: waits use a timebase loop and other Xenon hardware threads are not profiled. |
| starvation | Running iterations with neither queued nor prepared output after the service step, before EOF. |

## Generate and build

Create all three benchmark clips with `make video-benchmarks` or `bash scripts/make_video_benchmarks.sh`. Each subdirectory contains the exact root filename. Exact FFmpeg command for the first benchmark:

```sh
ffmpeg -hide_banner -loglevel error \
  -f lavfi -i testsrc2=size=320x240:rate=30 -t 8 -an \
  -c:v mpeg1video -bf 0 -g 12 -q:v 5 -pix_fmt yuv420p \
  -aspect 4:3 -f mpeg1video xenonfin-test.m1v
```

For 640×360 use `size=640x360:rate=30` and `-aspect 16:9`; for 720×480 use `size=720x480:rate=30` and `-aspect 3:2` (square pixels for this diagnostic). Replace the output file deliberately. The input must be MPEG-1 **elementary** video, not a program stream/MP4/MPEG-2 file renamed to `.m1v`. Limits: **8 MiB**, even dimensions through **720×480**, **23.976–30 FPS**, compressed picture ≤128 KiB, unchanged sequence geometry/rate/aspect. Start with `-bf 0`; reordered I/P/B clips also pass tests.

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

The toolchain, libfat USB source and XeLL ELF conversion are unchanged. The maximum decoded queue is approximately 2 MiB; decoder arena remains 2 MiB; total linked static memory is below 30 MiB. Sources use real libXenon APIs; hosted mocks are test-only.

## Decoder provenance and host evidence

The original investigation selected MIT `pl_mpeg` (`c871f2be022ece7ef4f64230b4fb8e1fb9eb6023`) for a scalar PowerPC MPEG-1 decoder, and reused the GPL XMPlayer YUV/pass-through shader arrays (`5fc2fb330bfb58bd2806fed6c63f6ae917cb3a3c`). Full MPlayer/FFmpeg ports were inspected, not transplanted. The actual FAT source uses installed `fatMountSimple()` and `usb2mass_ops_0/1/2` on uda/udb/udc; no FATX/HDD access is assumed. See [THIRD_PARTY.md](THIRD_PARTY.md) and the vendored `SOURCE.md` for hashes/licences.

The two 0.5 pl_mpeg fixes preserve fixed-memory bytes/EOF without quadratic shifts and flush the final future reference after trailing B pictures. A **third, EOF-only fix** in 0.5.1 permits the slice's 23-bit nonzero lookahead to inspect fewer remaining bits at fixed-memory EOF. A complete final macroblock in the 720×480 fixture was previously skipped despite containing valid short codes; appending a sequence-end marker independently isolated the cause. The change reads only available bits and preserves streaming-buffer semantics. The strict FFmpeg pixel threshold was retained.

Real compressed fixtures pass: 240/240 frames at 320×240/30; 48/48 at 640×360/24 with B pictures; 60/60 at 320×240/29.97 with B pictures; 60/60 at 640×360/30; 60/60 at 720×480/30. All compare to FFmpeg below 1/255 mean absolute YUV error and 32/255 maximum (actual maxima 7–10). Native PTS and padded-plane checks pass. The compressed RGB-patch comparison is described above.

Scheduler tests model 3.3 ms decode, 12 ms synchronous preparation or asynchronous GPU completion, 2 ms copy, and 20 ms UI updates at 4 Hz. All 240 frames are published without drops at the correct duration at 30 and 29.97 FPS in that model; this **does not predict actual console throughput**. Tests also exercise early-frame holding, independent clock sampling, startup, pause, rollover, genuine late drops, backpressure, faults, stop, synthetic bars, texture allocation reuse, plane pitch/order, private resolves, completion uncertainty, watchdog recovery, text stability and USB loading. Normal and ASan/UBSan suites plus real Jellyfin authentication/browsing/music integration are the required build checks. Physical performance rows above remain pending.

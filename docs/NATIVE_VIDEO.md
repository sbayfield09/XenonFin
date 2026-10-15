# XenonFin 0.5 — local silent MPEG-1 hardware test

The user confirmed the existing XeLL boot, controllers, stable text UI, Ethernet/HTTP, Jellyfin login/browsing, MP3 streaming/PCM buffering and music progress reports on a real Xbox 360. Version 0.5 extends those sources. **Video decoding/presentation has not yet been validated on physical Xbox hardware.** No Jellyfin video streaming, video audio, H.264 hardware-decoder API or dashboard/XEX target is added.

## Downloads and first console test

Download [xenonfin-xell-video.elf](../dist/xenonfin-xell-video.elf) and [xenonfin-test.m1v](../dist/xenonfin-test.m1v). Check `dist/SHA256SUMS`. The boot archive also contains both files and these instructions. Keep [0.4.2](../dist/xenonfin-xell-music-fix.elf) as the unchanged stable rollback.

1. Copy the ELF to your working FAT32 USB as **xenon.elf**. Copy **xenonfin-test.m1v** to that USB's root. Keep the drive inserted. Boot through the established XeLL procedure; verify the title says **0.5**. No Jellyfin connection is needed for this test.
2. Use LB/RB to select **Local video test**, immediately after Now playing. Stop any music on Now playing first; this test refuses to start while music or queued PCM is active.
3. Press **A** for the CPU reference renderer. After loading, confirm a genuinely moving colored test pattern appears at the correct aspect ratio, with no audio. The supplied clip is eight seconds, 320×240, 30 FPS, 240 frames. Check colors, orientation, smooth motion and the decoded/displayed/drop counters. A growing counter alone does not establish visible playback.
4. Press **A or Start** to pause; the picture and playback clock should hold. Resume should continue the clip. **X** stops, restores the text background and retains diagnostic totals. **B** stops and returns to Libraries. LB/RB also stops video before leaving its page. Verify library navigation/login state and ordinary music playback still work afterwards.
5. Replay the whole clip without pausing and record source FPS, actual rendered FPS, decoded/displayed/dropped totals, average/max decode time and average presentation time. On an unloaded console, the target is approximately 30 FPS; this is a target, not a measured Xbox result. A pause can leave an already submitted GPU frame completing once.
6. After the CPU result is recorded, press **Y** from a stopped/end state for the **GPU texture test**. This is the accelerated path: upload Y/Cb/Cr planes, shader color conversion and filtered scaling into a private tiled render target, then copy completed tile bands into the video area. Repeat color/aspect, pause/stop/return and performance checks. GPU initialization occurs only for this explicit selection.
7. Repeat with a generated 640×360/24 FPS clip if the first test is successful. Record console model, cable/display mode, CPU/GPU selection, source format, diagnostics, text stability and any error message. Use the stable rollback if either path prevents navigation.

**GPU bring-up limitation:** the existing upstream `Xe_Init()`/`edram_init()` routines contain hardware waits and fatal initialization/allocation paths. A failed initialization may require rebooting; this cannot be proven recoverable without hardware. After successful initialization, frame submission uses a nonblocking scratch-writeback fence with a 250 ms deadline. A timeout retains private GPU resources, stops the test and allows the CPU renderer/menu to continue. No display-address swap or display frame-counter dependency is introduced. A timeout does not prove that the GPU is healthy; reboot before retrying its initialization.

## Exact FFmpeg commands

Generate the supplied test pattern:

```sh
ffmpeg -hide_banner -loglevel error \
  -f lavfi -i testsrc2=size=320x240:rate=30 -t 8 -an \
  -c:v mpeg1video -bf 0 -g 12 -q:v 5 -pix_fmt yuv420p \
  -aspect 4:3 -f mpeg1video xenonfin-test.m1v
```

Or run `make video-test-file` / `bash scripts/make_video_test.sh`. The latter writes `build/video-fixtures/xenonfin-test.m1v` by default and accepts an output path. The script deliberately overwrites its selected generated output. For 640×360/24 FPS, use `testsrc2=size=640x360:rate=24` and `-aspect 16:9` with the same encoding options.

Convert your own short clip while preserving its aspect ratio inside 320×240:

```sh
ffmpeg -i input.mp4 -t 8 -an \
  -vf 'scale=320:240:force_original_aspect_ratio=decrease,pad=320:240:(ow-iw)/2:(oh-ih)/2,setsar=1,fps=30' \
  -c:v mpeg1video -bf 0 -g 12 -q:v 5 -pix_fmt yuv420p \
  -aspect 4:3 -f mpeg1video xenonfin-test.m1v
```

The input must be a **MPEG-1 elementary video stream** (`-f mpeg1video`), not a `.mpg` program stream, MP4, MPEG-2 or H.264 file renamed to `.m1v`. This first test caps file size at **8 MiB**, dimensions at **640×360** with even width/height, source rate at **23.976–30 FPS**, compressed pictures at **128 KiB**, and rejects changing sequence dimensions/rate/aspect or MPEG-2 extensions. Start with `-bf 0`; I/P/B decoding also passes the host fixtures. It is a bring-up subset, not an arbitrary-file media player.

## Source investigation and implementation choices

| Component inspected | Findings and use |
|---|---|
| XMPlayer, revision `5fc2fb330bfb58bd2806fed6c63f6ae917cb3a3c` | `mplayer/libvo/vo_xenon.c` uploads planar Y/U/V to `XE_FMT_8` textures, draws a RECTLIST with YUV/passthrough shaders and resolves EDRAM. Its two published shader arrays are reused with GPL attribution. The old full player, its UI, display swapping and blocking vblank/sync loops are not transplanted. |
| MPlayer's bundled FFmpeg PowerPC code | `mplayer/ffmpeg/libavcodec/ppc/Makefile` selects MPEG/IDCT/DSputil AltiVec and H.264 DSP files. These establish a CPU-decoding route, not proprietary hardware decoding. A full libavcodec/libavutil cross-build, runtime dispatch and port integration would be a larger dependency effort. It was inspected, not cross-built or claimed compatible wholesale with the current GCC. |
| `pl_mpeg`, revision `c871f2be022ece7ef4f64230b4fb8e1fb9eb6023` | Small MIT C MPEG-1 decoder with a low-level planar output API, no SIMD dependency. Selected and actually compiled by the existing Xenon GCC. Two small EOF/buffer fixes are described below; no MP2 audio or program-stream demux is used by this player. |
| Pinned libXenon Xenos APIs | `Xe_CreateTexture`, lock/unlock, shader loading/fetch patching, EDRAM target/resolve and command submission build successfully. A private scratch-register writeback, as used by libXenon's sync code, is polled cooperatively instead of calling `Xe_Sync()` or spinning on vblank. |
| Installed libfat and USB drivers | `fatMountSimple()` mounts the `usb2mass_ops_0/1/2` interfaces already serviced by `usb_do_poll()`. The installed library's interface names are `uda`, `udb`, `udc`. The new source helper mounts only those FAT USB volumes and opens `name:/xenonfin-test.m1v`; it does not initialize HDD/DVD, use FATX or assume arbitrary disk paths. |

All source/rendering is native C linked into the PowerPC ELF. No desktop decoder, remote transcoder, SDL window, proprietary decoder library or mock is linked into it. See [THIRD_PARTY.md](THIRD_PARTY.md) for pinned hashes and notices.

## Module boundaries and bounded work

- `video_decode.h` defines a codec vtable and planar frame view independent of pl_mpeg; additional codecs can implement the same interface. `video_decode.c` preflights sequence headers and wraps pl_mpeg in a resettable **2 MiB** arena. Allocation failure returns a diagnostic through a guarded escape rather than returning NULL to upstream's unchecked allocations.
- `video_source.c` selects the actual FAT USB path. `video.c` loads a bounded file in **32 KiB steps**, yielding to the main loop between reads. Once loaded, decoding has no disk/network dependency. A single low-level USB mount/read can still block inside libXenon's existing storage driver; keep the drive inserted. Read errors, short files, unsupported formats and size limits produce on-screen messages. Local storage is implemented, so an HTTP video-source workaround is unnecessary in this build.
- The player copies active Y/Cb/Cr rows into **two queue slots**, at most **691,200 bytes** total at 640×360. Decoder reference frames occupy the arena. A GPU submission uploads its own plane copies before releasing a slot. File memory is at most 8 MiB and is freed at EOF/stop/error. Video static allocations add about 3.7 MiB; the linked image's total static footprint is below 28 MiB, with the sound DMA buffer still in low BSS. GPU textures/rings are allocated on demand and retained/reused in bounded quantities; pending GPU memory is never freed on cancellation.
- Decode work is capped at **one output-frame call per main-loop iteration** and stops when the queue is full or paused. The MPEG decoder can process an additional reference picture during the first output/reordering; there is no unbounded time-based catch-up loop. Individual frame decode cannot be preempted. Width/height/compressed-picture caps and the live maximum-time diagnostic make that work observable; actual Xenon frame costs remain hardware measurements.
- A 64-bit playback timeline accumulates wrap-safe differences from the existing monotonic clock and excludes pauses. Frames carry decoder presentation timestamps from the elementary stream's fixed frame rate. No frame is submitted before its timestamp. Frames over two source-frame periods late are dropped while reference decoding continues; counters distinguish decoded, displayed and dropped pictures. The final decoded frame is held at EOF; there is no looping, seeking, A/V synchronization or next-item behavior in this stage.
- `video_pixels.c` supplies BT.601 limited-range YUV→RGB, explicit `0xffRRGGBB` native words, aspect-fit scaling with pixel aspect, black borders and the existing Xenos tile mapping. CPU conversion/scaling touches a **private staging buffer**. `xf_framebuffer_video_copy()` copies complete 32-row tile bands, with alignment/bounds checks and cache stores, into the reserved video rectangle. It never draws individual pixels directly into scanout.
- `video_native.c` supplies the accelerated Y/U/V texture path. Textures use explicit pitches and clamped filtering; the YUV shader converts and scales. The GPU resolves only to a private BGRA8888 tiled surface. After completion, cache invalidation makes that surface visible to the CPU before bounded tile-band copies. The display address, original UI framebuffer and video mode stay unchanged. A GPU can never resolve directly over the text menu through this path.
- Diagnostics occupy text rows outside the tile-aligned video rectangle and update at the existing **4 Hz** UI cadence. The previous changed-region text presenter leaves identical blank canvas cells untouched while counters change. Stop/error/page exit invalidates its reference so the next UI frame restores the image area. There are no added audio-path sleeps or rendering calls from HTTP/decoder callbacks.

## Diagnostic meanings

The overlay shows codec, source dimensions/FPS, last one-second rendered FPS, decoded/displayed/dropped counts, average/max decode time, average presentation time, renderer/texture format, queue occupancy/peak, pending submission and playback clock. Decode timing includes copying the planar result into its queue slot. CPU presentation is timed through conversion/scaling/copy without the main-loop interval; GPU presentation includes asynchronous completion polling and the final tile copy, so its measurement includes scheduler latency. FPS/totals hold after EOF/stop; paused playback reports zero FPS after its next statistics window. No host timing is presented as a measured Xbox result.

## Decoder fixes and validation

Two pl_mpeg changes were necessary for complete frame output:

1. Only discard consumed bytes for buffer modes which request discarding. Unconditionally shifting fixed-memory data changed its length without updating total size, broke EOF detection and made decoding do repeated large memmoves. The new guard leaves fixed-memory source bytes intact.
2. Flush the pending future reference picture at EOF even when the last coded picture was B. This fixes the missing final reference frame in reordered clips.

The original and modified hashes and exact changes are recorded in `platform/xenon/third_party/pl_mpeg/SOURCE.md`. The real scalar codec decoded **240/240** frames at 320×240/30 FPS, **48/48** at 640×360/24 FPS with B pictures, and **60/60** at 320×240/29.97 FPS with B pictures. Compared to FFmpeg YUV420 output, mean absolute pixel errors were approximately **0.164**, **0.074**, **0.139** on the 0–255 scale; maximum differences were 10/7/7. Frame timestamps and moving-image checks passed, normally and under ASan/UBSan. Host decode averages were below 1 ms/frame in the ordinary probe run; these are x86 host measurements, not Xbox performance claims.

Player tests cover queue bounds/ownership, no early presentation, pause/resume, late drops, millisecond rollover, stalled rendering, stop/cancel, file-size/I/O failures, codec/renderer failures, aspect fit and color conversion. The actual native renderer is tested with mocked GPU calls for plane order/pitches, vertex layout, private resolve, nonblocking completion, timeout and CPU recovery; these mocks do not execute a Xenos shader. Framebuffer tests cover tile-band bounds, text updates leaving the video area intact and restoring the library/UI after video stops. Source tests check actual FAT USB interface/path selection through mocked mounts. Existing authentication/browsing/music/network suites and real Jellyfin 10.10.7 music integration pass.

```sh
DEVKITXENON=/workspace/setup/devkit make xenon
make native-test
make video-test-file
python3 tests/native_video.py
make -C platform/xenon/tests SANITIZE=1 all test
UBSAN_OPTIONS=halt_on_error=1 \
  JELLYFIN_VIDEO_PROBE=$PWD/build/native-tests-sanitized/video-probe python3 tests/native_video.py
python3 tests/native_server.py --music
bash scripts/package.sh
```

The original toolchain and XeLL ELF conversion remain unchanged; libfat from that toolchain is the added native link dependency. Physical USB loading, visible decoded motion, RGB channel order, GPU initialization/shader execution, real frame rate, frame timing under load and stable diagnostics still require the console test above. **Do not call 0.5 video playback hardware-working until that result is supplied.**

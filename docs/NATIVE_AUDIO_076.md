# XenonFin 0.7.6 — movie shutdown investigation

**Diagnostic release, not a confirmed hardware repair.** The user confirms 0.7.5's integrated tone and MP3 music work; movie audio starts, then stops with a drain timeout and audio remains unavailable. No Xbox is attached to this environment. The initial console fault cannot be identified from that secondary timeout alone.

## Investigation completed before implementation edits

The following line references refer to baseline commit `6f4776281efad61b8c4f1a41e94b10d06dc05d59` (0.7.5), so they remain reproducible after source lines move. All paths are under `platform/xenon/` unless stated otherwise.

| Location | Confirmed behavior |
|---|---|
| `main.c:550` → `jvideo.c:289`, `xf_jvideo_tick()` | Sole production caller of the function containing the exact movie timeout. Tests also call it directly. |
| `jvideo.c:20–40`, `finish()` | Enters STOPPING and starts `drain_at`. Cancels stream, stops video, clears software compressed/PCM/partial-block buffers. It does not reset or stop the SDK audio device. Submitted DMA PCM is left to drain. |
| `jvideo.c:345–348`, `xf_jvideo_tick()` | When control requests are idle, audio is enabled, cached `audio.queued` is nonzero and elapsed time since `finish()` is at least 2000 ms, sets failure and replaces both error origin and message with the drain timeout. It can execute later than 2 seconds if a report is pending. |
| `video_audio.c:77–87`, `xf_av_audio_clock()` | Reads backend queue; an invalid reading marks audio failed but leaves `queued` unchanged. A stale nonzero count subsequently satisfies the movie timeout. A stale zero count can incorrectly skip that timeout. Neither establishes physical emptiness. |
| `audio_native.c:30–32,51–69` | A shared adapter fault is latched. All subsequent queue requests return -1 without fresh hardware reads. Faults include descriptor-base/count/last-valid checks; submission capacity checks and the no-consumption watchdog can also latch a fault. |
| `audio_native.c:133–145` | Separate two-second watchdog faults when a nonempty queue shows no advance in consumed frames. This is **not** the movie shutdown timeout. |
| `jvideo.c:355–356`; `audio_native.c:87–97` | Movie lease remains held unless a valid zero queue permits release. New acquisition fails if an owner already exists, or queue/state is not ready. A fault latch therefore prevents music from acquiring even if hardware later drains. |

### Every route into movie STOPPING

`main → xf_jvideo_tick → finish` can be reached by:

- `handle()` at baseline `jvideo.c:252–287`: PlaybackInfo HTTP failure (`:262`), invalid response/source (`:265`), network-video setup failure (`:266`), audio-only mode on silent media (`:269`) or missing audio backend (`:275`).
- `xf_jvideo_tick()`: session expiry (`:297`), rejected HTTP status/content type (`:303`), transport failure (`:306`), demux/elementary-stream failure (`:311,313`), initial audio preroll with no progress for 20 seconds (`:329`), video decoder/GPU failure (`:334`), and completed or prematurely ended stream (`:337`).
- `xf_jvideo_service()` (`:180`), called by `xf_jvideo_tick(:318)` and directly by `main(:546,552)`: audio decoder, PCM submission, queue or timestamp failure.
- `xf_jvideo_stop()` (`:218–219`), called from `main(:600,602,638,651,669)`: controller back/stop/exit, switching to audio diagnostic, or leaving the Video screen. Tests also call it directly.

Pause (`jvideo.c:221`) and rebuffering (`:100–104`) do not call `finish()`. `xf_jvideo_service(:124)` returns unless STREAMING, so it does not continue feeding PCM while STOPPING. Stopping submission lets already submitted DMA drain; that is not itself a reason for hardware to stop consuming. `xf_jvideo_forget(:10)` is a separate cancellation/reset of application state, retaining an outstanding lease; it does not emit this timeout.

### Music versus movies

Both initialize once at `main.c:533` and bind distinct owner contexts at `:534–536`. Both use the original SDK copy/cache/descriptor submission, S16LE 48 kHz stereo, complete 2048-byte descriptors, a 32 KiB maximum queued window and explicit acquire/release. Neither pause nor rebuffer resets the hardware. Music can submit up to 8192 bytes per output iteration (`music.c:126–147`); movies assemble 2048-byte blocks and submit up to four per output call (`video_audio.c:102–135`).

Music computes consumed frames from fresh queue reads (`music.c:17–25`), and its drain branch checks the backend directly (`:184,207`). Invalid music readings use a 65536-byte sentinel, so music could also time out after a shared fault; its hardware success before movies does not establish that all backend checks are correct under movie workload. Movies additionally apply PES timestamp mapping, initial alignment, gap/tail accounting and expose the consumed-frame clock to the video scheduler. Those movie-only paths can initiate failure, but the old timeout concealed which one did.

Music enters DRAINING via `music.c:41–48` on stop/error/skip/end and stops output there. Movie STOPPING is analogous. Both retain ownership if safe release cannot be established. The SDK's descriptor and PCM arrays are static; neither is freed on movie failure. The timeout code performs no audio register writes. This means **software lockout is proven; an unrecoverable physical DMA stall is not**.

### SDK register evidence

Pinned libXenon commit `c32c813c3d630ae922f83395ee4509b0468af71d`, `libxenon/drivers/xenon_sound/sound.c:93–125`, reads status at `0xea001604`:

```
read = status & 31
last_valid = (status >> 8) & 31
remaining = status >> 16
queued = ((last_valid - read) & 31) * 2048 + remaining
```

SDK free space is based on the descriptor read pointer and software write pointer, with full capacity when read==last_valid and remaining==0. `0x8000` is outside those fields, not 32768 queued bytes; no undocumented meaning is assigned to it. Base at +0 is the descriptor address. SDK initialization, cache maintenance, descriptor publication, control writes and ring allocation remain unchanged. Raw capture and separate SDK queue/free reads occur at slightly different instants; they need not agree exactly during active consumption.

## Minimal changes supported by the evidence

1. Retain the **first** movie/audio error and origin. A secondary drain failure no longer overwrites it.
2. Record the caller/function/source line, previous playback states, drain-entry time, first/latest queue result and submitted/consumed frames at entry. This identifies precisely which transition initiated shutdown, and whether any PCM was submitted afterward.
3. Track queue validity separately from the last valid count. After the unchanged two-second deadline, report either a measured nonempty queue or an **unverified drain because the queue read failed**. An invalid zero-cache reading cannot authorize completion as if empty.
4. After a shared fault, sample registers/SDK counts **read-only at 10 Hz**, keeping the original first-fault snapshot intact. These observations do not clear faults, modify playback clocks, reset hardware, submit PCM or release ownership.
5. Add video diagnostic pages 18–19 and CSV records for these measurements.

The low-level fault latch and safety rules are intentionally retained until the first offending reading is known. Thus **this release may still require reboot after the fault**. It repairs misleading error reporting and provides the missing evidence; it does not claim to repair either the unknown trigger or the hardware lockout. Automatically accepting a later empty sample without understanding the original descriptor/capacity/watchdog failure would bypass the requested safety checks.

Unmodified: music implementation, audio decoder/conversion, PCM format, SDK driver, MPEG-PS demux, MPEG-1 decoder, video scheduler, renderer, textures, network streaming and controller behavior except selection of two additional diagnostic pages. Standalone tone source/test controls remain intact; the shared adapter only gains read-only fault observation.

## Hardware test and evidence to collect

1. Download [full application ELF](../dist/xenonfin-xell-video.elf), rename to **xenon.elf** and cold-boot directly through XeLL. Confirm **0.7.6**. Use the existing USB/boot method; no standalone-test prerequisite.
2. Audio output → A: hear the 440 Hz tone, then allow it to finish. Confirm READY and owner none.
3. Log in and play Jellyfin MP3 music. Stop it and allow reporting/drain to finish.
4. Select a movie in mode C (audio and video), and try at least five minutes. Record whether audio stops before or after the picture and whether the error occurs at the same point each run.
5. If it succeeds, stop; immediately play music, then another movie without rebooting. Check pause/resume and dialogue synchronization as well. This full sequence is the acceptance test, not merely HTTP 200 or a successful build.
6. If it fails, **do not start another movie or reset diagnostic state before saving**. On Video use Left/Right for **page 18** (drain caller, queue validity, submissions after stop) and **page 19** (backend error, first fault and later hardware queue). Photograph both; page 17 remains available for decoder errors. Wait several seconds so the read-only after-fault observation can reveal whether hardware empties.
7. For complete, untruncated text: put packaged `xenonfin-test.m1v` at the root of a supported FAT USB device. Once playback/control cleanup is stopped, click the **right stick** from Video or Audio output. Save **xenonfin-av.csv**, **xenonfin-audio.csv**, and **xenonfin-video.csv** from the USB root. Exports contain no token or stream URL. Save status may replace the screen's main message, but the first cause remains in CSV.
8. Inspect Audio pages 1 and 3: state/owner, initialization count and retained first-fault registers. If locked out, record the attempted music result, then reboot only after collecting evidence. A run that locks out fails acceptance.

How the measurements decide the next repair:

- Invalid queue + native descriptor/count error: compare first-fault base/read/last/remaining against submitted frames and expected last descriptor. A later empty queue proves physical drain occurred despite the software latch, but does not yet justify discarding the original error.
- Native no-consumption watchdog + unchanged later pointers/nonempty count: evidence of a persistent physical consumption stall; retain base/control and timing.
- HTTP/PTS/decoder error + valid DMA that drains: the first error is the initiating fault, not DMA initialization. Ownership should release through existing cleanup.
- Valid nonempty queue at two seconds without a native fault: inspect later observation and no-consumption watchdog; check `submitted_at_entry` against `after_cleanup submitted` to rule out continued producer activity.

## Build and software validation

Use the existing toolchain; no dependency or environment changes are required:

```sh
make -C platform/xenon/tests all test
make -C platform/xenon/tests SANITIZE=1 all test
DEVKITXENON=/workspace/setup/devkit make xenon
DEVKITXENON=/workspace/setup/devkit python3 scripts/check_native_audio.py
```

[Validation record](native-audio-076-validation.json) records software results and pending physical acceptance. Regression cases explicitly reproduce masked primary errors, stale nonzero/zero queue readings, true occupied-queue timeout, no new submissions after STOPPING, and an empty hardware observation after a fault while safety/ownership remain latched. Host DMA simulation cannot determine whether the console's actual offending reading is legitimate.

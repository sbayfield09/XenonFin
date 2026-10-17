# XenonFin 0.7.5 — validated DMA startup and audio ownership

The standalone PCM test is confirmed working by the user. The integrated 0.7.4 application failed before streaming or decoding. **The 0.7.5 integrated tone and media output require physical Xbox verification.** Host tests and compilation cannot establish audible output.

## What the reported registers establish

The pinned libXenon implementation is `libxenon/drivers/xenon_sound/sound.c`, commit `c32c813c3d630ae922f83395ee4509b0468af71d`. Its queue calculation reads the register at `0xea001604`:

```
read_descriptor = status & 0x1f
last_valid_descriptor = (status >> 8) & 0x1f
remaining_bytes = status >> 16
queued_bytes = ((last_valid_descriptor - read_descriptor) & 31) * 2048
               + remaining_bytes
```

| Hardware evidence | Interpretation supported by the SDK |
|---|---|
| Base `000a0000` | Matches the full application's linked descriptor address in 0.7.4 |
| Control `1d08001c` | Exactly the value written by SDK initialisation |
| Status `00008000` | Read=0, last-valid=0, remaining=0, therefore **zero queued bytes** |

Bit 15 (`0x8000`) is outside all three SDK queue fields. It is not a byte count. This release does not invent a hardware meaning for that bit or change its value.

Both applications already initialise in the same order: display, console, USB, SDK sound. There is one SDK initialisation call behind a guard, not a reset when changing screens. The main and isolated binaries place the original SDK's static DMA objects at different linker-selected addresses, which is expected; both must remain aligned and below 32 MiB.

## The failing software condition and the remaining hardware uncertainty

0.7.4's shared adapter latched a permanent failure if:

```
pending < 0 || pending > 65536 || pending > submitted_frames * 4
```

Before any PCM has been submitted, **any positive SDK queue observation triggers that last condition**. Subsequent calls return `-1` without re-reading the device. Worse, the offending register was read for diagnostics only after this check, so the failed read was not retained. The Audio screen independently read newer registers. An empty status displayed later cannot clear or explain the earlier latch.

The hardware-confirmed 0.7.3 standalone callback returned the SDK queue count directly; it did not have this permanent adapter latch. Both 0.7.4 binaries include the newer shared adapter. If the separately tested file was also 0.7.4, its different startup timing may expose a different first observation, but this cannot be established from the later snapshot alone. The original standalone reference is retained for comparison.

That produces the three reported messages:

- The tone checks a nonzero backend return; `-1` becomes “DMA is not empty (or invalid).”
- Movie audio rejects the negative return as “Invalid audio DMA queue reading.”
- Music treats the invalid return as an occupied queue, then times out draining it. HTTP/MP3 counters can remain zero.

SDK initialisation resets its software write pointer but **does not write status at offset +4**. The regression tests reproduce a nonempty reset observation followed by the reported empty `0x8000` state: old validation would latch; the new startup state waits for verified emptiness and then permits acquisition. It never submits over a nonempty or unknown queue.

**The supplied snapshot alone cannot prove which earlier value triggered the user's console failure.** It decodes to zero and cannot itself satisfy the failing condition. Reset-time observations are a reproducible failure mechanism, not a claimed measurement of the console's missing earlier sample. The retained startup and first-fault snapshots in this build are necessary if cold boot still fails. A deeper hardware fault remains possible; no success is claimed in advance.

## Changes

The original pinned `libxenon.a(sound.o)` still performs all GPIO/HDMI initialisation, control writes, buffer copies, cache maintenance and DMA descriptor publication. Its SHA-256 remains `9a569f3a768d93da6a5ea9e4d3091a12b62b9ea1a9f41d0d5fc2d16b713d9692`. No replacement sound driver, guessed reset sequence or pitch correction was added.

The adapter now has COLD, STARTING, READY and FAULT states. Before the first submission it requires read=last=remaining=0 and the SDK's free-space result of 65,536 bytes. Temporary observations wait cooperatively, with no sleeps in audio output. If this does not settle within two seconds of service, a specific startup fault and its register snapshot are retained. An unknown or nonempty state is never labelled empty merely because this program has not submitted PCM.

Queue validation uses one captured status value with the SDK masks and arithmetic above; separate SDK queue/free readings are saved for comparison. During playback it checks descriptor length, last-valid against the software submission position, queue bounds and descriptor-base continuity. Backward consumption observations retain the existing monotonic clock and increment a diagnostic; stalled queued data still faults after two seconds. Runtime initialisation also checks the descriptor base, all 32 descriptor addresses/lengths, the sample ring's alignment and the first-32-MiB requirement. The build audit independently verifies actual linked addresses.

Tone, music and movie clients have distinct backend contexts and explicit acquisition/release. Ownership remains held through pause, rebuffer, track changes and drain/cleanup. Another client cannot submit or release that owner's DMA. Premature release and repeated initialisation are rejected/counted without hardware writes; a repeated initialisation call cannot reset active playback. A fault retains ownership when safe drain cannot be established. The inactive music player no longer polls and poisons its decoder state before a track is selected. Logout cancels producers and retains any outstanding lease until drain.

The established PCM format remains **48 kHz, signed S16LE, interleaved stereo, four bytes per stereo frame**, submitted as complete 2,048-byte descriptors. The original sample generator, minimp3 decoder, MPEG-PS demuxer, A/V timing, buffer capacities, MPEG-1 decoder, frame scheduler, textures and framebuffer implementation are unchanged. Video-only/local diagnostics can still run when audio startup has failed.

## Captured diagnostics

On the main application's **Audio output** screen, Left/Right selects three pages:

1. Current state/owner, most recently captured base/control/status, initialisation/ownership counters and first startup-wait status.
2. Saved registers before SDK init, immediately after init and before the first PCM submission of the current session.
3. During playback, after drained release and the **first fault**. A pending slot has not yet occurred; it is not a measured zero register.

The first startup-wait sample is also retained even if startup subsequently succeeds. On a latched fault, the displayed register capture stays frozen at the fault rather than silently showing a newer empty register. `xenonfin-audio.csv` contains the raw and decoded fields, SDK readings, submitted/consumed counts and lifecycle counters. From Audio output, click the right stick while players are stopped to save it alongside the existing video/A/V CSVs. The existing USB discovery requires the packaged `xenonfin-test.m1v` at the FAT USB root. Saving diagnostics is allowed after a device fault when players have stopped. Authentication data is not included.

The updated isolated test uses LB/RB for equivalent snapshot pages; Left/Right still selects tones. The original hardware-confirmed reference remains available as the [unchanged 0.7.3 standalone ELF](https://raw.githubusercontent.com/sbayfield09/XenonFin/02f993b349173a71a8dd7b07c6d61837a8da5601/dist/xenonfin-xell-audio-test.elf). It is not a prerequisite for booting the full application.

## Final hardware test

1. Download the [full 0.7.5 ELF](../dist/xenonfin-xell-video.elf), rename it **xenon.elf** on the existing XeLL boot device, fully power off and boot it directly. Do not run the separate audio application first. Verify version 0.7.5.
2. Before starting Ethernet, open Audio output. Expect READY, owner none, one init call, zero duplicate/active-init/conflict counts. Inspect pages 2–3. `00008000` may appear as an empty status; raw status need not literally be zero.
3. Press A for 440 Hz. Hear a clean, correctly pitched tone. Confirm owner tone during the test and none after drain. Repeat with 880 Hz, left/right tests, and a 60-second 440 Hz run (X chooses the test; Y toggles duration). Record the consumed-frame rate and listen for continuity.
4. If the tone fails, retain the first-fault and startup snapshots/CSV. Leave media acceptance pending; do not work around the failure by first booting the isolated test or altering tone frequency.
5. After the integrated tone passes, initialise networking, sign in and play actual MP3 music for several minutes. Check pause/resume, track switching, stop and a second tone. The owner must return to none after cleanup; init count must remain one.
6. On the stopped Video screen, use left-stick click to choose **mode B (audio only)**, then select a movie/episode and hear its soundtrack. Stop and choose **mode C (both)** for synchronised playback. Play for at least five minutes; check dialogue/flash-beep alignment, pause/resume, stop and return to the browser. Existing Video Left/Right diagnostic pages remain available.
7. Retest video-only mode A and local CPU/GPU video. Repeat a fresh direct cold boot into the full application to establish that startup is independent of the isolated reference.

No console is attached to this environment. Only this final build is offered; software checks do not replace steps 2–7.

## Software validation and build

[Recorded results](native-audio-075-validation.json) identify the tested source, SDK provenance, protected video-source hashes and host-only results. The regression cases cover the exact reported status, startup residue/recovery/timeout, invalid alignment/layout, unexpected last-valid/remaining fields, lease conflicts, stop-before-drain, repeated init during playback, PCM ownership and ring wraparound. Native and ASan/UBSan suites include existing waveform, MP3, timestamp, playback-state and transport checks. Real-server tests use Jellyfin 10.10.7 and production C clients with a simulated consumption device.

The final native and ASan/UBSan runs passed, as did seven waveform/PCM reference checks, real-server music (mono 44.1 kHz, stereo 48 kHz and FLAC transcoding) and all **16 A/V cases**. The **300-second host endurance run decoded/displayed 9,000/9,000 frames**, with zero video drops, audio underruns, rebuffer events, silence bridges or PCM trims. It consumed 14,402,560 stereo frames, completed playback reports/cleanup, and recorded a maximum audio service gap of 85.117 ms. Forced 250 ms CPU starvation deliberately produced one audio underrun and two controlled video drops; five-second network starvation recovered after one underrun. Those fault cases are not labelled uninterrupted playback.

Both PowerPC ELFs compile without warnings. The link audit confirms the unchanged SDK driver and descriptor/ring physical addresses **0xa0000/0xa0100** in the full application and **0x40000/0x40100** in the isolated test. The main application's placement therefore matches the reported 0.7.4 addresses. These are software/build checks, not a physical 440 Hz acceptance result.

```sh
make -C platform/xenon/tests all test
make -C platform/xenon/tests SANITIZE=1 all test
JELLYFIN_MUSIC_PROBE=build/native-tests-sanitized/music-probe python3 tests/native_server.py --music
JELLYFIN_VIDEO_STREAM_PROBE=build/native-tests-sanitized/jvideo-probe \
  python3 tests/native_video_server.py --extended --metrics /tmp/av-075.json
DEVKITXENON=/workspace/setup/devkit make xenon
DEVKITXENON=/workspace/setup/devkit python3 scripts/check_native_audio.py
```

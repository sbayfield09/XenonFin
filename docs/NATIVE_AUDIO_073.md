# XenonFin 0.7.3 — restore the working PCM output backend

The user subsequently confirmed correct sound from the standalone test. The integrated follow-up is [0.7.4](NATIVE_AV_074.md); this page retains the original rollback procedure and historical acceptance status.

**First acceptance gate: a clean, correctly pitched 440 Hz sine on a real Xbox 360. This is still pending.** The user reports 0.7.2 video works, but movie audio and even its standalone tone are glitched/low-pitched. Earlier releases produced recognisable MP3/music and movie sound. Investigation of MPEG-PS and A/V timestamps is suspended.

## Regression comparison and rollback

The 0.4 music release (`d4b4ac6`), 0.6 (`ae879e8`) and 0.7.1 (`16d8eb9`) resolve sound calls from the same pinned **unmodified libxenon.a(sound.o)**. Commit `5b2b3ec` (0.7.2) added a compiled replacement. Its host tests asserted a guessed control-register model; they were not proof of silicon behavior. That replacement and those tests have now been removed.

| Audio output operation | Working SDK / 0.7.1 | Regressed 0.7.2 | 0.7.3 |
|---|---|---|---|
| Driver object | Pinned archive `sound.o` | Project override of sound functions | Original archive `sound.o` |
| Submit control transaction | `write32(base+8, read32(base+0) \| 0x01000000)` | Read **base+8**, then write base+8 | Exact SDK transaction restored |
| Init stop/reset order | After GPIO/HDMI, ring clear/cache and descriptor setup | Moved before GPIO/HDMI and memory setup | Exact SDK order restored |
| HDMI/GPIO/I2C/sample configuration | Original SDK sequence | Same values but different reset order/control replay | Original SDK sequence |
| DMA memory | Static 65,536-byte ring; 32 × 2,048-byte descriptors; 256-byte alignment; first 32 MiB | Same capacities in replacement object | Original allocation/index/cache code |
| PCM format | 48,000 Hz signed S16LE interleaved stereo | Same source byte format | Same format |
| Producer/consumer and cache | SDK write pointer, modulo-32 read/last-valid/remaining accounting and cache stores | Copied code | Original SDK object, no local implementation |
| Short tone producer | SDK square wave, 1,024-byte writes | Cooperative square wave, 2,048-byte writes | Verified sine diagnostics, complete descriptors through original SDK |

The source-level regression is the **replacement of the established hardware control/reset path**. The changed submit read is the leading shared-path suspect: it runs for both tone and decoded music, whereas neither demuxing nor MP3 decoding runs for a generated PCM tone. We undo both hardware transaction changes rather than trying another register sequence.

The precise silicon interpretation of those registers is **not established here**. In particular, writing a descriptor address at base+0 does not prove that reading base+0 returns that address; MMIO read/write meanings may differ. The previous claim that preserving base+8 control bits was a verified hardware repair was unsupported and is withdrawn. A physical rollback test is required to confirm the cause and repair. No frequency adjustment is used to mask a slow or misconfigured output engine.

## Isolated acceptance ELF

**Download [xenonfin-xell-audio-test.elf](../dist/xenonfin-xell-audio-test.elf)** and copy it as **xenon.elf** to the existing XeLL boot device. The boot archive already names the audio-only ELF **xenon.elf**; its full application is **xenonfin-app.elf** for later gates. Cold-boot into it so no previous application's DMA remains active. Keep the HDMI connection/TV settings used for the working releases.

This ELF links only its test main, PCM generator, existing stable text compositor and the required libXenon controller/display/sound support. It never initialises networking and contains no HTTP/lwIP, MPEG, MP3, video player/renderer or A/V scheduler symbols. Display initialisation is retained for minimal text; no video frame/texture submission occurs. Source/link inspection is reproducible with `scripts/check_native_audio.py`.

Controls are deliberately manual, with **no automatic next stage**:

| Control | Action |
|---|---|
| A | Start the selected test, only with an empty DMA queue |
| Left/Right while stopped | 440 stereo → 880 stereo → 1 kHz stereo → silence → 440 left only → 440 right only → 440 left/880 right |
| Up/Down while stopped | Toggle 1 or 60 seconds |
| X | Stop producing PCM and drain already-submitted audio |
| B | Stop/drain first; press again after drain to return to XeLL |

A stalled DMA produces an error without more writes or automatic reinitialisation. If the queue remains stuck, cold-boot; the program will not exit while hardware still owns its PCM. The full application similarly drains a diagnostic tone before exit. Its Audio output page offers A to start, X to select a test and Y to select duration; use the dedicated ELF for the actual isolation test.

## Verify PCM, DMA and pitch

The SDK header explicitly documents **48 kHz, signed 16-bit stereo PCM, little endian**. Each frame is four bytes: L low, L high, R low, R high. PowerPC native-endian int16 casts are not used. The generator uses amplitude 6,000 with no DC offset: samples stay within −6,000..+6,000, or exactly zero on silent channels.

A deterministic 1,200-entry quantised sine table represents `round(6000*sin(2*pi*n/1200))`. Steps of 11, 22 and 25 entries at 48,000 frames/s produce exactly 440, 880 and 1,000 Hz. It is normal sample generation, not compensation for measured output pitch. The native generator packs and hashes a one-second, 192,000-byte bank **before the first DMA submission**, against independent reference fingerprints. Host C and Python checks compare every packed channel sample with the mathematical sine and verify exact frequency crossings. The boot archive includes seven one-second WAVs containing these actual native generator bytes for PC reference; they are not recordings of Xbox output.

During playback, a fixed 2,048-byte scratch block takes consecutive/cyclic frames from that bank. The SDK synchronously copies it into its own static DMA ring before returning, then performs its original cache stores. Scratch storage can be reused after that return; hardware never points into it. No PCM memory is freed. Submission is bounded to 8 KiB queued, leaves at least 2 KiB free-ring guard, uses whole descriptors and never changes descriptor lengths. Frame counts, queue space and ring wrap are tested independently with a consuming DMA model.

At 48 kHz:

- 48,000 stereo frames = 192,000 valid bytes = one second of source audio.
- The one-second test pads only the last descriptor with 128 silent frames. It submits **48,128 frames**, expected **1.002667 seconds** plus device startup and controller observation latency. The sine frequency itself is unchanged.
- The 60-second test contains **2,880,000 frames / 11,520,000 bytes**, exactly 5,625 descriptors, with no tail padding and bounded resident memory.

The overlay reports submitted and consumed frames (`submitted_bytes - SDK_unplayed_bytes`, divided by four), queued bytes, a measured window of at least 48,000 consumed frames, its elapsed monotonic microseconds and estimated samples/s. It also shows first-submission-to-drain elapsed time, expected submitted duration, underruns, maximum service gap and **read-only** base/control/status plus descriptor read/last-valid/remaining fields. Raw queue readings are validated before use. A backwards consumed count increments **counter faults** and invalidates/restarts the rate window; rate figures with counter faults must not be treated as proof of a good device clock. These counters estimate DMA consumption rather than merely counting submitted samples; they cannot prove audible frequency, HDMI/TV latency or undocumented register behavior.

The PowerPC clock uses coherent upper/lower/upper timebase reads and the existing exact 49,875,000 Hz conversion. The controller loop services PCM around text work; text updates at most four times/s and no decoder/network is present. A 0.5 ms outer controller-loop pacing interval does not alter sample generation or the DMA consumption clock. Generation/submission contains no sleep or busy-wait.

## Ordered hardware gates — stop at the first failure

1. **440 Hz first.** Leave default 440 stereo / one second selected and press A. Hear a smooth, clean sine near 440 Hz, without glitching or low-pitched noise. Repeat. Compare with the supplied `audio-reference/440-stereo.wav` on a PC if useful. Record elapsed time, preflight hash, queued/consumed counts, raw status/control and output route. If distorted, **do not proceed** to 880, music or movies; preserve the screen, and compare the [previous 0.7.1 ELF](https://github.com/sbayfield09/XenonFin/blob/16d8eb9715a5e844f31ef420b06c07da95c329f9/dist/xenonfin-xell-video.elf) after a separate cold boot.
2. **880 Hz.** Only after 440 passes, select 880 and press A. It should be one octave higher, equally clean, with the same consumption rate/duration. Check the 1 kHz sine and digital silence; silence must produce no tone or noise.
3. **Channel separation.** Select LEFT only, then RIGHT only. Sound must come from the indicated TV channel/speaker, with the other silent. The split test provides 440 left / 880 right. TV mono processing can obscure this; retain/report the actual TV audio setting rather than swapping PCM channels as a guess.
4. **60 uninterrupted seconds.** Only after pitch and separation pass, select 440 stereo and toggle to 60 seconds. It must remain clean for the whole run, without periodic glitches at DMA wraps. Expect 2,880,000 submitted/consumed frames, zero underruns/counter faults, a roughly one-second 48,000-frame consumption window and roughly 48,000 estimated frames/s. At normal output the elapsed time should be near 60 seconds; double duration indicates approximately half-rate consumption. Preserve the final screen. Counters alone are insufficient: listen/record pitch and continuity.
5. **MP3 music.** Only after the isolated 60-second gate passes, boot the [full application ELF](../dist/xenonfin-xell-video.elf), also named xenon.elf on USB. Sign in and play a previously working MP3 track. The decoder/conversion/music implementation has not changed. Verify clean sound, pause/resume/stop, then stop and wait for cleanup.
6. **Movie soundtrack, then synchronisation.** Only after music passes, test the existing movie audio path (mode B audio only is available), then mode C both. No demux/PTS/scheduler changes have been made for 0.7.3. Assess dialogue/flash-beep synchronisation and continuous playback only after actual soundtrack quality passes. If an earlier gate fails, leave these stages pending.
7. Retest existing movie video and local CPU/GPU video once audio gates permit using the full application. The protected video/streaming/sync/decoder sources match 0.7.2 byte-for-byte; compilation/source equality does not substitute for this physical regression check.

## Build and evidence

```sh
DEVKITXENON=/workspace/setup/devkit make xenon xenon-audio-test
make -C platform/xenon/tests ../../../build/native-tests/sound-diag-tests
build/native-tests/sound-diag-tests
make -C platform/xenon/tests SANITIZE=1 ../../../build/native-tests-sanitized/sound-diag-tests
build/native-tests-sanitized/sound-diag-tests
python3 scripts/verify_audio_pcm.py
DEVKITXENON=/workspace/setup/devkit python3 scripts/check_native_audio.py
```

[Validation record](native-audio-073-validation.json) contains the unchanged archive sound-object checksum, linked sound function provenance, physical DMA addresses/alignment, isolation checks, independent PCM verification, simulated 60-second/slow-clock tests and protected-source hashes. Native PCM tests and ASan/UBSan tests pass. Both XeLL-compatible PowerPC ELFs cross-compile. Full application sound descriptor/ring physical addresses are 0x000a0000/0x000a0100; isolated ELF addresses are 0x00040000/0x00040100, all 256-aligned and within the first 32 MiB. Placement follows the same SDK/linker mechanism rather than forcing new hardware addresses.

No real Xbox is attached to this environment. Clean tone output, actual pitch/rate, continuous 60-second hardware playback and all subsequent gates remain **UNTESTED for 0.7.3**. No new MP3/movie/sync behavior is introduced or claimed while that first gate is pending. Existing source functionality is retained in the full build for staged tests.

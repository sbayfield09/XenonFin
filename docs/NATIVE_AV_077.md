# XenonFin 0.7.7 — remove speculative movie-audio padding

The user reports the 0.7.6 `Audio PTS overlaps already submitted PCM` failure on every tested video with audio. This identifies the initiating application check; it does not by itself prove which timestamp or DMA condition occurred. Tone and music are hardware-confirmed working. **0.7.7 requires console testing; no physical success is claimed.**

## Reproduced client defect

At baseline `f5f20e842472f8ee5997003b6b40b7e42a1103c3`, `jvideo.c:170–176` enabled `audio.bridge` whenever video ES storage was full and software audio levels were low. It did not require an empty DMA queue or evidence of an actual timestamp gap. `video_audio.c:128–132` then padded incomplete PCM blocks with silence even though the audio decoder had not reached EOF. The flag persisted until PCM accumulated to 64 KiB, while repeated decode/output calls normally prevented that accumulation.

A decoded 1152-sample MP3 frame is not a whole number of 512-frame DMA descriptors. The last 128 samples can legitimately remain in an unfinished descriptor. The old bridge path appended silence, submitted that descriptor, and advanced `submitted`. When the next MP3 frame arrived with continuous timestamps, its desired position preceded that artificially advanced position. The overlap check at `video_audio.c:113–114` stopped playback. No bad media or broken PCM hardware is required to reproduce this mechanism.

The regression test now submits continuous PCM, sets the former pressure flag, feeds successive 1152-frame fragments and verifies exact sample bytes, the retained partial descriptor, zero inserted silence and no overlap. Complete starvation must submit nothing until real PCM is available. Genuine backward timestamp overlaps remain errors, with their numerical positions captured. A backward timestamp adjustment is a second independently reproducible route to the same old message; the supplied photo does not exclude it.

**The bridge defect is confirmed in software. Whether it completely explains this console's failure, and whether a separate DMA fault remains, must be established by testing this ELF.** Removing the overlap guard or clearing the native fault latch would conceal those distinctions.

## Targeted changes

- Buffer pressure no longer permits silence or starts an unaligned audio clock. Partial PCM descriptors are retained until more real samples arrive. Final descriptor padding requires actual audio EOF.
- Silence for a measured forward PTS gap, initial positive A/V offset, or a video tail after verified audio EOF remains supported. No real samples are trimmed, duplicated, or pitch-adjusted.
- The program-stream demuxer can queue compressed video while continuing to the next audio PES packet. Video bytes are delivered to the existing decoder in their original order. The caller owns fixed storage; no per-frame allocation or unbounded growth is introduced.
- The backlog is **4 MiB**, additional to the existing 256 KiB decoder ES and 512 KiB HTTP/PS ring. The existing maximum supported audio gap is ten seconds and requested video bitrate is 2.5 Mbit/s: that corresponds to 3.125 MB, with room for encoder bursts. This is a byte bound, not a guarantee for arbitrary interleave/bitrate. A full backlog with no usable audio reports an explicit interleave-limit error instead of inventing clock progress. This allocation is below 1% of Xbox 360 RAM; the four decoded-picture slots are unchanged.
- Demux input completion is separate from draining queued video. Audio can reach verified EOF and drive a silent video tail while compressed video remains queued. The video decoder receives EOF only after every queued video byte is delivered.
- CSV records now include backlog occupancy/peak, verified input completion, bridge/tail counts and exact overlap expected/next/read positions. Video page **10** shows overlap positions if present; page **12** shows backlog measurements. Pages **18–19** retain the shutdown/native-fault diagnostics from 0.7.6.

No changes to `audio_native.c/.h`, hardware initialization, DMA registers/descriptors, cache management, PCM format, music playback, MP3 decoder, MPEG-1 decoder, video scheduler, GPU renderer, textures or HTTP transport. The original sound driver and its safety checks remain in use. The standalone audio ELF remains the previous reference.

## Software verification

See [recorded results](native-av-077-validation.json). Native/sanitizer regressions cover continuous partial PCM, starvation, true PTS gaps/overlaps, optional demux backlog wraparound, capacity backpressure and EOF ordering. A real-server pressure test holds the video consumer until at least 64 KiB is queued, verifies that no PCM has been submitted during the hold, then requires ordinary audio/video playback, pause/resume, reporting and clean ownership release. This establishes that buffer pressure can be relieved without a fabricated clock advance. Host tests cannot establish audible console output.

The network-outage fixture now interrupts reception as soon as audio starts, while HTTP is still active. Its former three-second trigger could occur after the new backlog had received the entire eight-second fixture, so it no longer exercised starvation. The assertion still requires a recorded underrun, a rebuffer reason and successful recovery; it was not relaxed to accept an ineffective interruption.

```sh
make -C platform/xenon/tests all test
make -C platform/xenon/tests SANITIZE=1 all test
JELLYFIN_VIDEO_STREAM_PROBE=build/native-tests-sanitized/jvideo-probe \
  python3 tests/native_video_server.py --extended --metrics /tmp/av-077.json
DEVKITXENON=/workspace/setup/devkit make xenon
DEVKITXENON=/workspace/setup/devkit python3 scripts/check_native_audio.py
```

## Console test

1. Download the [full XeLL ELF](../dist/xenonfin-xell-video.elf), rename it **xenon.elf**, cold-boot directly and confirm **0.7.7**.
2. Play the integrated 440 Hz tone, then Jellyfin MP3 music. Stop music and let cleanup finish.
3. Play the same movie/episode that failed immediately, in mode C (audio + video). Run for at least five minutes; listen for dialogue sync and continuity. Check pause/resume.
4. Stop, immediately play music, then start another movie without rebooting. A timeout, lockout or overlap still fails acceptance.
5. If it fails, capture Video pages **10, 18 and 19** before replaying. Page 10 now retains exact overlap positions. Page 12 shows the backlog peak/bound. Save `xenonfin-av.csv` and `xenonfin-audio.csv`: with playback stopped and packaged `xenonfin-test.m1v` at the FAT USB root, click the right stick on Video or Audio output. Save before rebooting.

If the overlap still occurs with zero bridge frames, the new positions and PTS error distinguish a real timestamp discontinuity from this repaired padding path. Any native DMA fault remains separately visible and is not automatically reset.

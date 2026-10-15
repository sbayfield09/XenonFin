# XenonFin 0.7.2 — receive servicing and rebuffer diagnostics

The user reports HDMI directly to the TV: real movie sound and pictures play in sync while PLAYING, then REBUFFERING freezes video and drains sound; playback eventually recovers, but can later fail with **HTTP idle timeout**. The 440 Hz test was unavailable because video had not been stopped. This does **not** establish a broken HDMI route or a cold-boot audio failure. The reported processing/interval averages (17.65/35.52 ms) do not identify the slow pipeline stage; the latter measures picture presentation intervals, not Ethernet polling frequency.

Rebuffering holds the consumed-audio/video timeline until playable data is available again. HTTP idle timeout is a separate transport watchdog: eight seconds without observed receive/request-ACK progress, excluding time when a retained packet cannot fit in the application buffer. Neither message alone identifies the server, cable, decoder or DMA as the cause.

**This build repairs specific source defects and adds evidence for the next console run. It has not been tested on physical Xbox hardware here and does not establish uninterrupted playback or eliminate every cause of the reported stalls.**

## Source findings and changes

### Receive servicing

In pinned libXenon `drivers/network/network.c`, `network_poll()` calls `enet_poll()` once. In `drivers/lwip/xenon/netif/enet.c`, that calls `enet_input()` once, which consumes at most **one RX descriptor**. It does not drain all pending packets. XenonFin previously called this once per main-loop iteration, before non-preemptible video decoding and UI work. Fast desktop socket probes do not reproduce that driver limitation.

`native_net.c` now services pending descriptors for at most **16 polls or 2 ms**, checked between packets. An empty interface still receives one poll for TCP/DHCP timers. `driver_guard.c` checks the pinned RX pointer/index (offsets 0/8 on PowerPC), invalidates the descriptor cache line and reads the same little-endian ownership bit as the original driver. It never consumes/reclaims descriptors itself. The existing enet RX/TX implementation, DHCP lease checks and TX ownership guard remain.

The main loop services networking before decoding, after decoding and after UI work, with cooperative audio refills taking priority around it. No packet-arrival wait or audio-path sleep was added; no media/TCP window capacity was enlarged. The per-packet callback remains non-preemptible, so the time limit is a yield check between packets, not a hard interrupt deadline.

### Receive-window recovery

Pinned lwIP 1.4.1 `tcp_send_empty_ack()` clears ACK flags, calls `ip_output()` and discards its return value. If the TX ownership guard returns ERR_MEM, a receive-window reopening ACK can be lost. The application's old `tcp_output()` retry covered only unacknowledged outgoing request bytes, which are normally finished long before a movie is playing.

After accepting a previously refused pbuf, the client now re-advertises its receive window if no subsequent packet arrives for **250 ms**, retrying at most four times per second until data arrives or the existing watchdog expires. This uses the pinned PCB's `TF_ACK_NOW` and `tcp_output`, outside callbacks on the existing sole NO_SYS owner. These ACK attempts do **not** count as receive progress and cannot extend the eight-second idle deadline.

Capacity backpressure now records the **exact retained pbuf length**. Its timeout exemption compares application free space against that length, rather than TCP_WND. ERR_MEM still neither acknowledges nor frees the pbuf; lwIP retains and retries it. Control requests retain their 20-second total deadline; long streams retain their existing unlimited body duration and eight-second connect/idle watchdogs.

These mechanisms are source-level receive weaknesses. Without the original console's final RX/PCB snapshot, they do not prove that its last timeout had either cause; a stopped server transcode or real network interruption remains distinguishable in the new trace.

### Independent sound-driver repair and diagnostic tone

The sound driver actually linked from the pinned library read **0xea001600 (descriptor address)** when setting the run bit, then wrote that address into **0xea001608 (control)**. The vendored pinned copy reads/writes control at **base+8**. Source and the old/new PowerPC instructions confirm the register mismatch. It is independent of the user's clarified tone ownership issue, and is not asserted to be the cause of the rebuffering.

The original GPIO/HDMI setup, 48 kHz stereo S16LE format, 65,536-byte ring, 32 descriptors, cache operations and queue arithmetic remain. Descriptors/buffer remain aligned in the linker's first-32-MiB region. The existing stop/reset writes now precede memory changes on explicit idle reinitialisation. Provenance and BSD notice are bundled in `third_party/libxenon_sound` and [LIBXENON_SOUND_LICENSE.md](LIBXENON_SOUND_LICENSE.md).

Audio output **A** now runs a cooperative 200 ms, 440 Hz PCM test in complete 2,048-byte descriptors. It keeps UI/network servicing alive and reports a two-second DMA-stall error. It never shares output with active music/movie playback. **Y** can reinitialise audio only after playback/tone ownership has ended; normal successful playback does not trigger automatic resets. No reset is needed merely because a movie is buffering.

## Diagnostics and exact console test

1. Download [the ELF](../dist/xenonfin-xell-video.elf), copy as **xenon.elf** to the existing XeLL boot device and verify **XENONFIN 0.7.2**. Use the same server, movie and HDMI setup as the failing run. Ethernet → initialise, sign in, Libraries → movie details → **A**. Server transcoding requirements are unchanged: MPEG-1 video plus MP3 audio in progressive MPEG-PS; see [NATIVE_AV.md](NATIVE_AV.md).
2. Keep **mode C (both)** initially. **A/Start** pauses/resumes, **X** stops, **B** returns. With playback and cleanup stopped, left-stick click cycles C → A (video only) → B (audio only); **Y** replays the selected movie. Compare modes only if stalls persist.
3. **Left/Right** cycles **16 pages**. Existing pages 1–14 retain video, GPU, drop, timing, network, audio, DMA, PTS and stage diagnostics. Page 10 has MP3 average/max decode cost; page 11 has maximum audio service gap and wait reason; page 9 has PCM/DMA/audio ES levels.
4. **Page 15** retains the latest unexpected DMA-drain/rebuffer event after recovery. It shows an observed wait classification, PCM/audio ES/video ES/PS occupancy, usable picture count, hold duration, service gap and time since PCM submission. The most recent 16 events are retained in a bounded ring. These labels describe the state at the event, not a guaranteed root cause. User pauses and initial preroll are excluded.
5. **Page 16** shows TCP receive/announced window, retained pbuf size, blocked packet size, idle-watchdog age, PCB state/flags and reopening ACK attempts. RX batch/peak, budget yields, maximum service gap and TX-busy count are cumulative **since network initialisation/boot**, not reset per movie. The TCP snapshot is retained when a request ends. A full PS buffer/backpressure is different from empty buffers with an open receive window and no incoming bytes.
6. If a timeout occurs, preserve **pages 11, 12, 15 and 16** before starting another item. After stop/error and reporting cleanup, right-stick click in Video saves **xenonfin-av.csv** and **xenonfin-video.csv** using the existing FAT USB path discovery. Keep the packaged **xenonfin-test.m1v** at the USB root so discovery can find that volume. The AV file adds retained TCP state and all rebuffer records. An open event's saved duration is zero until recovery; its timestamp and `open` flag identify an unfinished hold. Do not start another clip before saving. Check the matching server transcoding/FFmpeg log if bytes stopped with an open receive window.
7. Play continuously for **at least ten minutes**, checking audible continuity, visible motion, real FPS, audio underruns, rebuffer count and clock phase. Then test a 20-second pause, resume, stop and return to Libraries. A five-second interruption should rebuffer/recover; an outage over eight seconds may correctly terminate with the unchanged idle timeout. Verify MP3 music, library navigation and the existing CPU/GPU local-video tests.
8. For the optional tone check, stop playback and wait for cleanup. Go to **Audio output**, press **A** and hear the beep. The page shows route, raw DMA base/control/status and queued/free bytes. A drained counter alone is not proof of audible output. **Y** is an idle-only recovery tool if the tone/DMA actually fails, not a workaround for repeated network buffering.

Success requires actual continuous console sound/pictures with stable synchronisation. The host results below cannot substitute for this run.

## Build and verification

```sh
DEVKITXENON=/workspace/setup/devkit make xenon
make -C platform/xenon/tests all test
make -C platform/xenon/tests SANITIZE=1 all test
JELLYFIN_VIDEO_STREAM_PROBE=build/native-tests-sanitized/jvideo-probe \
  python3 tests/native_video_server.py --extended --metrics /tmp/av-072.json
python3 tests/native_server.py --music
```

Native fault tests cover bounded RX bursts and time yields, descriptor ownership inspection, exact-pbuf capacity timeout exclusion, bounded reopening ACK attempts without fake progress, real idle termination, latched rebuffer/recovery and existing cancellation/cleanup. Driver MMIO tests exercise the same vendored sound source with host boundary adapters across different descriptor physical addresses; restoring the old register read fails the control-preservation assertion. Those tests cannot establish physical register behavior or audible HDMI output.

[Recorded 0.7.2 host results](native-av-072-metrics.json) are separate from [historical 0.7.1 ten-minute results](native-av-recovery-metrics.json). The server probes use production HTTP/PS/MP3/MPEG-1/scheduler code with host socket/display/DMA adapters; the Xbox NIC and physical DMA are covered only by source/disassembly inspection and boundary tests here. The toolchain, graphics backend and video scheduler remain unchanged.

All native suites, ASan/UBSan suites and 15 sanitized real-Jellyfin cases passed for this build. Normal movie, episode and simulated console-cost cases displayed 240/240 frames with zero drops or audio underruns. The forced CPU-stall case recorded four drain events and recovered all frames; the five-second network interruption recorded one and recovered. The 20-second pause passed without an HTTP timeout. Eight flash/beep pairs had a maximum host-model error of 458 microseconds. Existing real-server authentication/library/music tests passed. These are bounded host checks, not a new ten-minute physical console result.

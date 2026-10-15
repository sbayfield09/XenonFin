# XenonFin 0.4 / 0.4.2 — native music

The user confirmed **0.3** on a real Xbox 360 through XeLL: graphics/controller, audio diagnostics, Ethernet/HTTP, authentication, library discovery, movie pagination and metadata/details. Version 0.4 extends those sources and the same pinned libXenon toolchain. It does not recreate the project or use the desktop player.

**The user has now confirmed actual MP3 music playback works on Xbox 360 in 0.4.** The reported issue was flickering white text over a stable blue background. The initial 0.4.1 presentation fix froze menu updates while B still exited. Version 0.4.2 removes its GPU swap dependency and copies only changed pixel regions; see [NATIVE_RENDERING.md](NATIVE_RENDERING.md) for the diagnosis and required console regression checks. No physical console is attached to this development environment. Broader format/control tests below remain useful regression checks.

## Download and console test

Download [xenonfin-xell-music-fix.elf](../dist/xenonfin-xell-music-fix.elf), verify it against `dist/SHA256SUMS`, rename it **xenon.elf**, and boot from your FAT32 USB using the working XeLL method. The existing diagnostic archive now contains 0.4.2 and these instructions. Keep the unchanged [0.4 music build](../dist/xenonfin-xell-music.elf) and [0.3](../dist/xenonfin-xell-browse.elf) for rollback; their bytes are unchanged.

1. Initialize **Network / DHCP diagnostics** and sign in through the existing server/login screens. Your LAN example remains `192.168.68.202:8096`. Server, credentials and token remain RAM-only. This native build still uses numeric IPv4 HTTP on a trusted LAN; it has no TLS, hostname or reverse-proxy base-path support.
2. **Browse an album:** open Libraries → your actual music library → artist → album. A opens each level. Up/Down selects; Left/Right pages; Y shows details; B goes back. Start at the music library level cycles Artists/Albums/Tracks. Track selection shows title, artist, album, duration and track number. Titles and types occupy aligned columns; long titles get ellipses. Details use word wrapping and Up/Down overview scrolling within the detected console dimensions.
3. **Select a track:** press A on an Audio row, or A on its details. The dedicated **Now playing** screen opens. It negotiates a real Jellyfin media source, sends an authenticated stream request, receives MP3 bytes, decodes them and submits PCM to the existing audio driver. Initially use a known local MP3 song, preferably 44.1 kHz stereo, several minutes long. Select a recognizable song rather than judging by the diagnostic tone or a counter.
4. **Hear actual music:** verify the recognizable recording is audible through your normal TV/receiver connection, at the correct pitch and speed, with both channels working. Check MP3 frame/PCM counters increase and progress follows the sound. Test 44.1 kHz mono, 44.1 kHz stereo and 48 kHz stereo as available. The user confirmed audible MP3 playback in 0.4; verify it remains uninterrupted with the rendering fix.
5. **Pause/resume:** A or Start toggles pause. Already submitted DMA data can drain for up to about **171 ms**; then progress must remain still. Resume must continue the song rather than restart it. Leave it paused for at least 30 seconds, then resume. Bounded receive buffers can fill during pause; lwIP backpressure intentionally stops further receipt without blocking controller input.
6. **Skip tracks:** Left/Right on Now playing selects previous/next within the current album or music Tracks view. It closes the old stream, drains queued sound, reports stop, fetches the neighbor using its server index and negotiates the next stream. Test an album longer than 10 tracks, including the page boundary. Playback also advances naturally to the next album track. End of the final track stops. Switching is not gapless.
7. **Stop:** X on Now playing stops and reports the final played position. The same short DMA drain applies. A paused stream can be stopped. To replay a stopped song, return to the browser and select it again.
8. **Return to the browser:** B from Now playing returns to Libraries while music continues. Use **Back/View** to reopen Now playing; LB/RB still changes sections. Continue movie browsing/details during music and check there are no audible gaps while changing screens. A on another Audio row switches to that track after the old session is finished.
9. Recheck the original graphics, controller, DHCP/server/login and audio diagnostics. On the Audio diagnostic page, A stops active music first; press A again after it stops to run the original tone. Logging out or changing server/account cancels music and erases its copied token and buffers. B exiting the application also clears session state.
10. Test server shutdown, cable loss, invalid/denied content, session expiry and cancellation during startup. Errors must be visible, buffers must remain bounded and the UI must respond. HTTP 401 stops music and clears the session; sign in again. There is no automatic retry loop that keeps submitting credentials. Reopen a track after restoring connectivity. An audio DMA drain timeout requests a reboot rather than waiting forever.

Record console model, output connection/video mode, server version, source track rate/channels/format, stream HTTP status, audible result, pause/resume behavior, progress, rebuffer count, skip sequence and any error. Never share tokens, passwords or a raw stream URL. Long-running playback, physical sound quality, DMA timing and the new batched console drawing still require real-console checks.

## Native implementation and bounds

All production components below compile into the XeLL PowerPC ELF:

- `music.c`: cooperative playback state machine, PCM output, device-queue clock, pause/stop/skip, album neighbor fetching and Jellyfin reports. UI, DHCP, metadata requests and stream callbacks run on the same existing main loop. No threads, blocking read loops or full-file downloads are added.
- `audio_decode.c`: pinned **minimp3** scalar MPEG Layer III decoder, incremental ID3 skipping, mono expansion and rational-phase linear sample-rate conversion. Inputs from 8–48 kHz become **48,000 Hz, signed 16-bit stereo PCM, little endian**. Output uses explicit byte packing on big-endian Xenon. Conversion state spans MP3 frames; fractional positions do not accumulate drift. Encoder delay/padding is not removed for gapless playback; a small leading/trailing interval can remain. Linear interpolation is a simple first-stage resampler, not a claim of mastering-quality conversion.
- `tcp_client.c` / `http.c`: the existing raw lwIP client and framing parser now also support bounded streaming sinks and authenticated JSON POST/DELETE. Metadata responses retain their **32 KiB** limit. Audio bytes go to a **128 KiB compressed ring**, not that JSON buffer. A refused incoming pbuf is retained by lwIP with **ERR_MEM**, without consuming or acknowledging it; a retry is processed once after space returns. Content-Length, chunked and connection-close framing are supported.
- PCM uses a **128 KiB software ring**. The existing libXenon **64 KiB DMA ring** receives only complete **2,048-byte descriptor blocks**, with at most **32 KiB queued** and free-space checks. Only the final incomplete PCM block is padded. Pause/stop stop adding PCM and let that bounded device tail drain; no undocumented audio MMIO or replacement audio driver is used. A pending stop/skip has a two-second hardware-drain watchdog.
- Progress is submitted output frames minus the audio driver's unplayed bytes, clamped to valid submitted frames. It advances with device consumption, not network arrival or wall time during a stall. The driver reports descriptor-level progress; sub-descriptor timing precision and real audio-output latency need hardware validation.
- Decoder work is capped at eight MP3 frames per tick. UI rendering no longer invokes a second decoder tick: audio is serviced in the normal cooperative loop independently of whether a frame is drawn. JSON responses, paths, history and queues remain bounded. Next/previous fetch **one** neighboring item rather than allocating an entire album. Direct MP3 requests can be up to **512 MiB**, incrementally processed; ID3v2 tags are skipped up to **16 MiB** without buffering the tag. Unsupported/invalid streams fail visibly.
- JSON/control requests retain connect/idle **8 s** and total **20 s** deadlines. Music also has an **8 s** connect/idle deadline and **20 s** header deadline, with **no 20-second song cutoff**. Idle time is suspended only while receive capacity is intentionally exhausted; after capacity returns, a real stall times out. Link/DHCP loss and cancellation close the connection. No credential-bearing redirects are followed.
- `text.c` implements sanitized fixed-width cells, ellipses and word wrapping. The inherited ASCII console uses `?` for unavailable Unicode glyphs. `console_batch.c` captures printf/direct characters into a complete character grid. `console_frame.c` updates changed glyphs in the free off-screen surface; `framebuffer.c` reuses the libXenon font/tile layout and original framebuffer, copying changed cache lines from completed CPU-side surfaces without hardware swap waits. The former memdcbst wrapper is removed; NIC/audio cache operations are untouched. See NATIVE_RENDERING.md.

The browser still handles movie/TV/music/other libraries; A on a Movie/Episode still opens details and never invokes this audio player. No native artwork or video decoder has been added.

## Jellyfin API behavior

1. `POST /Items/{id}/PlaybackInfo` includes the authenticated user, a native **MP3-only audio device profile**, DirectPlay and progressive HTTP MP3 transcoding support. This is not the desktop FFmpeg profile. Local MP3 direct play is accepted only after the server says it is supported and the returned codec/rate/channels are compatible. Artwork attachment streams are not mistaken for actual video content.
2. Direct play uses `GET /Audio/{id}/stream.mp3?Static=true&MediaSourceId=...&PlaySessionId=...`. Otherwise, a server-negotiated **relative progressive HTTP audio URL** is used, with MP3 decoding still required. Foreign origins, HLS/DASH and redirects are refused. Any API key in the negotiated URL is removed; the existing token is supplied through the same `Authorization: MediaBrowser ... Token="..."` header as authenticated browsing.
3. Reports use `POST /Sessions/Playing` after PCM first reaches the device, `POST /Sessions/Playing/Progress` every five seconds and on pause/resume, and `POST /Sessions/Playing/Stopped` after drain. They include item/source/session IDs, played position, pause state and DirectPlay/Transcode method. Seeking is advertised as **false** because it is not implemented. Reporting failures are shown separately and do not block the audio path. Normal stop/skip/natural-end requests clean up a transcode with `DELETE /Videos/ActiveEncodings?DeviceId=...&PlaySessionId=...`. Logout prioritizes immediate cancellation and token erasure, so a pending final report may be cancelled.

Progressive MP3 transcoding requires the user's Jellyfin audio-transcoding permission and a working server encoder. Direct MP3 should be tested first. The test server additionally verified FLAC → MP3, but other formats and every Jellyfin version are not claimed tested. There is no seek, persistent queue, shuffle/repeat, saved session, transparent network resume or gapless playback in this stage.

## Decoder investigation

| Option | Findings and decision |
|---|---|
| minimp3 | Small single-header C decoder, low-level per-frame API, no allocation in that API, scalar fallback; CC0. Selected with `MINIMP3_NO_SIMD` and `MINIMP3_ONLY_MP3`, compiled for the existing Xenon target. Existing floating-point support is used; no x86/ARM intrinsics are required. |
| dr_mp3 | Viable single-header alternative with callback I/O, `DR_MP3_NO_STDIO`, `DR_MP3_NO_SIMD`, public-domain/MIT-0 licensing and a low-level decoder API. The reviewed header is about 209 KiB versus minimp3's 77 KiB, and includes higher-level I/O/seek/allocation machinery unnecessary for this cooperative pipeline. It was inspected, not imported or benchmarked on Xbox. |
| mpg123 / historical XMPlayer integration | The existing reference tree shows a full mpg123 library adapter. This could support a broader decoder runtime, but would require an additional cross-built library/integration. Its presence in reference sources does not establish a working library in this pinned devkit. No old desktop/MPlayer backend was substituted for native decoding. |

minimp3 is vendored unchanged at commit `ea99364f61c14656440e8d77e9c233ccf3124633`; see [THIRD_PARTY.md](THIRD_PARTY.md) for hashes/license. Successful cross-compilation establishes ABI/build compatibility, while audible MP3 output is now user-confirmed; wider format and sustained-performance measurements remain hardware tests.

## Build and validation

```sh
DEVKITXENON=/workspace/setup/devkit make xenon
make native-test
make -C platform/xenon/tests SANITIZE=1 all test
python3 tests/native_audio.py
python3 tests/native_server.py --music
# Optional: run those same integrations with the sanitizer binaries:
JELLYFIN_DECODE_PROBE=$PWD/build/native-tests-sanitized/decode-probe python3 tests/native_audio.py
JELLYFIN_NATIVE_PROBE=$PWD/build/native-tests-sanitized/probe \
JELLYFIN_MUSIC_PROBE=$PWD/build/native-tests-sanitized/music-probe python3 tests/native_server.py --music
bash scripts/package.sh
```

The native output remains `build/xenon/xenonfin-boot.elf32`; packaging exposes `dist/xenonfin-xell-music-fix.elf`, retains the original 0.4/0.3/0.2 downloads and writes checksums. The source archive includes the vendored decoder/license. Packaging still requires the existing desktop binary for its separate historical desktop archive; the native player does not link desktop code.

Validation evidence:

- Native API/parser/keyboard/browsing/driver suites plus music format, buffering, layout, complete-frame isolation and framebuffer compatibility and unavailable-register regression tests pass. Fault injection covers expired authentication, forbidden/redirected/HTML/invalid streams, idle/link failure, cancellation, skip across a ten-item boundary and a stuck DMA queue. Streaming tests check refused-pbuf ownership, exact retry consumption and a backpressured transfer beyond 20 seconds.
- Actual compressed MP3 fixtures decode through the same scalar C decoder linked into the ELF at 8, 16, 22.05, 32, 44.1 and 48 kHz, mono/stereo. Exact output frame ratios, non-silent PCM, mono-channel equality, expected spectral frequencies, a 60-second conversion and a 300 KiB ID3 tag pass. AddressSanitizer and UndefinedBehaviorSanitizer runs pass without suppression.
- A disposable official Jellyfin **10.10.7** instance passes the previous public-info/login/restricted-user/movie/TV/music/other browsing tests, then authenticates actual native stream requests. The actual native player/HTTP/decoder/reporting sources run through a host POSIX-TCP bridge and a deterministic audio-queue backend: 44.1 kHz mono MP3, 48 kHz stereo MP3, pause/resume with frozen progress, next/previous, natural EOF and auto-advance, explicit stop, FLAC → progressive MP3 and transcode cleanup all pass. The server accepts report HTTP **204**, records play counts and clears now-playing state after stop. The bridge is test-only; the ELF uses real lwIP and `xenon_sound_submit`.

The host checks validate source/build, compressed-data decoding and API behavior. The user independently confirmed audible 0.4 music on Xbox. The 0.4.2 presentation changes still need the console checks in NATIVE_RENDERING.md; host tests cannot establish physical display behavior.

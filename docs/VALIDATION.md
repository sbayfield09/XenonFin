# Validation — 14 October 2026

## Native 0.7.5 DMA startup and ownership

The integrated 0.7.4 app failed before any media decode; standalone sound is confirmed working. 0.7.5 adds validated startup, explicit tone/music/movie leases and retained register snapshots using the original SDK driver. [Diagnosis, software evidence and cold-boot test](NATIVE_AUDIO_075.md). **The reported `00008000` status itself is empty; the earlier failing sample was not retained, so the console trigger still needs confirmation. Physical 0.7.5 tone/media output is unverified.** Older results below are historical.


## Native 0.7.4 complete audio integration

The user confirms correct sound from the 0.7.3 standalone test. Version 0.7.4 unifies standalone/tone/music/movie output through the original SDK driver and a shared checked adapter; it adds consumed-audio timestamp-origin alignment without PCM trimming, bounded recovery hysteresis and DMA/decoder diagnostics. The video/transport/demux implementations remain unchanged. [Integration and final hardware test](NATIVE_AV_074.md). **Physical 0.7.4 music/movie continuity and synchronisation remain unverified.** Earlier results below are historical.


## Native 0.7.3 audio regression rollback

Real hardware found that 0.7.2 corrupts both the PCM tone and movie audio. The sound-driver override and its control/reset changes are removed; both new ELFs link the original pinned `libxenon.a(sound.o)` used by earlier working releases. An isolated PCM ELF excludes networking/codecs/video/A/V code and measures verified sine samples and consumed-DMA timing. **First gate: clean, correctly pitched 440 Hz on the console, still pending.** [Backend comparison, diagnostics and ordered tests](NATIVE_AUDIO_073.md). Video/streaming/decoder/scheduler sources are unchanged. Historical 0.7.2 host tests did not validate the physical hardware-control assumption.


## Native 0.7.2 receive/rebuffer update

The user confirms real HDMI movie audio/video are in sync while PLAYING, but repeated rebuffering eventually ends in an HTTP idle timeout. The earlier missing 440 Hz tone was caused by active video ownership. Version 0.7.2 batches pending Ethernet RX descriptors with a bounded service budget, services networking around codec/UI work, retries reopened receive-window updates, and preserves rebuffer/TCP snapshots. It also repairs an independently verified sound-driver control-register read and provides a cooperative tone/idle reset. [Source findings, evidence and hardware procedure](NATIVE_AV_072.md). **Continuous Xbox playback and the new driver/network changes still require physical testing.** Earlier release results below are historical.


## Native 0.7.1 recovery update

Real Xbox testing found 0.7 A/V stutter and an application-generated stall/error. Version 0.7.1 fixes a deterministic frozen-master/full-video-preroll dependency, prioritises cooperative PCM refills, retains buffer capacities, and adds A/B/C comparison modes plus DMA/HTTP/PTS/stage diagnostics. [Exact cause, controls, ten-minute acceptance and test evidence](NATIVE_AV_071.md). Hardware acceptance remains pending; historical host results below cannot establish physical sound or A/V synchronisation. Current [0.7.1 counters](native-av-recovery-metrics.json): 15 sanitized real-server cases and a 600-second host run with 18,001/18,001 video frames, zero drops/audio underruns/bridging/late trims. Injected CPU/network starvation was counted and recovered; these are host models.


## Native 0.7 update — 15 October 2026

The user confirmed 0.6 real Xbox streaming/GPU MPEG-1 at approximately 720×414/30 with zero drops. Native 0.7 adds actual MP3 movie decode/PCM and a hardware-consumption audio master to that pipeline; **physical movie audio and synchronisation remain pending**. [Implementation, server requirements and exact flash/beep hardware test](NATIVE_AV.md).

Production native C checks cover raw MP3 in MPEG-PS, shared 33-bit PTS wrap, sample-exact alignment, DMA consumption rather than submission, frozen/slower output clocks, PCM assembly, joint pause/rebuffer, stop reporting and malformed audio/headers. Disposable real Jellyfin 10.10.7 + FFmpeg 7.1.5 tests exercise decoded movie/episode PCM (mono and 5.1→stereo), known flash/beep timing, a 20-second pause, five-second network interruption/recovery, video-only fallback, positive offset, early audio end and timestamp-gapped sources. Source gaps are placed by PCM timestamp mappings; the continuous long benchmark resamples copied AAC loop boundaries into a continuous input instead of mislabelling synthetic gaps as client clock drift. The continuous five-minute host run displays 9,000/9,000 frames, decodes 12,502 MP3 frames and consumes 14,401,536 stereo PCM frames, with zero drops/underruns/bridging/late trims/timestamp corrections; [recorded counters](native-av-metrics.json). Host device/display adapters cannot establish real Xbox output latency. Existing native music, decoder/colour, renderer, network/login/browsing and ASan/UBSan suites remain enabled.

## Native 0.6 update — 15 October 2026

The user confirmed native 0.5.2 on Xbox: **720×480/30, 240 decoded, 240 presented, zero dropped**, correct colours, timing and the working audio/network/library baseline. Native 0.6 adds actual Jellyfin movie/episode MPEG-1 streaming in C without replacing that decoder/scheduler/renderer. **New streamed physical Xbox frames and five-minute stability remain pending.** [Server configuration, host evidence and exact physical test procedure](NATIVE_VIDEO_STREAMING.md).

The updated PowerPC XeLL ELF cross-compiles with the existing pinned libXenon/Xenon GCC 16.2 toolchain. Host checks use the production native C HTTP/parser/demux/codec/player and RGB converter, with only host TCP/display adapters: disposable real Jellyfin 10.10.7 + FFmpeg 7.1.5 transcodes H.264/AAC MP4 to MPEG-1/MPEG-PS. Movie and episode each decode/present **240/240** frames at real-time host speed, zero drops/underruns; RGB content changes, pause/resume, authenticated start/progress/stop and encoding cleanup pass. A five-minute **real-time host** run decoded/presented **9,001/9,001 frames**, zero drops/underruns, 99,086,336 bytes received with PS storage never exceeding 524,288 bytes; [recorded counters](native-video-streaming-metrics.json). Cancellation and invalid sessions/containers/framing/EOF/timeouts/link loss pass native fault suites. Incremental MPEG-PS decoding matches local decoded pixels for all five existing codec fixtures, including I/P/B and fractional rates. ASan/UBSan native suites and real-server probes pass. Existing real Jellyfin login/browsing, MP3 mono/stereo, FLAC→MP3, pause/skip/end/report/cleanup regressions pass after the shared HTTP changes. These do not measure the Xbox GPU or establish physical A/V output.

The rest of this page retains the earlier desktop and baseline validation history.

This page records the initial desktop/baseline validation. **The user subsequently confirmed 0.3 on real Xbox hardware:** boot, graphics/controller/audio diagnostics, networking/authentication, libraries, movie pagination and metadata/details. Version 0.4 adds the native MP3 pipeline documented in [NATIVE_MUSIC.md](NATIVE_MUSIC.md): build, waveform/resampling, sanitizer and real Jellyfin direct/transcoded playback/reporting tests passed on the host. **The user subsequently confirmed actual audible MP3 playback works in 0.4, with text-only flicker.** The 0.4.2 recovery removes the GPU swap wait that froze 0.4.1 menus (B still exited on hardware), and builds with the same toolchain and passes native/sanitizer renderer and existing protocol suites, plus real Jellyfin music integration. See [NATIVE_RENDERING.md](NATIVE_RENDERING.md) for the source-level cause and pending console presentation checks. Earlier hardware-unverified statements below refer to the original cloud validation.

The runnable target verified here is the **Debian amd64 desktop client**. The Xbox artifact was cross-compiled and inspected, not booted. No XEX or Xbox playback benchmark exists.

## Completed checks

| Check | Result |
|---|---|
| `make -j3 DEPS_ROOT=/workspace/setup/sysroot all` | Desktop app, core tests and UI test harness compile and link |
| `make DEPS_ROOT=/workspace/setup/sysroot test` | 57 passed, 0 failed |
| `make DEPS_ROOT=/workspace/setup/sysroot sanitize` | 57 passed, 0 failed with AddressSanitizer/UndefinedBehaviorSanitizer and leak detection |
| `python3 tests/integration.py` | 63 core/network cases passed, plus codecs, artwork, UI and TLS checks below |
| Controller input | SDL virtual controller A, D-pad focus, initial hold delay and repeat verified |
| Controller keyboard UI flow | Server entry, masked login, browsing, details, video render, return to details, search, settings and persistence passed through the actual application state machine |
| HTTP behavior | Bounded response, cancellation, connection-reset recovery, same-origin redirects, cross-origin credential protection and POST redirect refusal passed |
| Credential origin isolation | Saved credentials are not forwarded when a different server is selected; public probes do not send a saved token |
| TLS | Self-signed server rejected; supplied public server reached with certificate/hostname verification enabled |
| Artwork | Asynchronous PNG download, 600×900 → 240×360 resizing and repeated texture rendering passed |
| Music | HTTP MP3, AAC/M4A and FLAC decoded to stereo PCM; 48,000 / 48,128 / 48,000 output samples respectively for one-second synthetic clips |
| Player controls | Byte-range HTTP seek and pause/resume exercised against a real generated MP4 stream |
| `python3 tests/real_server.py` | Passed against a disposable official Jellyfin 10.10.7 server and temporary account |
| Real-server workflow | Login, two library views, four media items, real search, playback negotiation, static stream, server-side HEVC transcode, MP3 and start/progress/stop reporting passed |
| Session persistence | Saved token reused for real-server UI startup; private settings roundtrip/logout/remember-login opt-out tested |
| `DEVKITXENON=/workspace/setup/devkit make xenon` | Native diagnostic cross-compiles and links |
| Native artifact inspection | ELF32, big-endian, PowerPC, static, stripped; upstream-adjusted entry point `0x2300` |
| Setup repeatability | Rootless install script rerun successfully with cached, checksum-verified toolchain/server layers; core tests passed |
| Cloud launcher | `bash scripts/run_cloud.sh --headless --seconds 1` renders and exits successfully |

The standalone real-server test creates only synthetic movies/music and its own temporary account. It verifies actual server watch history updates after playback. The HEVC case asserts that the negotiated method is `Transcode` and the **received** codecs/container are H.264/AAC MPEG-TS, rather than merely trusting a requested profile. Local test servers terminate on completion.

## Measured desktop sample results

These are short, synthetic, headless runs using SDL **dummy video and audio**. The cloud machine has an amd64 CPU, not a Xenon CPU. Clock error is the difference between video presentation timestamps and the queued-audio clock; it is not a measurement of physical speaker/display latency. Dummy audio scheduling and startup time affect wall-clock FPS. A report of zero dropped frames for one run is not a universal performance guarantee.

| Source | Received format | Decoded / displayed frames | Dropped | Audio underruns | Max video/audio-clock error | Wall time |
|---|---|---:|---:|---:|---:|---:|
| Local generated 720p30 clip | H.264/AAC MP4, 1280×720 | 90 / 90 | 0 | 0 | 20 ms | 2.885 s |
| HTTP fixture 720p30 clip | H.264/AAC MP4, 1280×720 | 90 / 89 | 1 | 0 | 20 ms | 2.903 s |
| Real Jellyfin static stream | H.264/AAC MP4, 1280×720 | 90 / 88 | 2 | 0 | 20 ms | 3.055 s |
| Real Jellyfin HEVC conversion | H.264/AAC MPEG-TS, 640×360 | 90 / 90 | 0 | 0 | 20 ms | 4.192 s including transcode startup |

The static 720p samples generated approximately 144,384 stereo sample frames. The real-server two-second MP3 test generated 96,000 stereo sample frames. Complete machine-readable measured counters are in [validation-metrics.json](validation-metrics.json).

## User's server

After the environment network update/reconnect, `--probe https://jellyfin.stanleybayfield.co.uk` succeeded over verified HTTPS and returned the server name `Stan-Jellyfin`. Only the public information endpoint was used. No account credentials were configured, so authentication, library access or playback on that server was not tested. The earlier cloud proxy rejection was resolved; it was not treated as a Jellyfin authentication failure.

The alternate `192.168.68.202:8096` address requires a real LAN/VPN route from the cloud. It has not been validated. No VPN, router, server account, library or watch history on the user's server was modified.

## Not verified / remaining

- Xbox launch, display/controller/Ethernet/PCM behavior, console HTTP/TLS/UI integration, and a console decoder/player are not verified or complete.
- No Xbox 480p, 720p or 1080p playback, real A/V drift measurement, sustained bitrate, CPU/memory measurement, or long-running stability test was performed.
- Physical desktop sound/display output, desktop 1080p decoding, HDR tone mapping, every Jellyfin version, and every codec combination were not tested.
- HLS/DASH, cross-origin media, subtitles, gapless audio and transparent midstream reconnection are unsupported. Queue length is capped at 200 tracks.
- The first validation ran in an initially empty working repository. Subsequent commits and pushes are recorded in Git history; source archives omit Git metadata.
- Installation/startup instructions are saved in the environment draft. Saving them does not publish a snapshot. Review/save and publication remain product actions; a fresh task restore has not been tested.

The requested native Xbox 360 definition of done is **not satisfied**. PLATFORM.md describes the substantive native integration and hardware work required next.

## Original native 0.5 host evidence (before console testing)

The user confirmed stable text rendering and the established MP3/network/Jellyfin features. Native MPEG-1 decoding, scalar color conversion, a two-frame scheduler, local FAT USB source adapter and Xenos texture/private-target integration now cross-compile using the same toolchain. Player/source/renderer/framebuffer regression suites pass normally and with ASan/UBSan. Real compressed video produces 240/240, 48/48 and 60/60 frames matching FFmpeg within the documented pixel tolerances. Existing real Jellyfin music integration still passes. See [NATIVE_VIDEO.md](NATIVE_VIDEO.md). USB, GPU execution, visible motion and native performance remain untested on physical Xbox hardware in this environment.

## Native 0.5.1 colour and timing evidence

The user confirms 0.5 MPEG-1 decode and both renderers on Xbox, with wrong CPU colours and dropped frames. 0.5.1 follows the pinned libXenon big-endian B,G,R,X framebuffer byte ABI and has independent known-colour tests: compressed patches through the real decoder/tiled converter differ from FFmpeg RGB by mean 0.487/255 and maximum 3/255. Actual 320×240/30, 640×360/30 and 720×480/30 fixtures decode completely with unchanged strict YUV tolerances. The 720×480 final macroblock exposed and now covers a fixed-memory EOF lookahead bug.

A prepared-frame scheduler model including the reported 3.3 ms decode cost, render/copy costs and 4 Hz UI stalls displays all 240 frames without drops at the expected duration. Stage metrics separate conversion, upload, commands, completion bounds/poll uncertainty, copy, UI and cooperative waiting; VSync waits are disabled. Normal/sanitized native suites and real Jellyfin login/browse/music integration pass. **No 0.5.1 physical colour, frame-rate or CPU-utilisation measurements have been made in this environment.** See [NATIVE_VIDEO.md](NATIVE_VIDEO.md) for the pending hardware matrix.

## Native 0.5.2 timing/drop evidence

The user confirmed corrected CPU colours in 0.5.1 and reported 218/240 frames at 720×480/30. Source inspection found the native timebase conversion divided 49.875 MHz by a truncated 49 ticks/µs (about 1.7857% fast), and composed UI printf still traversed libXenon's blocking UART path. Both are corrected and directly regression-tested. Absolute clock origins, exact MPEG rational PTS, deadline-aware cooperative service, reasoned drops, percentile histograms and bounded CSV trace pass native tests. VBlank gating/half-refresh modes pass simulated 60 Hz/rejection/timeout tests; no physical retrace test is claimed. Real codec/colour comparisons, ASan/UBSan and real Jellyfin login/browse/music integration pass. Hardware costs plus added I-frame/UI spikes produce 240/240 presentations in the host timing model. All 0.5.2 console frame-rate/tail/retrace/drop-attribution results remain pending; see [NATIVE_VIDEO.md](NATIVE_VIDEO.md).

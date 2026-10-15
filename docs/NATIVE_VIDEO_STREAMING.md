# XenonFin 0.6 — native Jellyfin video streaming

The user confirmed 0.5.2 **on Xbox 360**: correct CPU/GPU colours and 720×480/30 local MPEG-1 playback, 240 decoded, 240 displayed, zero dropped. Version 0.6 connects that decoder, frame scheduler and renderer to authenticated Jellyfin movie/episode streams. **The new streaming ELF still requires physical testing.** An HTTP 200, a host RGB conversion or a successful cross-build cannot establish Xbox video playback.

## Downloads and first console test

- [XeLL ELF](../dist/xenonfin-xell-video.elf) — copy to FAT USB as **xenon.elf**.
- [Boot archive](../dist/xenonfin-xbox-boot-diagnostic.tar.gz) — ELF, local regression clip, licences and instructions.
- [Source archive](../dist/xenonfin-source.tar.gz); [checksums](../dist/SHA256SUMS).
- [Confirmed 0.5.2 rollback](https://github.com/sbayfield09/XenonFin/tree/30ecf9bcfbae34deb786ccf9514c593221731e56/dist).

1. Boot using your existing XeLL method. Verify **XENONFIN 0.6**. A local test clip is optional for streaming; the ELF still needs a supported XeLL boot source. No console filesystem writes/download cache are used during streaming.
2. Initialise Ethernet and confirm **ACK lease verified: YES**. Enter your LAN server, for example **192.168.68.202:8096**, on the server page, test it, and sign in as usual. This address is not compiled into the application. Native networking still requires numeric IPv4, cleartext HTTP and a root Jellyfin installation; HTTPS, DNS names, proxy base paths and redirects are unsupported.
3. Stop music on **Now playing** with X and let queued PCM drain. In **Libraries**, open Movies, select a movie, open its details, then press **A** to play. Episodes work the same way through series → seasons → episode details. The player negotiates first, buffers, then uses the existing **GPU Y/U/V path** by default.
4. Confirm **actual moving movie images**, correct colours/aspect and silent output. The source may be H.264/HEVC/etc., but only server-transcoded MPEG-1 reaches the decoder. A stream that is not compatible MPEG-PS/MPEG-1 produces an error. It never falls back to the unsupported original file.
5. **A or Start** pauses/resumes after decoding starts. The timer and displayed image must freeze; resume must continue near that position without a burst of late drops. A paused stream fills bounded buffers and TCP backpressure stops further reception. Seeking is not implemented.
6. **Left/Right** changes eight statistics pages. The timer is always in the header. Page 7 shows safe URL *type*, HTTP status, transcode request, throughput, PS/ES occupancy, total runtime and underruns. Page 8 shows original source codecs/container/resolution and network/demux p95/max. Pages 1–6 retain decoder, conversion, upload, GPU observation/copy, VBlank, queue/drop/clock/UI diagnostics. The sampled FPS uses wall time; the `avg` value uses media time, excluding pause/buffering, so also compare the timer with wall time and the underrun counter. Tokens and full streaming URLs are never displayed.
7. **X** stops; wait for server cleanup, then A replays. **B** stops and returns to the current library/details location while cleanup completes. Changing sections also stops video. Server cleanup is asynchronous; wait for it before another video or music session. No dashboard exit is required.
8. Repeat with an SDR movie **longer than five minutes**. Let it run for at least five minutes without pausing. Record initial/steady/end counters, displayed FPS, timer versus wall time, throughput, compressed/decoded queue occupancy, underruns and reasoned drops. Check the Jellyfin dashboard/logs for MPEG-1 encoding and playback progress; after stop the encoder should terminate. A five-minute source should produce about 9,000 decoded frames at 30 FPS. A zero FPS counter after END/PAUSE is not a throughput measurement.
9. Separately test pause for 30 seconds, resume, X stop, replay, B return, episode playback, invalid session, network cable loss and server shutdown. UI/controller input must remain usable. Then verify library browsing and actual MP3 sound/pause/skip/stop. Retain the local CPU/GPU clip tests under **Video / local test**; Up/Down selects CPU/GPU colour bars while stopped, A/Y start local CPU/GPU if no Jellyfin movie is retained. Starting colour bars clears the retained streaming selection so local tests remain reachable.

Record console model, display mode/refresh, revision, Jellyfin/FFmpeg versions, original codecs, source/output dimensions/FPS and which renderer/sync mode is used. Default **VSync off / late threshold 100 ms** matches the accepted 0.5.2 scheduling. The existing stopped LT/RT lateness and VBlank experiments remain available. The USB CSV facility remains a local-test feature and needs the FAT clip/mount; the streaming path does not mount storage.

## Server configuration and verified route

Verified with a **disposable real Jellyfin 10.10.7 server** and this environment's FFmpeg **7.1.5**. No changes were made to the user's Jellyfin installation. Other server versions/builds and the user's own encoder configuration still need testing.

- The user must have library access and permission to play media and transcode video/audio. The server must be able to read the source and write its normal transcode cache.
- The configured FFmpeg needs `mpeg1video` and `mp2` **encoders**, an MPEG-PS (`mpeg`) muxer, and a decoder for the original source. On the server, run `ffmpeg -hide_banner -encoders` and `ffmpeg -hide_banner -muxers` using **Jellyfin's configured executable** to check. The first console comparison should use a known SDR source and software transcoding; hardware encoder support must not be assumed for MPEG-1.
- Keep native HTTP reachable over Ethernet, usually port 8096. Reverse-proxy buffering/time limits must permit progressive long responses; direct LAN access is the initial target. This client uses the server's ordinary documented routes, not a helper service or invented conversion URL.
- HDR, live/infinite inputs, remote media origins, discs, subtitles, changing sequence formats, seeking and video audio are outside this stage. The parser chooses the first eligible local SDR source from up to eight advertised sources; there is no source-picker UI yet.

The client POSTs authenticated **`/Items/{itemId}/PlaybackInfo`** with `UserId`, `IsPlayback=true`, direct play/stream disabled, transcoding enabled and a separate native profile. It inspects `MediaSources`, `MediaStreams`, source/session IDs, runtime, original container/video/audio codecs/resolution and progressive transcoding support. It requires `SupportsTranscoding=true`, `Protocol=File`, `TranscodingSubProtocol=http`, `TranscodingContainer=mpeg` and a non-attached SDR video stream. Unsupported/malformed/oversized negotiations fail visibly.

It constructs the documented **`GET /Videos/{itemId}/stream.mpeg`** route, with:

```text
Static=false
MediaSourceId=<validated ID>
DeviceId=<same device as negotiation>
PlaySessionId=<validated ID>
VideoCodec=mpeg1video
VideoStreamIndex=<selected stream index>
AudioCodec=mp2
AudioBitRate=64000
MaxAudioChannels=2
SubtitleStreamIndex=-1
EnableAutoStreamCopy=false
AllowVideoStreamCopy=false
AllowAudioStreamCopy=false
MaxWidth=720
MaxHeight=480
Framerate=30
VideoBitRate=2500000
RequireNonAnamorphic=true
```

These are request constraints, not proof of the response format. The incremental demux checks MPEG-PS framing; the decoder independently checks MPEG-1 sequences, even dimensions up to 720×480, supported rate codes through 30 FPS, initial intra picture, repeated format consistency and a maximum 128 KiB compressed picture/header. MPEG-2 video extensions and incompatible original containers are rejected before presentation. The same-origin URL is constructed from validated IDs; arbitrary server-returned URLs are not followed. Authentication uses the existing `MediaBrowser` header, never query tokens.

**Silent playback caveat:** In Jellyfin 10.10.7, `AudioStreamIndex=-1` falls back to the first audio stream; it does **not** mean `-an`. Omitting an audio codec also caused HTTP 500 because this route inferred an unavailable `mpeg` audio encoder. The verified standard-API approach explicitly encodes **64 kbit/s MP2 when the source has audio**, downmixes to at most two channels for MP2 compatibility, then discards its packets on the console. No movie audio enters the MP3/PCM backend. Video-only inputs have no audio track. This version does not claim to disable audio encoding on the server. No undocumented FFmpeg command injection or custom server plugin is used.

Start/progress/stop reports use `/Sessions/Playing`, `/Sessions/Playing/Progress`, `/Sessions/Playing/Stopped`, `PlayMethod=Transcode`, item/source/session IDs, 100 ns position ticks, pause/buffering state and `CanSeek=false`. Start is sent only after the first displayed frame; progress approximately every five seconds. Stop/end/error cancels reception, reports stop if started, then DELETEs `/Videos/ActiveEncodings?DeviceId=...&PlaySessionId=...`. Report failures are shown without hiding playback errors. HTTP 401 invalidates the local session. Console power loss/logout cannot guarantee a remote cleanup report; closed TCP/server timeouts remain the fallback.

## Pipeline, ownership and scheduling

```text
lwIP callbacks → HTTP framing → 512 KiB compressed PS ring
  → bounded MPEG-PS/PES demux → 256 KiB elementary ring
  → existing pl_mpeg decoder → four YUV slots + one prepared GPU picture
  → existing absolute-PTS scheduler → existing renderer/framebuffer compositor
```

`jvideo.c` owns negotiation, reception, demux service and reporting. `video_api.c` owns the restricted profile/source parser. `video_ps.c` parses fragmented MPEG-1/2 pack/PES framing, extracts optional 90 kHz PTS for diagnostics and discards audio/subtitles/other streams. The main video stream is E0, as produced by the verified single-video FFmpeg output. One bounded 65,542-byte PES scratch buffer retains a packet while its payload drains; only elementary video is copied to the codec ring. Demux emits at most 32 KiB payload per tick and does not synchronously read sockets or wait for more input. A complete PES is bounded by its 16-bit length; unbounded/invalid/truncated packets fail explicitly.

The low-level codec now distinguishes **needs input** from EOF. Its fixed arena remains **2 MiB**, including the new 256 KiB ES ring and reference frames. Four maximum-sized YUV slots occupy 2,073,600 bytes. Network/PES/ES storage does not grow with movie length; long HTTP framing counters are 64 bit, including on 32-bit PowerPC. JSON control responses remain capped at 32 KiB. No complete movie allocation, disk cache, general FFmpeg port, new decoder thread or replacement toolchain was added.

lwIP retains a pbuf when the ring cannot accept its entire `tot_len`: the receive callback returns `ERR_MEM`, without ACK/free or partial ownership transfer. lwIP retries after space becomes available. Accepted data is copied before ACK/free. Pausing applies normal backpressure through both compressed rings and the decoded queue; idle timeout is suppressed only during genuine receiver backpressure. Connect/idle deadlines remain eight seconds and negotiation/header total deadlines twenty seconds; successful ongoing streaming has no whole-movie twenty-second limit. Chunked, content-length and close-delimited responses are supported; incomplete reads, transport errors and cancellation stop playback with a message.

The proven 49.875 MHz monotonic conversion, exact rational frame-index PTS, absolute scheduling, preroll and lateness rule remain. For the forced constant-rate silent transcode, decoded display-order frame timestamps start at zero; absolute PES timestamp offsets are not used to drive an audio clock. Seeking/discontinuities are not supported. The playback origin is established only at first presentation after preroll. During a compressed-input underrun, the last image holds and the clock freezes; it resumes when the decoded queue refills (or EOF permits draining). User pause and buffering share pause accounting, so overlapping waits are excluded once. The application counts these underruns rather than disguising them as EOF or generating catch-up drops.

Controller/UI work, HTTP callbacks and one decoder step remain cooperatively serviced on the main thread. There are no blocking socket calls or unbounded input waits. A single MPEG picture decode is still non-preemptible, as in 0.5.2. Existing nonblocking GPU fences, texture reuse, cache management, aspect scaling and the stable four-Hz text compositor are unchanged. Network/demux p95/max measure individual service calls (network includes NIC/lwIP poll plus HTTP state-machine calls); GPU time remains an observation upper bound, separate from upload/copy/VBlank. Host timing and the overlay do not measure total Xbox CPU utilisation.

## Reproduce source/build/host checks

```sh
DEVKITXENON=/workspace/setup/devkit make xenon
make native-test
make -C platform/xenon/tests SANITIZE=1 all test
python3 tests/native_video.py
python3 tests/native_video_server.py
python3 tests/native_video_server.py --long     # five minutes of HOST playback
python3 tests/native_server.py --music          # existing login/browse/audio regression
```

`JELLYFIN_TEST_SERVER` selects the official server binary; `FFMPEG` selects its FFmpeg. `JELLYFIN_VIDEO_STREAM_PROBE` and `JELLYFIN_VIDEO_STREAM_DECODE_PROBE` can select sanitized probes. Each server test creates its own temporary server, account and generated H.264/AAC movie/episode library, then removes it. Secrets are passed in environment bindings and checked for accidental output. No personal server is contacted. `scripts/fetch_test_server.py` installs the pinned test server if needed. No host socket/display bridge is linked into the Xbox ELF.

Host evidence: real 10.10.7 H.264/AAC 640×360/24 source → authenticated MPEG-PS/MPEG-1 640×360/30 → **240 decoded/240 presented**, zero drops/underruns, moving RGB conversions, real pause/resume, start/progress/stop and encoding cleanup. Episode (5.1 AAC source downmixed for MP2 transport) and cancellation use the same production pipeline. The five-minute **real-time host** run decoded/presented **9,001/9,001 frames**, zero drops/underruns, received **99,086,336 bytes** and held PS storage at its fixed **524,288-byte** limit. Source duration included a short audio tail, hence one extra encoded video frame. [Recorded counters](native-video-streaming-metrics.json). Incremental arbitrarily fragmented PS decoding matches the existing local codec pixel-for-pixel across 320×240, 640×360, 720×480, fractional rates and I/P/B reorder cases; local decoded YUV/known RGB patches retain the independent FFmpeg comparison. Fault suites cover JSON/permission/source constraints, 64-bit HTTP lengths/chunks, packet splits/wrap/backpressure, invalid containers, 401/403/302, incomplete EOF, timeout, link loss and cancel. The scheduler test excludes startup/rebuffer/overlapping pause exactly once. ASan/UBSan checks use the same C implementation. **These are host/API/decoder checks; actual streamed Xbox frames, controls and five-minute physical stability remain pending.**

For an optional reproducible five-minute SDR movie to import into a server movie library:

```sh
ffmpeg -f lavfi -i 'testsrc2=size=640x360:rate=24' \
  -f lavfi -i 'sine=frequency=440:sample_rate=48000' -t 300 \
  -c:v libx264 -preset veryfast -pix_fmt yuv420p -b:v 1200k \
  -c:a aac -b:a 96k 'XenonFin Streaming Test (2026).mp4'
```

The MP4 is the **server's original fixture**, not a local file for the Xbox MPEG-1 decoder. Browse it on the console so the Jellyfin transcode and HTTP path are exercised. The existing local `.m1v` FFmpeg commands/benchmarks remain in [NATIVE_VIDEO.md](NATIVE_VIDEO.md).

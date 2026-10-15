# Platform investigation and implementation boundary

**0.7 update:** user-confirmed 0.6 real Jellyfin streaming/GPU MPEG-1 at approximately 720×414/30, zero drops. Native 0.7 adds MPEG-PS MP3 extraction, existing minimp3/PCM conversion and an actual DMA-consumption master for the preserved video scheduler. [NATIVE_AV.md](NATIVE_AV.md) documents architecture and pending physical sound/sync acceptance. It uses the same toolchain; there is no new XDK, codec port or hardware pause assumption. Earlier release status below is historical.

**0.6 status:** the user confirmed 0.5.2 local 720×480/30 video at **240/240 frames, zero drops**, completing the initial local-video prerequisite. Native 0.6 adds Jellyfin MPEG-1/MPEG-PS negotiation, bounded incremental HTTP/PES input, controls and reports, and reuses that decoder/scheduler/renderer. Real Jellyfin host/API/decoder and cross-build evidence is documented in [NATIVE_VIDEO_STREAMING.md](NATIVE_VIDEO_STREAMING.md); streamed Xbox pictures and five-minute console stability remain pending. No movie audio/HD codec/artwork was added. Earlier investigation details below are retained as history.

Investigated 14 October 2026 using source checkouts, upstream build rules, and actual compilation. No physical Xbox 360 was connected to this environment.

**Native 0.4 update (15 October 2026):** the user has confirmed 0.3 XeLL boot, graphics/controller/audio diagnostics, Ethernet/HTTP/authentication, library discovery, movie pagination and metadata/details on a real console. Version 0.4 extends that code with native MP3 streaming, scalar decoding, PCM conversion/output, controls and Jellyfin reports. See [NATIVE_MUSIC.md](NATIVE_MUSIC.md). The user has since confirmed audible MP3 playback on console in 0.4, with text-only flicker. The 0.4.1 presentation fix froze menu updates while B still exited. Version 0.4.2 removes the hardware swap dependency; [NATIVE_RENDERING.md](NATIVE_RENDERING.md) documents its pending console checks. The user has now confirmed the text fix. Version 0.5 adds a small native local MPEG-1 decoder and CPU/GPU renderer test; [NATIVE_VIDEO.md](NATIVE_VIDEO.md) documents confirmed 0.5 video and confirmed 0.5.1 colours and confirmed 0.5.2 cadence. Native 0.6 streaming is now implemented for physical testing; artwork remains deferred.

## Verified open-source route

[Free60 libXenon](https://github.com/Free60Project/libxenon/tree/c32c813c3d630ae922f83395ee4509b0468af71d) describes itself as a bare-metal library. Its `devkitxenon/rules` invokes `xenon-gcc`, links with `app.lds` and libXenon, then invokes `xenon-objcopy -O elf32-powerpc --adjust-vma 0x80000000`. The included examples use `xenos_init`, `console_init`, USB polling and `get_controller_data`. We used these actual APIs for `platform/xenon/main.c`, not invented SDK APIs.

The tested, pinned Free60 image's amd64 manifest is `sha256:4113852fd3281b50c084f9e26cfb6cdd30b707bb2e7ee676e919dbb7cfbdd24f`. `scripts/fetch_devkit.py` extracts its toolchain/library layers after verifying the manifest and each compressed layer. Its installed compiler reports Xenon GCC 16.2.0. The native diagnostic builds to ELF32, big-endian, PowerPC, statically linked, using its linker-selected entry point after the upstream ELF32 address adjustment. File-format inspection and successful linking are not boot verification.

The current upstream source-building script references mirrored GNU/newlib archives and carries its own missing-checksum TODO. We did not execute that download/build path or disable verification. The registry snapshot avoids relying on the current source script being a reproducible source build. It is a pinned binary toolchain, not a claim that its bootstrap was independently audited or rebuilt here.

## ELF and XEX

XeLL supplies a bare-metal ELF launch environment. Aurora/XeXMenu ordinarily launch XEX applications in the Xbox OS environment. XEX is a different container and execution environment, with imports, ABI and initialization requirements. Renaming an ELF or wrapping its bytes does not adapt libXenon's privileged bare-metal drivers to the dashboard environment.

No working open-source-only XEX build/runtime/backend combination was established in this investigation. Microsoft's Xbox 360 SDK/XDK headers, libraries, platform services and decoder interfaces were not supplied, copied or downloaded. A future dashboard target needs a legally usable, verified toolchain/runtime and independently implemented platform backends. `default.xex` is not generated. The present native build supports connectivity/authentication, text library browsing, diagnostics and a native MP3 pipeline with user-confirmed audible 0.4 output; local MPEG-1 video is confirmed in 0.5, with zero-drop 0.5.2 cadence confirmed and new 0.6 streaming awaiting console validation.

## Existing player evidence

[XMPlayer source at 5fc2fb330bfb58bd2806fed6c63f6ae917cb3a3c](https://github.com/LibXenonProject/xmplayer/tree/5fc2fb330bfb58bd2806fed6c63f6ae917cb3a3c) is a useful reference. Its README identifies version 0.0.4 (2013), a particular old libXenon revision, USB/XeLL installation and known seek/synchronization bugs. It explicitly lists **no networking**, music not supported, and no surround/optical output in that release. It is not a ready-made Jellyfin streaming backend.

Its Makefile links `mplayer`, `avformat`, `avcodec`, `swscale`, `avutil`, `postproc` and libXenon; its output includes `xenon.elf`. `mplayer/libvo/vo_xenon.c` and `mplayer/libao2/ao_xenon.c` provide platform display/audio adaptation. The bundled FFmpeg PPC Makefile includes AltiVec H.264 DSP. This supports investigation of CPU decoding and GPU frame presentation, not a claim of H.264 hardware decoding. The Xenos graphics API is not, by itself, a codec decoder.

LibXenon's `xenon_sound/sound.h` documents 48 kHz signed 16-bit **little-endian stereo PCM**, a submission API, and free/unplayed buffer queries. Any port must explicitly account for big-endian PowerPC and the PCM byte order. `network/network.h` exposes DHCP, poll and link status through lwIP. The code inspected did not establish a maintained, certificate-validated TLS + current FFmpeg + current UI integration on the console.

The original dashboard's H.264 capabilities do not establish a publicly usable homebrew decoder API. No proprietary hardware decoder is advertised or called here. Reusing XMPlayer is possible only after its old dependencies/build, GPL obligations, networking, timing and resource ownership have been resolved; it has not been integrated or rebuilt in this repository.

## Implemented alternative

The runnable alternative is the Linux SDL/FreeType/libcurl/FFmpeg client. It exercises real API authentication, libraries, controller actions, artwork, music and negotiated A/V streaming. Its transcode device profile is named **XenonFin desktop FFmpeg**. The Xbox diagnostic never advertises that profile and never pretends its desktop decoder is available on the console.

Progress against the requested stages:

| Stage | Result |
|---|---|
| 1: native boot/graphics/controller | Version 0.3 confirmed on hardware by user; broader model/output matrix unrun |
| 2: console connection/auth | Native DHCP/HTTP/keyboard/login working on console in 0.3 |
| 3: console browsing/search | Native discovery/movie paging/metadata confirmed in 0.3; TV/music hierarchy API tested; native search absent |
| 4: console music | 0.4 native MP3 streaming/decoding/PCM conversion/output implemented and host-tested; actual MP3 output confirmed by user; 0.4.2 text fix confirmed; 0.5.2 local cadence confirmed; 0.6 streaming needs console testing |
| 5: console local video decoder | Native 0.5 local MPEG-1/CPU/GPU test cross-built and host-tested; 0.5 video confirmed; 0.5.2 720×480/30, 240/240 frames confirmed |
| 6: Jellyfin negotiation/stream/report | Native 0.6 progressive MPEG-1/MPEG-PS video, incremental decoding/RGB and reports pass real Jellyfin host integration; Xbox streaming/five-minute stability pending. Native MP3 output is user-confirmed. |
| 7: optimize/HD | Desktop synthetic 720p tests only; no Xbox video resolution/FPS claim |
| 8: packaging | Source, Linux desktop, current XeLL ELF/archive and unchanged 0.4/0.3/0.2 rollback ELFs; no XEX |

## Work needed to finish the Xbox client

1. Test [0.7 audio/video synchronisation](NATIVE_AV.md) on the console: actual movie/episode images, five-minute continuous run, pause/resume, stop and return, source compatibility and server cleanup.
2. Compare streaming network/demux p95/max, buffer occupancy/underruns and the preserved local 0.5.2 decode/render/clock diagnostics before performance changes.
3. Retest actual MP3 audio, network/authentication, libraries, controller/UI, cable loss and repeated playback on hardware. Broader models/display modes and long-run stability remain unmeasured.
4. Native persistence, verified TLS, artwork and seeking remain future work; movie audio/A-V synchronisation is implemented for hardware acceptance. Never advertise HD codecs or copy an unsupported original into the MPEG-1 decoder.
5. Complete the relevant physical acceptance matrix before claiming broader support.

The new presentation behavior and remaining native video work cannot be validated by host API/decoder tests or successful cross-compilation.

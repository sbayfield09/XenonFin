# Platform investigation and implementation boundary

Investigated 14 October 2026 using source checkouts, upstream build rules, and actual compilation. No physical Xbox 360 was connected to this environment.

## Verified open-source route

[Free60 libXenon](https://github.com/Free60Project/libxenon/tree/c32c813c3d630ae922f83395ee4509b0468af71d) describes itself as a bare-metal library. Its `devkitxenon/rules` invokes `xenon-gcc`, links with `app.lds` and libXenon, then invokes `xenon-objcopy -O elf32-powerpc --adjust-vma 0x80000000`. The included examples use `xenos_init`, `console_init`, USB polling and `get_controller_data`. We used these actual APIs for `platform/xenon/main.c`, not invented SDK APIs.

The tested, pinned Free60 image's amd64 manifest is `sha256:4113852fd3281b50c084f9e26cfb6cdd30b707bb2e7ee676e919dbb7cfbdd24f`. `scripts/fetch_devkit.py` extracts its toolchain/library layers after verifying the manifest and each compressed layer. Its installed compiler reports Xenon GCC 16.2.0. The native diagnostic builds to ELF32, big-endian, PowerPC, statically linked, with entry point `0x2300` after the upstream ELF32 address adjustment. File-format inspection and successful linking are not boot verification.

The current upstream source-building script references mirrored GNU/newlib archives and carries its own missing-checksum TODO. We did not execute that download/build path or disable verification. The registry snapshot avoids relying on the current source script being a reproducible source build. It is a pinned binary toolchain, not a claim that its bootstrap was independently audited or rebuilt here.

## ELF and XEX

XeLL supplies a bare-metal ELF launch environment. Aurora/XeXMenu ordinarily launch XEX applications in the Xbox OS environment. XEX is a different container and execution environment, with imports, ABI and initialization requirements. Renaming an ELF or wrapping its bytes does not adapt libXenon's privileged bare-metal drivers to the dashboard environment.

No working open-source-only XEX build/runtime/backend combination was established in this investigation. Microsoft's Xbox 360 SDK/XDK headers, libraries, platform services and decoder interfaces were not supplied, copied or downloaded. A future dashboard target needs a legally usable, verified toolchain/runtime and independently implemented platform backends. `default.xex` is not generated. The present native build is explicitly a **boot diagnostic**, not a Jellyfin client hidden behind an ELF extension.

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
| 1: native boot/graphics/controller | Source and native ELF build complete; boot/display/controller/audio require hardware verification |
| 2–3: console connection/auth/browse/search | Working and tested on desktop; not ported to libXenon |
| 4: console music | Real desktop MP3/AAC/FLAC decode/output path; no console integration |
| 5: console local video decoder | Desktop FFmpeg decoding tested; Xenon decoder integration absent |
| 6: Jellyfin negotiation/stream/report | Desktop integration tested against real Jellyfin; native integration absent |
| 7: optimize/HD | Desktop synthetic 720p tests only; no Xbox resolution/FPS claim |
| 8: packaging | Source, Linux client and labeled native diagnostic archives; no XEX |

## Work needed to finish the Xbox client

1. Boot the diagnostic on supported Fat/Slim/E RGH/JTAG hardware, verify video mode, hotplug/repeat, Ethernet, PCM and loader return. Resolve any hardware-specific failures before expanding the target.
2. Choose and prove a console media runtime: maintain an open-source bare-metal decoder/player port, or provide a legally usable dashboard runtime. Benchmark local clips before making network/player claims.
3. Implement console HTTP/TLS transport, scheduling/thread primitives, filesystem configuration and platform-independent API bindings. Keep certificate and hostname verification; reject HTTPS explicitly until it is supported. The desktop `std::thread`/POSIX/SDL/FreeType stack is not assumed to cross-compile to libXenon.
4. Integrate Xenos texture uploads/scaling, endian-correct PCM submission, queued-audio clock, seek/reset, decoder cleanup and controller UI. Validate each with local samples before Jellyfin streaming.
5. Expose a native device profile containing only measured capabilities. Start with 480p and 720p H.264 Baseline/Main. If CPU decoding is insufficient, benchmark server-side lighter-codec alternatives such as MPEG-2 with the chosen decoder, including network bitrate and A/V timing. Do not advertise those paths before measuring them.
6. Test actual server negotiation, transcoding/tone mapping, seek/reconnect/resume, and long-term memory stability. Complete the hardware checklist and record results. Only then describe the application as a native functioning Xbox client.

The missing native runtime/integration and physical test hardware are substantive remaining work. They are not resolved by a successful desktop test, a renamed artifact, or a publication snapshot.

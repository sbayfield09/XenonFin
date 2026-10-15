# Dependencies and provenance

Original XenonFin source is GPL-2.0-or-later; see LICENSE. Native minimp3, pl_mpeg and two XMPlayer shader arrays are vendored under `platform/xenon/third_party`, with decoder licenses/provenance alongside them. The tiled-console attribution is retained in source and [LIBXENON_CONSOLE_LICENSE.md](LIBXENON_CONSOLE_LICENSE.md). Other external dependencies are supplied by the pinned toolchain or system packages. Preserve applicable notices and source availability when distributing linked artifacts.

| Component | Source | Use / license |
|---|---|---|
| libcurl | https://curl.se/libcurl/ | HTTP/TLS, curl license |
| FFmpeg | https://ffmpeg.org/ | Demux/decode/resample/scale; LGPL or GPL depending on build; Debian FFmpeg here enables GPL components |
| SDL2 | https://github.com/libsdl-org/SDL | Desktop window/controller/video/audio, zlib license |
| FreeType | https://freetype.org/ | Desktop typography, FreeType license or GPL |
| nlohmann/json | https://github.com/nlohmann/json/tree/v3.11.3 | JSON, MIT license |
| DejaVu fonts | https://dejavu-fonts.github.io/ | System-installed test font, its own Bitstream/DejaVu license; not bundled |
| Free60 libXenon / devkit | https://github.com/Free60Project/libxenon | Bare-metal drivers, newlib/GCC/binutils and mixed upstream notices; inspect the pinned toolchain/image sources for redistribution |
| XMPlayer | https://github.com/LibXenonProject/xmplayer | GPL reference player; 0.5 reuses its two published YUV/vertex shader arrays, with attribution; full player not linked |
| Jellyfin | https://github.com/jellyfin/jellyfin | GPL-2.0 server used for API source inspection and isolated tests; not included in application packages |
| minimp3 | https://github.com/lieff/minimp3/tree/ea99364f61c14656440e8d77e9c233ccf3124633 | Native scalar MPEG Layer III decoder, CC0; unchanged header and license included in source archives |

Vendored minimp3 SHA-256: `minimp3.h` **57e437c5c1f0e8b243885d3929c8973b5e6c778451e0100ab4251d19915cb3ad**; `LICENSE` **6a1ee543e5282cd9061881edf462e6fdab181f328da71fc2c9a6950a80e94d01**. It compiles with `MINIMP3_NO_SIMD` and `MINIMP3_ONLY_MP3`; the upstream header is not patched. dr_mp3 was inspected as an alternative and is not linked or distributed.

The official test-server image is pinned to amd64 manifest `sha256:3b38dae4c3ddd6ebc7378538fba4d3f314070ebefbdb3d688166b7c8658fb123` (10.10.7). Its server and web layers are checksum-verified. The Free60 toolchain manifest is recorded in PLATFORM.md and the downloader. OCI digests provide exact downloaded content identification; they do not imply an independent compiler bootstrap audit.

Jellyfin API source inspected separately at commit `aec42b345e1a7da25ee5492183f12209338df688`; actual integration testing used 10.10.7. Microsoft SDK/XDK code, headers, binaries and codec libraries are not included or needed for the implemented builds. Synthetic test media is generated locally. `scripts/package.sh` includes original application source alongside binaries but does not bundle distribution libraries, the toolchain or fonts; anyone distributing those components must satisfy their own applicable licensing/source obligations.

The 0.4.2 console adapter reuses the linked libXenon GPL 8x16 Linux font and adapts the BSD console tiled-address calculation. The exact console notice and font attribution accompany all updated binary/source archives in LIBXENON_CONSOLE_LICENSE.md. Source: the pinned Free60 libXenon commit linked in PLATFORM.md, `libxenon/drivers/console/{console.c,font_8x16.h}`.

## Native 0.5 video additions

- **pl_mpeg**, MIT, Dominic Szablewski: pinned at `c871f2be022ece7ef4f64230b4fb8e1fb9eb6023`; header, standard MIT notice and provenance/two-patch record are in `platform/xenon/third_party/pl_mpeg/`. Upstream SHA-256 `3a8cb30c83c2a1147719c30fe0c8b93da2987aa43140077c575b39aaa75fc2c9`; patched SHA-256 `fe2f63c67b0dd41b97ad7b4c8c84e1e470f14ccc8d4a7009e5dcf7c69b454ac3`. The low-level MPEG-1 video API is used; MP2 audio/demux APIs are not used.
- **XMPlayer published YUV/passthrough shader arrays**, GPL-2.0, Ced2911: copied verbatim from `mplayer/libvo/vo_xenon.c` at `5fc2fb330bfb58bd2806fed6c63f6ae917cb3a3c` into `platform/xenon/third_party/xmplayer_yuv_shaders.h` with attribution. Header SHA-256 `e66be9369d35409d7a57ea266215a9fb6a384a7e877fbf8c06f3eb3ab63ac115`. The project GPL license accompanies source/binaries. These are the shader programs distributed as source arrays by that open-source player; no Microsoft SDK/XDK compiler, headers, runtime libraries or proprietary video-decoder payload was downloaded or linked. Arrays are not rebuilt by this toolchain. The rest of the old MPlayer/FFmpeg player is not linked.
- **libfat**, the existing pinned devkit archive, is newly linked for FAT USB storage. Public-interface BSD notice is reproduced in [LIBFAT_LICENSE.md](LIBFAT_LICENSE.md). Actual use is `fatMountSimple` plus the existing libXenon USB mass-storage interfaces; HDD/FATX/NTFS/ext2 access is not initialized or used.
- **Synthetic test video** is generated locally from FFmpeg `testsrc2` using the documented command; it contains no third-party movie or music content. It is bundled only as a hardware test fixture.

The earlier XMPlayer row described the pre-0.5 reference-only investigation; the two shader arrays are now the explicit reused component. Native video tile addressing also retains the existing libXenon console attribution. Binary archives contain the pl_mpeg/MIT, libfat, console and GPL notices, and the source archive contains the actual decoder edits and renderer sources.

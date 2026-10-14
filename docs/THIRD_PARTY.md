# Dependencies and provenance

Original XenonFin source is GPL-2.0-or-later; see LICENSE. The native 0.4 minimp3 decoder and its license are vendored under `platform/xenon/third_party/minimp3`; other external dependencies are not copied into the source tree. Scripts fetch pinned, verified binary snapshots or use distribution packages. Preserve applicable notices and source availability when distributing linked artifacts.

| Component | Source | Use / license |
|---|---|---|
| libcurl | https://curl.se/libcurl/ | HTTP/TLS, curl license |
| FFmpeg | https://ffmpeg.org/ | Demux/decode/resample/scale; LGPL or GPL depending on build; Debian FFmpeg here enables GPL components |
| SDL2 | https://github.com/libsdl-org/SDL | Desktop window/controller/video/audio, zlib license |
| FreeType | https://freetype.org/ | Desktop typography, FreeType license or GPL |
| nlohmann/json | https://github.com/nlohmann/json/tree/v3.11.3 | JSON, MIT license |
| DejaVu fonts | https://dejavu-fonts.github.io/ | System-installed test font, its own Bitstream/DejaVu license; not bundled |
| Free60 libXenon / devkit | https://github.com/Free60Project/libxenon | Bare-metal drivers, newlib/GCC/binutils and mixed upstream notices; inspect the pinned toolchain/image sources for redistribution |
| XMPlayer | https://github.com/LibXenonProject/xmplayer | GPL reference investigation only; no player source imported or linked |
| Jellyfin | https://github.com/jellyfin/jellyfin | GPL-2.0 server used for API source inspection and isolated tests; not included in application packages |
| minimp3 | https://github.com/lieff/minimp3/tree/ea99364f61c14656440e8d77e9c233ccf3124633 | Native scalar MPEG Layer III decoder, CC0; unchanged header and license included in source archives |

Vendored minimp3 SHA-256: `minimp3.h` **57e437c5c1f0e8b243885d3929c8973b5e6c778451e0100ab4251d19915cb3ad**; `LICENSE` **6a1ee543e5282cd9061881edf462e6fdab181f328da71fc2c9a6950a80e94d01**. It compiles with `MINIMP3_NO_SIMD` and `MINIMP3_ONLY_MP3`; the upstream header is not patched. dr_mp3 was inspected as an alternative and is not linked or distributed.

The official test-server image is pinned to amd64 manifest `sha256:3b38dae4c3ddd6ebc7378538fba4d3f314070ebefbdb3d688166b7c8658fb123` (10.10.7). Its server and web layers are checksum-verified. The Free60 toolchain manifest is recorded in PLATFORM.md and the downloader. OCI digests provide exact downloaded content identification; they do not imply an independent compiler bootstrap audit.

Jellyfin API source inspected separately at commit `aec42b345e1a7da25ee5492183f12209338df688`; actual integration testing used 10.10.7. Microsoft SDK/XDK code, headers, binaries and codec libraries are not included or needed for the implemented builds. Synthetic test media is generated locally. `scripts/package.sh` includes original application source alongside binaries but does not bundle distribution libraries, the toolchain or fonts; anyone distributing those components must satisfy their own applicable licensing/source obligations.

# pl_mpeg provenance

Source: https://github.com/phoboslab/pl_mpeg/blob/c871f2be022ece7ef4f64230b4fb8e1fb9eb6023/pl_mpeg.h

Upstream SHA-256: `3a8cb30c83c2a1147719c30fe0c8b93da2987aa43140077c575b39aaa75fc2c9`.

XenonFin patched header SHA-256: `fe2f63c67b0dd41b97ad7b4c8c84e1e470f14ccc8d4a7009e5dcf7c69b454ac3`.

Upstream's header declares `SPDX-License-Identifier: MIT`, credits Dominic Szablewski, and the README describes an MIT-licensed library. Upstream has no separate LICENSE file at this revision; LICENSE here reproduces the standard MIT notice with that author attribution.

Two local patches in `plm_video_decode`:

- Guard `plm_buffer_discard_read_bytes(self->buffer)` with `self->buffer->discard_read_bytes`, preserving fixed-memory length/EOF and avoiding quadratic buffer shifts.
- Remove the last-coded-picture-type restriction when flushing `frame_backward` at EOF. A pending future reference also needs output after trailing reordered B pictures.

Both are covered by `tests/native_video.py`: 240 I/P frames, 48 I/P/B frames with padded 640x360 planes, and 60 I/P/B frames at 29.97 FPS are compared to FFmpeg. These are the only edits to the upstream header. The application uses only its low-level video/fixed-memory APIs, with custom bounded arena macros in video_decode.c. MP2 and demux functions are not used.

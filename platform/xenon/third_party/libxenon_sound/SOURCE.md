# Pinned Free60 sound driver repair

Source: https://github.com/Free60Project/libxenon/blob/c32c813/libxenon/drivers/xenon_sound/sound.c

Same driver as the project's pinned toolchain; BSD notice is included in LICENSE. This copy is compiled before libxenon.a, supplying the original five public sound/tone functions so the archive sound.o is not selected. Graphics, GPIO/HDMI configuration, 48 kHz S16LE stereo format, 65,536-byte ring, 32 DMA descriptors, cache operations and queue accounting are retained.

Local changes:

- Submission reads **control at base+8**, ORs the existing run bit and writes control at base+8. Upstream reads **descriptor address at base+0** and copies that address into control. The bug is present in the actual previously linked PowerPC instructions, not just a reference source file. Descriptor physical placement varies with executable layout; its bits must never configure the sound engine.
- Move the existing stop/reset writes to the beginning of init, before buffer clearing, for explicit idle-only recovery. Do not reset during movie/music/tone ownership.
- The legacy tone submits complete 2,048-byte descriptors instead of half descriptors (1,024 bytes), and zero-pads its final descriptor. The UI now uses a cooperative tone with a bounded stall watchdog instead of the legacy busy-wait function.
- Use uintptr_t for address masking and a byte-pointer cast for copying, to exercise this same driver source with host MMIO/cache/GPIO/SMC adapters. Native addresses/ABI are unchanged. XF_SOUND_HOST_TEST only substitutes those hardware boundaries; the ELF compiles against the existing libXenon headers.

Host tests validate actual register access, preserved control bits with different descriptor addresses, descriptor alignment/endianness/cache ordering, ring wrap and tone data. They cannot confirm DAC/HDMI sound or the physical register behavior; console testing remains necessary.

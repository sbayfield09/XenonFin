ifeq ($(strip $(DEVKITXENON)),)
$(error Set DEVKITXENON to your libXenon toolchain prefix)
endif
include $(DEVKITXENON)/rules
CC := $(DEVKITXENON)/bin/xenon-gcc
OBJCOPY := $(DEVKITXENON)/bin/xenon-objcopy
TARGET := ../../build/xenon/xenonfin-boot
AUDIO_TARGET := ../../build/xenon/xenonfin-audio-test
AUDIO_OBJECTS := $(addprefix ../../build/xenon/,audio_test_main.o audio_native.o sound_diag.o console_batch.o console_frame.o framebuffer.o)
CFLAGS := -std=gnu99 -O2 -g -Wall -Wextra $(MACHDEP) -I$(LIBXENON_INC)
LDFLAGS := $(MACHDEP) -Wl,--gc-sections -Wl,--wrap=mdelay -T $(LDSCRIPT) -L$(LIBXENON_LIB)
.PHONY: all clean
all: $(TARGET).elf32 $(AUDIO_TARGET).elf32
SOURCES := audio_native.c sound_diag.c main.c protocol.c json.c http.c browse.c text.c console_batch.c console_frame.c framebuffer.c ui_clock.c audio_decode.c music_api.c music.c tcp_client.c native_net.c driver_guard.c init_guard.c video.c video_audio.c video_api.c video_ps.c jvideo.c video_sync.c video_decode.c video_pixels.c video_native.c video_source.c
# Keep unused upstream MP2/demux APIs out of this video-only bring-up ELF.
../../build/xenon/video_decode.o: CFLAGS += -ffunction-sections -fdata-sections
OBJECTS := $(patsubst %.c,../../build/xenon/%.o,$(SOURCES))
../../build/xenon/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -Wl,-Map,$@.map -lfat -lxenon -lm -o $@
$(TARGET).elf32: $(TARGET).elf
	$(OBJCOPY) -O elf32-powerpc --adjust-vma 0x80000000 $< $@
	$(DEVKITXENON)/bin/xenon-strip $@
clean:
	rm -f $(OBJECTS) $(OBJECTS:.o=.d) $(TARGET).elf $(TARGET).elf32
	rm -f $(AUDIO_OBJECTS) $(AUDIO_OBJECTS:.o=.d) $(AUDIO_TARGET).elf $(AUDIO_TARGET).elf32 $(AUDIO_TARGET).elf.map $(TARGET).elf.map
-include $(OBJECTS:.o=.d)

# Isolated PCM acceptance ELF: no application/network/codec/video player objects.
.PHONY: audio-test
audio-test: $(AUDIO_TARGET).elf32
$(AUDIO_TARGET).elf: $(AUDIO_OBJECTS) Makefile
	$(CC) $(AUDIO_OBJECTS) $(MACHDEP) -Wl,--gc-sections -Wl,-Map,$@.map -T $(LDSCRIPT) -L$(LIBXENON_LIB) -lxenon -lm -o $@
$(AUDIO_TARGET).elf32: $(AUDIO_TARGET).elf
	$(OBJCOPY) -O elf32-powerpc --adjust-vma 0x80000000 $< $@
	$(DEVKITXENON)/bin/xenon-strip $@
-include $(AUDIO_OBJECTS:.o=.d)

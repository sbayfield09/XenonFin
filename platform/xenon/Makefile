ifeq ($(strip $(DEVKITXENON)),)
$(error Set DEVKITXENON to your libXenon toolchain prefix)
endif
include $(DEVKITXENON)/rules
CC := $(DEVKITXENON)/bin/xenon-gcc
OBJCOPY := $(DEVKITXENON)/bin/xenon-objcopy
TARGET := ../../build/xenon/xenonfin-boot
CFLAGS := -std=gnu99 -O2 -g -Wall -Wextra $(MACHDEP) -I$(LIBXENON_INC)
LDFLAGS := $(MACHDEP) -Wl,--gc-sections -Wl,--wrap=mdelay -T $(LDSCRIPT) -L$(LIBXENON_LIB)
.PHONY: all clean
all: $(TARGET).elf32
SOURCES := main.c protocol.c json.c http.c browse.c text.c console_batch.c console_frame.c framebuffer.c ui_clock.c audio_decode.c music_api.c music.c tcp_client.c native_net.c driver_guard.c init_guard.c video.c video_sync.c video_decode.c video_pixels.c video_native.c video_source.c
# Keep unused upstream MP2/demux APIs out of this video-only bring-up ELF.
../../build/xenon/video_decode.o: CFLAGS += -ffunction-sections -fdata-sections
OBJECTS := $(patsubst %.c,../../build/xenon/%.o,$(SOURCES))
../../build/xenon/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -lfat -lxenon -lm -o $@
$(TARGET).elf32: $(TARGET).elf
	$(OBJCOPY) -O elf32-powerpc --adjust-vma 0x80000000 $< $@
	$(DEVKITXENON)/bin/xenon-strip $@
clean:
	rm -f $(OBJECTS) $(OBJECTS:.o=.d) $(TARGET).elf $(TARGET).elf32
-include $(OBJECTS:.o=.d)

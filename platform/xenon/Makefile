ifeq ($(strip $(DEVKITXENON)),)
$(error Set DEVKITXENON to your libXenon toolchain prefix)
endif
include $(DEVKITXENON)/rules
CC := $(DEVKITXENON)/bin/xenon-gcc
OBJCOPY := $(DEVKITXENON)/bin/xenon-objcopy
TARGET := ../../build/xenon/xenonfin-boot
CFLAGS := -std=gnu99 -O2 -g -Wall -Wextra $(MACHDEP) -I$(LIBXENON_INC)
LDFLAGS := $(MACHDEP) -Wl,--gc-sections -T $(LDSCRIPT) -L$(LIBXENON_LIB)
.PHONY: all clean
all: $(TARGET).elf32
$(TARGET).o: main.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(TARGET).elf: $(TARGET).o
	$(CC) $< $(LDFLAGS) -lxenon -lm -o $@
$(TARGET).elf32: $(TARGET).elf
	$(OBJCOPY) -O elf32-powerpc --adjust-vma 0x80000000 $< $@
	$(DEVKITXENON)/bin/xenon-strip $@
clean:
	rm -f $(TARGET).o $(TARGET).elf $(TARGET).elf32

HOST_CC ?= cc
DEVKITXENON ?= /workspace/setup/devkit
OUT := ../../../build/native-tests
CFLAGS := -std=c99 -O1 -g -Wall -Wextra -Wpedantic -Werror
ifeq ($(SANITIZE),1)
OUT := ../../../build/native-tests-sanitized
CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
endif
COMMON := ../protocol.c ../json.c ../http.c ../browse.c
COMMON_HEADERS := ../protocol.h ../browse.h
MUSIC := ../audio_decode.c ../music_api.c
MUSIC_HEADERS := ../audio_decode.h ../music_api.h ../third_party/minimp3/minimp3.h
.PHONY: all test
all: $(OUT)/console-adapter-tests $(OUT)/render-tests $(OUT)/framebuffer-tests $(OUT)/music-fault-tests $(OUT)/music-probe $(OUT)/music-protocol-tests $(OUT)/decode-probe $(OUT)/browse-tests $(OUT)/protocol-tests $(OUT)/tcp-tests $(OUT)/guard-tests $(OUT)/network-tests $(OUT)/init-tests $(OUT)/probe
$(OUT):
	mkdir -p $@
$(OUT)/music-protocol-tests: music_protocol_tests.c $(COMMON) $(MUSIC) ../text.c $(COMMON_HEADERS) $(MUSIC_HEADERS) | $(OUT)
	$(HOST_CC) $(CFLAGS) music_protocol_tests.c $(COMMON) $(MUSIC) ../text.c -o $@
$(OUT)/decode-probe: decode_probe.c ../audio_decode.c ../protocol.c ../json.c $(MUSIC_HEADERS) ../protocol.h | $(OUT)
	$(HOST_CC) $(CFLAGS) decode_probe.c ../audio_decode.c ../protocol.c ../json.c -o $@
$(OUT)/music-probe: music_probe.c posix_tcp.c ../music.c ../tcp_client.c $(COMMON) $(MUSIC) $(COMMON_HEADERS) $(MUSIC_HEADERS) ../music.h ../native_net.h | $(OUT)
	$(HOST_CC) $(CFLAGS) -I$(DEVKITXENON)/usr/include music_probe.c posix_tcp.c ../music.c ../tcp_client.c $(COMMON) $(MUSIC) -o $@
$(OUT)/music-fault-tests: music_fault_tests.c tcp_fake.h ../music.c ../tcp_client.c $(COMMON) $(MUSIC) $(COMMON_HEADERS) $(MUSIC_HEADERS) ../music.h ../native_net.h | $(OUT)
	$(HOST_CC) $(CFLAGS) -I$(DEVKITXENON)/usr/include music_fault_tests.c ../music.c ../tcp_client.c $(COMMON) $(MUSIC) -o $@
$(OUT)/browse-tests: browse_tests.c $(COMMON) $(COMMON_HEADERS) ../browse.h | $(OUT)
	$(HOST_CC) $(CFLAGS) browse_tests.c $(COMMON) -o $@
$(OUT)/protocol-tests: protocol_tests.c $(COMMON) $(COMMON_HEADERS) | $(OUT)
	$(HOST_CC) $(CFLAGS) protocol_tests.c $(COMMON) -o $@
$(OUT)/tcp-tests: tcp_tests.c tcp_fake.h ../tcp_client.c $(COMMON) $(COMMON_HEADERS) ../native_net.h | $(OUT)
	$(HOST_CC) $(CFLAGS) -I$(DEVKITXENON)/usr/include tcp_tests.c ../tcp_client.c $(COMMON) -o $@
$(OUT)/probe: probe.c $(COMMON) $(COMMON_HEADERS) | $(OUT)
	$(HOST_CC) $(CFLAGS) probe.c $(COMMON) -o $@
$(OUT)/guard-tests: guard_tests.c ../driver_guard.c ../driver_guard.h | $(OUT)
	$(HOST_CC) $(CFLAGS) -I$(DEVKITXENON)/usr/include guard_tests.c ../driver_guard.c -o $@
$(OUT)/network-tests: network_tests.c ../native_net.c $(COMMON) ../native_net.h ../driver_guard.h | $(OUT)
	$(HOST_CC) $(CFLAGS) -I$(DEVKITXENON)/usr/include network_tests.c ../native_net.c $(COMMON) -o $@
$(OUT)/init-tests: init_tests.c ../init_guard.c ../driver_guard.h | $(OUT)
	$(HOST_CC) $(CFLAGS) -I$(DEVKITXENON)/usr/include init_tests.c ../init_guard.c -o $@
test: $(OUT)/console-adapter-tests $(OUT)/render-tests $(OUT)/framebuffer-tests $(OUT)/music-fault-tests $(OUT)/music-protocol-tests $(OUT)/browse-tests $(OUT)/protocol-tests $(OUT)/tcp-tests $(OUT)/guard-tests $(OUT)/network-tests $(OUT)/init-tests
	$(OUT)/console-adapter-tests
	$(OUT)/render-tests
	$(OUT)/framebuffer-tests
	$(OUT)/music-fault-tests
	$(OUT)/music-protocol-tests
	$(OUT)/browse-tests
	$(OUT)/protocol-tests
	$(OUT)/tcp-tests
	$(OUT)/guard-tests
	$(OUT)/network-tests
	$(OUT)/network-tests init-failure
	$(OUT)/init-tests

$(OUT)/render-tests: render_tests.c ../console_frame.c ../console_frame.h ../ui_clock.c ../ui_clock.h | $(OUT)
	$(HOST_CC) $(CFLAGS) render_tests.c ../console_frame.c ../ui_clock.c -o $@
$(OUT)/framebuffer-tests: framebuffer_tests.c ../framebuffer.c ../framebuffer.h ../console_frame.h | $(OUT)
	$(HOST_CC) $(CFLAGS) -I$(DEVKITXENON)/usr/include framebuffer_tests.c ../framebuffer.c -o $@

$(OUT)/console-adapter-tests: console_adapter_tests.c ../console_batch.c ../console_frame.c ../console_batch.h ../console_frame.h ../framebuffer.h | $(OUT)
	$(HOST_CC) $(CFLAGS) -I$(DEVKITXENON)/usr/include console_adapter_tests.c ../console_batch.c ../console_frame.c -o $@

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
.PHONY: all test
all: $(OUT)/browse-tests $(OUT)/protocol-tests $(OUT)/tcp-tests $(OUT)/guard-tests $(OUT)/network-tests $(OUT)/init-tests $(OUT)/probe
$(OUT):
	mkdir -p $@
$(OUT)/browse-tests: browse_tests.c $(COMMON) $(COMMON_HEADERS) ../browse.h | $(OUT)
	$(HOST_CC) $(CFLAGS) browse_tests.c $(COMMON) -o $@
$(OUT)/protocol-tests: protocol_tests.c $(COMMON) $(COMMON_HEADERS) | $(OUT)
	$(HOST_CC) $(CFLAGS) protocol_tests.c $(COMMON) -o $@
$(OUT)/tcp-tests: tcp_tests.c ../tcp_client.c $(COMMON) $(COMMON_HEADERS) ../native_net.h | $(OUT)
	$(HOST_CC) $(CFLAGS) -I$(DEVKITXENON)/usr/include tcp_tests.c ../tcp_client.c $(COMMON) -o $@
$(OUT)/probe: probe.c $(COMMON) $(COMMON_HEADERS) | $(OUT)
	$(HOST_CC) $(CFLAGS) probe.c $(COMMON) -o $@
$(OUT)/guard-tests: guard_tests.c ../driver_guard.c ../driver_guard.h | $(OUT)
	$(HOST_CC) $(CFLAGS) -I$(DEVKITXENON)/usr/include guard_tests.c ../driver_guard.c -o $@
$(OUT)/network-tests: network_tests.c ../native_net.c $(COMMON) ../native_net.h ../driver_guard.h | $(OUT)
	$(HOST_CC) $(CFLAGS) -I$(DEVKITXENON)/usr/include network_tests.c ../native_net.c $(COMMON) -o $@
$(OUT)/init-tests: init_tests.c ../init_guard.c ../driver_guard.h | $(OUT)
	$(HOST_CC) $(CFLAGS) -I$(DEVKITXENON)/usr/include init_tests.c ../init_guard.c -o $@
test: $(OUT)/browse-tests $(OUT)/protocol-tests $(OUT)/tcp-tests $(OUT)/guard-tests $(OUT)/network-tests $(OUT)/init-tests
	$(OUT)/browse-tests
	$(OUT)/protocol-tests
	$(OUT)/tcp-tests
	$(OUT)/guard-tests
	$(OUT)/network-tests
	$(OUT)/network-tests init-failure
	$(OUT)/init-tests

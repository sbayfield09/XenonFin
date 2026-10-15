CXX ?= g++
CPPFLAGS += -Iinclude
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic -pthread
ifeq ($(DEPS_ROOT),)
CPPFLAGS += $(shell pkg-config --cflags libcurl sdl2 freetype2 libavcodec libavformat libavutil libswscale libswresample)
LDLIBS += $(shell pkg-config --libs libcurl sdl2 freetype2 libavcodec libavformat libavutil libswscale libswresample) -pthread
else
CPPFLAGS += -I$(DEPS_ROOT)/usr/include -I$(DEPS_ROOT)/usr/include/x86_64-linux-gnu -I$(DEPS_ROOT)/usr/include/SDL2 -I$(DEPS_ROOT)/usr/include/freetype2
LDLIBS += -lcurl -l:libSDL2-2.0.so.0 -l:libfreetype.so.6 -l:libavcodec.so.61 -l:libavformat.so.61 -l:libavutil.so.59 -l:libswscale.so.8 -l:libswresample.so.5 -pthread
endif
CORE = src/network.cpp src/settings.cpp src/jellyfin.cpp src/keyboard.cpp
PLAYER = src/playback.cpp src/images.cpp
APP = src/main.cpp src/ui.cpp src/graphics.cpp src/input.cpp
OBJ = $(patsubst %.cpp,build/%.o,$(CORE) $(PLAYER) $(APP))
.PHONY: all test integration sanitize native-test xenon clean
all: build/xenonfin build/core-tests build/ui-tests
build/xenonfin: $(OBJ)
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@
build/core-tests: $(patsubst %.cpp,build/%.o,$(CORE)) build/src/input.o build/tests/core_tests.o
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@
build/ui-tests: $(filter-out build/src/main.o,$(OBJ)) build/tests/ui_flow.o
	$(CXX) $(CXXFLAGS) $^ $(LDLIBS) -o $@
build/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
test: build/core-tests
	./build/core-tests
integration: all
	python3 tests/integration.py
build/sanitized-tests: $(CORE) src/input.cpp tests/core_tests.cpp $(wildcard include/xenonfin/*.hpp)
	@mkdir -p build
	$(CXX) $(CPPFLAGS) -std=c++17 -O0 -g -pthread -fsanitize=address,undefined -fno-omit-frame-pointer $(CORE) src/input.cpp tests/core_tests.cpp $(LDLIBS) -o $@
sanitize: build/sanitized-tests
	ASAN_OPTIONS=detect_leaks=1 ./build/sanitized-tests
xenon:
	$(MAKE) -C platform/xenon
native-test:
	$(MAKE) -C platform/xenon/tests all test
clean:
	rm -rf build
-include $(wildcard build/src/*.d build/tests/*.d)

.PHONY: video-test-file native-video-test
video-test-file: build/video-fixtures/xenonfin-test.m1v
build/video-fixtures/xenonfin-test.m1v: scripts/make_video_test.sh
	bash scripts/make_video_test.sh $@
native-video-test: native-test
	python3 tests/native_video.py

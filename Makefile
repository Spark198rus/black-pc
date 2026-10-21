# Makefile para Compilação Incremental do Executável Nativo Linux x86 (black_linux)

CXX ?= g++
CXXFLAGS = -m32 -march=i686 -msse -msse2 -mstackrealign -std=c++20 -O2 -g -Wall -Wextra -fno-pie \
           -I. -Iloader/src -Iloader/src/platform \
           -Itools/dxvk-native/usr/include/dxvk -Itools/dxvk-native/usr/include/dxvk/wsi \
           -I/usr/include/SDL2 -I/usr/include/i386-linux-gnu/SDL2 \
           -Itools/sdl2/include -Itools/sdl2/include/SDL2 -Itools/sdl2/include/i386-linux-gnu

LDFLAGS = -m32 -no-pie -lpthread \
          -Ltools/dxvk-native/usr/lib32 -ldxvk_d3d9 \
          -Ltools/sdl2/lib -lSDL2 \
          -Wl,-rpath,'$$ORIGIN/lib:$$ORIGIN/tools/dxvk-native/usr/lib32:$$ORIGIN/tools/sdl2/lib'

DXVK_DIR = tools/dxvk-native
DXVK_SO  = $(DXVK_DIR)/usr/lib32/libdxvk_d3d9.so
DXVK_URL = https://github.com/doitsujin/dxvk/releases/download/v3.1.1/dxvk-native-3.1.1-steamrt-sniper.tar.gz


SRCS = \
    loader/src/platform/main_linux.cpp \
    loader/src/platform/xbe_loader.cpp \
    loader/src/platform/k_exports_posix.cpp \
    loader/src/platform/k_file_posix.cpp \
    loader/src/platform/k_memory_posix.cpp \
    loader/src/platform/k_sync_posix.cpp \
    loader/src/platform/k_thread_posix.cpp \
    loader/src/platform/k_timing_posix.cpp \
    loader/src/platform/host_sdl.cpp \
    loader/src/platform/input_sdl.cpp \
    loader/src/platform/audio_sdl.cpp \
    loader/src/platform/host_d3d9_linux.cpp \
    loader/src/platform/d3dcompiler_posix.cpp \
    loader/src/platform/perf_posix.cpp \
    loader/src/fe/pages.cpp \
    loader/src/fe/apt.cpp \
    loader/src/fe/bind_list.cpp \
    loader/src/fe/calls.cpp \
    loader/src/fe/pause_ui.cpp \
    loader/src/fe/pc_settings.cpp \
    loader/src/fe/vars.cpp \
    loader/src/d3d/device.cpp \
    loader/src/d3d/dispatch.cpp \
    loader/src/d3d/draw.cpp \
    loader/src/d3d/formats.cpp \
    loader/src/d3d/nv2a_psh.cpp \
    loader/src/d3d/nv2a_vsh.cpp \
    loader/src/d3d/pshader.cpp \
    loader/src/d3d/pushbuf.cpp \
    loader/src/d3d/resources.cpp \
    loader/src/d3d/texture.cpp \
    loader/src/d3d/vshader.cpp \
    loader/src/audio/reverb.cpp \
    loader/src/audio/dsound.cpp \
    loader/src/input/xinput.cpp \
    loader/src/input/autoaim.cpp \
    loader/src/input/bindings.cpp \
    loader/src/input/mouselook.cpp \
    loader/src/input/movies.cpp \
    loader/src/fov.cpp \
    loader/src/hooks.cpp \
    loader/src/settings.cpp \
    loader/src/sha1.cpp \
    loader/src/log.cpp

OBJS = $(patsubst %.cpp,build/%.o,$(SRCS))

TARGET = black_linux

all: $(TARGET)

$(DXVK_SO):
	@mkdir -p $(DXVK_DIR)
	@echo "[deps] Fetching DXVK-Native 3.1.1..."
	@curl -sL $(DXVK_URL) | tar -zx -C $(DXVK_DIR)/

deps: $(DXVK_SO)

$(TARGET): $(DXVK_SO) $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

build/%.o: %.cpp $(DXVK_SO)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

check: all
	$(MAKE) -C tests run

clean:
	rm -rf build $(TARGET)

.PHONY: all check clean deps

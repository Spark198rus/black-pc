# Makefile para a Suíte de Testes e Validação do Port Nativo Linux x86 (BLACK PC)

CXX ?= g++
CXXFLAGS = -m32 -std=c++20 -O2 -Wall -Wextra -fno-pie -I.. -I../loader/src
LDFLAGS = -m32 -no-pie -lpthread

SDL_CFLAGS ?= $(shell pkg-config --cflags sdl2 2>/dev/null || echo "-I/usr/include/SDL2 -I/usr/include/i386-linux-gnu/SDL2") -I../tools/sdl2/include -I../tools/sdl2/include/SDL2 -I../tools/sdl2/include/i386-linux-gnu
SDL_LDFLAGS ?= $(shell pkg-config --libs sdl2 2>/dev/null || echo "-L/usr/lib/i386-linux-gnu -lSDL2") -L../tools/sdl2/lib -Wl,-rpath,'$$ORIGIN/../tools/sdl2/lib'

DXVK_CFLAGS = -I../tools/dxvk-native/usr/include/dxvk -I../tools/dxvk-native/usr/include/dxvk/wsi $(SDL_CFLAGS)
DXVK_LDFLAGS = -L../tools/dxvk-native/usr/lib32 -Wl,-rpath,'$$ORIGIN/../tools/dxvk-native/usr/lib32' -ldxvk_d3d9 $(SDL_LDFLAGS)

# 1. Core & ABI Architecture
CORE_TARGETS = test_abi test_kpcr_fs test_memory_layout

# 2. XBE Format & Dynamic Trampolines
XBE_TARGETS = test_xbe_parser test_xbe_loader_map test_thunk_dispatcher test_boot_entry

# 3. Kernel HLE POSIX Subsystems
KERNEL_TARGETS = test_k_sync_posix test_k_file_posix test_k_thread_posix test_k_memory_posix

# 4. Host Platform Subsystems (SDL2)
HOST_TARGETS = test_sdl_window test_sdl_input test_sdl_audio test_mouselook_pipeline

# 5. Graphics Subsystem & Shaders (DXVK Vulkan)
GRAPHICS_TARGETS = test_dxvk_device test_dxvk_draw_pipeline test_d3dcompiler_posix

# 6. End-to-End Integration
INTEGRATION_TARGETS = test_fe_apt test_boot_pipeline test_host_loop test_render_pipeline

TARGETS = $(CORE_TARGETS) $(XBE_TARGETS) $(KERNEL_TARGETS) $(HOST_TARGETS) $(GRAPHICS_TARGETS) $(INTEGRATION_TARGETS)

all: $(TARGETS)

# --- Core & ABI ---
test_abi: test_abi.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

test_kpcr_fs: test_kpcr_fs.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

test_memory_layout: test_memory_layout.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

# --- XBE Format & Trampolines ---
test_xbe_parser: test_xbe_parser.cpp ../loader/src/platform/xbe_loader.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

test_xbe_loader_map: test_xbe_loader_map.cpp ../loader/src/platform/xbe_loader.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

test_thunk_dispatcher: test_thunk_dispatcher.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

test_boot_entry: test_boot_entry.cpp ../loader/src/platform/xbe_loader.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

# --- Kernel HLE POSIX ---
test_k_sync_posix: test_k_sync_posix.cpp ../loader/src/platform/k_sync_posix.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

test_k_file_posix: test_k_file_posix.cpp ../loader/src/platform/k_file_posix.cpp ../loader/src/platform/k_sync_posix.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

test_k_thread_posix: test_k_thread_posix.cpp ../loader/src/platform/k_thread_posix.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

test_k_memory_posix: test_k_memory_posix.cpp ../loader/src/platform/k_memory_posix.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

# --- Host Platform (SDL2) ---
test_sdl_window: test_sdl_window.cpp ../loader/src/platform/host_sdl.cpp
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) $^ -o $@ $(LDFLAGS) $(SDL_LDFLAGS)

test_sdl_input: test_sdl_input.cpp ../loader/src/platform/input_sdl.cpp ../loader/src/platform/host_sdl.cpp
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) $^ -o $@ $(LDFLAGS) $(SDL_LDFLAGS)

test_sdl_audio: test_sdl_audio.cpp ../loader/src/platform/audio_sdl.cpp
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) $^ -o $@ $(LDFLAGS) $(SDL_LDFLAGS)

test_mouselook_pipeline: test_mouselook_pipeline.cpp ../loader/src/platform/host_sdl.cpp \
                         ../loader/src/platform/input_sdl.cpp \
                         ../loader/src/input/mouselook.cpp \
                         ../loader/src/log.cpp
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) $^ -o $@ $(LDFLAGS) $(SDL_LDFLAGS)

# --- Graphics Subsystem (DXVK Vulkan) ---
test_dxvk_device: test_dxvk_device.cpp
	$(CXX) $(CXXFLAGS) $(DXVK_CFLAGS) $< -o $@ $(LDFLAGS) $(DXVK_LDFLAGS)

test_dxvk_draw_pipeline: test_dxvk_draw_pipeline.cpp ../loader/src/platform/d3dcompiler_posix.cpp
	$(CXX) $(CXXFLAGS) $(DXVK_CFLAGS) $^ -o $@ $(LDFLAGS) $(DXVK_LDFLAGS)

test_d3dcompiler_posix: test_d3dcompiler_posix.cpp ../loader/src/platform/d3dcompiler_posix.cpp
	$(CXX) $(CXXFLAGS) $(DXVK_CFLAGS) $^ -o $@ $(LDFLAGS) $(DXVK_LDFLAGS)

# --- End-to-End Integration ---
test_fe_apt: test_fe_apt.cpp ../loader/src/fe/pages.cpp ../loader/src/fe/apt.cpp ../loader/src/fe/pc_settings.cpp ../loader/src/log.cpp
	$(CXX) $(CXXFLAGS) $(DXVK_CFLAGS) $^ -o $@ $(LDFLAGS)

test_boot_pipeline: test_boot_pipeline.cpp ../loader/src/platform/xbe_loader.cpp \
                    ../loader/src/platform/k_memory_posix.cpp \
                    ../loader/src/platform/k_file_posix.cpp \
                    ../loader/src/platform/k_sync_posix.cpp \
                    ../loader/src/platform/k_thread_posix.cpp \
                    ../loader/src/platform/k_timing_posix.cpp \
                    ../loader/src/platform/k_exports_posix.cpp \
                    ../loader/src/sha1.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

test_host_loop: test_host_loop.cpp ../loader/src/platform/host_sdl.cpp \
                ../loader/src/platform/input_sdl.cpp \
                ../loader/src/platform/audio_sdl.cpp
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) $^ -o $@ $(LDFLAGS) $(SDL_LDFLAGS)

test_render_pipeline: test_render_pipeline.cpp ../loader/src/platform/host_d3d9_linux.cpp \
                     ../loader/src/platform/host_sdl.cpp \
                     ../loader/src/platform/input_sdl.cpp \
                     ../loader/src/platform/audio_sdl.cpp \
                     ../loader/src/platform/d3dcompiler_posix.cpp
	$(CXX) $(CXXFLAGS) $(DXVK_CFLAGS) $^ -o $@ $(LDFLAGS) $(DXVK_LDFLAGS)

check: run

run: all
	@echo "=========================================================="
	@echo "    EXECUTANDO BATERIA DE TESTES DO PORT LINUX x86        "
	@echo "=========================================================="
	@echo ""
	@echo "---------------- [ 1. CORE & ABI ] ----------------"
	./test_abi
	@echo ""
	./test_kpcr_fs
	@echo ""
	./test_memory_layout
	@echo ""
	@echo "---------------- [ 2. FORMATO XBE & TRAMPOLINS ] ----------------"
	./test_xbe_parser ../game/default.xbe
	@echo ""
	./test_xbe_loader_map ../game/default.xbe
	@echo ""
	./test_thunk_dispatcher
	@echo ""
	./test_boot_entry ../game/default.xbe
	@echo ""
	@echo "---------------- [ 3. KERNEL HLE POSIX ] ----------------"
	./test_k_sync_posix
	@echo ""
	./test_k_file_posix
	@echo ""
	./test_k_thread_posix
	@echo ""
	./test_k_memory_posix
	@echo ""
	@echo "---------------- [ 4. PLATAFORMA HOST (SDL2) ] ----------------"
	./test_sdl_window
	@echo ""
	./test_sdl_input
	@echo ""
	./test_sdl_audio
	@echo ""
	./test_mouselook_pipeline
	@echo ""
	@echo "---------------- [ 5. SUBSISTEMA GRÁFICO (DXVK VULKAN) ] ----------------"
	DXVK_WSI_DRIVER=SDL2 ./test_dxvk_device
	@echo ""
	DXVK_WSI_DRIVER=SDL2 ./test_dxvk_draw_pipeline
	@echo ""
	DXVK_WSI_DRIVER=SDL2 ./test_d3dcompiler_posix
	@echo ""
	@echo "---------------- [ 6. INTEGRAÇÃO END-TO-END ] ----------------"
	./test_fe_apt
	@echo ""
	./test_boot_pipeline ../game/default.xbe
	@echo ""
	./test_host_loop
	@echo ""
	DXVK_WSI_DRIVER=SDL2 ./test_render_pipeline
	@echo ""
	@echo "=========================================================="
	@echo "       TODOS OS TESTES FORAM CONCLUÍDOS COM SUCESSO!      "
	@echo "=========================================================="

demo: test_render_pipeline
	DXVK_WSI_DRIVER=SDL2 ./test_render_pipeline --interactive

demo-host: test_host_loop
	./test_host_loop --interactive

demo-render: demo

run-gui: demo

clean:
	rm -f $(TARGETS)

.PHONY: all run check run-gui demo demo-host demo-render clean

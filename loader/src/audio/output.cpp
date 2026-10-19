// XAudio2 (xaudio2_9.dll ships with Windows 10, loaded at runtime): a mastering voice and one float
// source voice that the mixer keeps QUEUED blocks ahead of the device.
//
// XAudio2 stops for good when its device goes away (sleep and wake with HDMI or USB audio, headphones unplugged,
// another default device): OnCriticalError, and the source voice takes no more buffers. The mixer then got no
// requests, the game's DirectSound buffers stopped playing and the game waited on its sound. So blocks_wanted
// watches for that (the error, or no block taken for a second), closes XAudio2 and opens it again on the default
// device once a second; meanwhile, and without XAudio2 at all, it asks for blocks by the clock (silent).
#include "audio/output.h"

#include "common.h"

#include <windows.h>
#include <xaudio2.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace audio_out {
namespace {

constexpr uint32_t QUEUED = 6, RING = QUEUED + 1; // 60 ms ahead of the device

typedef HRESULT(WINAPI *PFN_Create)(IXAudio2 **, UINT32, XAUDIO2_PROCESSOR);
PFN_Create g_create;
IXAudio2 *g_xa;
IXAudio2MasteringVoice *g_master;
IXAudio2SourceVoice *g_src;
float g_ring[RING][BLOCK * 2];
uint32_t g_next;
FILE *g_wav;
uint32_t g_wav_frames;
std::atomic<HRESULT> g_error{S_OK};
ULONGLONG g_last_taken, g_retry_at; // GetTickCount64: the device last took a block; the next reopen
LARGE_INTEGER g_clock_start;
uint64_t g_clock_blocks; // asked for by the clock since g_clock_start (0: not running)

struct EngineCallback : IXAudio2EngineCallback {
    void STDMETHODCALLTYPE OnProcessingPassStart() override {}
    void STDMETHODCALLTYPE OnProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnCriticalError(HRESULT error) override { g_error = error; }
} g_callback;

void wav_header()
{
    uint32_t data = g_wav_frames * 4;
    struct {
        char riff[4]; uint32_t size; char wave[4], fmt[4]; uint32_t fmt_size;
        uint16_t tag, ch; uint32_t rate, bytes_per_s; uint16_t align, bits;
        char data[4]; uint32_t data_size;
    } h = {{'R', 'I', 'F', 'F'}, 36 + data, {'W', 'A', 'V', 'E'}, {'f', 'm', 't', ' '}, 16, 1, 2, RATE, RATE * 4, 4, 16,
           {'d', 'a', 't', 'a'}, data};
    fseek(g_wav, 0, SEEK_SET);
    fwrite(&h, sizeof h, 1, g_wav);
    fseek(g_wav, 0, SEEK_END);
}

void close_engine()
{
    if (g_src) g_src->DestroyVoice();
    if (g_master) g_master->DestroyVoice();
    if (g_xa) {
        g_xa->UnregisterForCallbacks(&g_callback);
        g_xa->StopEngine();
        g_xa->Release();
    }
    g_src = nullptr;
    g_master = nullptr;
    g_xa = nullptr;
}

HRESULT start_engine()
{
    g_error = S_OK;
    HRESULT hr = g_create(&g_xa, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (SUCCEEDED(hr)) hr = g_xa->RegisterForCallbacks(&g_callback);
    if (SUCCEEDED(hr)) hr = g_xa->CreateMasteringVoice(&g_master, 2, RATE);
    if (SUCCEEDED(hr) && g_cfg.background) g_master->SetVolume(0.0f); // tests: keep the timing, not the sound
    WAVEFORMATEX wf = {};
    wf.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    wf.nChannels = 2;
    wf.nSamplesPerSec = RATE;
    wf.wBitsPerSample = 32;
    wf.nBlockAlign = 8;
    wf.nAvgBytesPerSec = RATE * 8;
    if (SUCCEEDED(hr)) hr = g_xa->CreateSourceVoice(&g_src, &wf, XAUDIO2_VOICE_NOPITCH);
    if (SUCCEEDED(hr)) hr = g_src->Start();
    if (FAILED(hr)) close_engine();
    g_last_taken = GetTickCount64();
    return hr;
}

// Blocks due by the clock (no device to pace the mixer). More than a second behind (the PC slept) is skipped,
// not mixed in a burst.
uint32_t clock_blocks()
{
    LARGE_INTEGER f, now;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&now);
    if (!g_clock_blocks) {
        g_clock_start = now;
        g_clock_blocks = 1;
    }
    uint64_t due = (uint64_t)(now.QuadPart - g_clock_start.QuadPart) * RATE / BLOCK / f.QuadPart + 1;
    if (due > g_clock_blocks + RATE / BLOCK) g_clock_blocks = due - 1;
    uint32_t want = due > g_clock_blocks ? (uint32_t)std::min<uint64_t>(due - g_clock_blocks, 10) : 0;
    g_clock_blocks += want;
    return want;
}

} // namespace

bool open()
{
    if (!g_cfg.wav_path.empty() && !_wfopen_s(&g_wav, g_cfg.wav_path.c_str(), L"wb")) {
        wav_header();
        logf("[audio] recording the mix to a WAV file"); // its log time = the WAV's start, to line up captures
    }
    HMODULE m = LoadLibraryW(L"xaudio2_9.dll");
    if (!m) m = LoadLibraryW(L"xaudio2_8.dll");
    g_create = m ? (PFN_Create)GetProcAddress(m, "XAudio2Create") : nullptr;
    if (!g_create) {
        logf("[audio] XAudio2 not available: no sound");
        return false;
    }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED); // XAudio2 needs COM on the creating thread (the mixer's)
    HRESULT hr = start_engine();
    if (FAILED(hr)) {
        logf("[audio] XAudio2 setup failed 0x%08lx: no sound", hr);
        g_create = nullptr;
        return false;
    }
    logf("[audio] XAudio2 output: %u Hz stereo, %u ms blocks, %u queued", RATE, BLOCK * 1000 / RATE, QUEUED);
    return true;
}

uint32_t blocks_wanted()
{
    if (!g_create) return clock_blocks();
    ULONGLONG now = GetTickCount64();
    if (g_src && (g_error != S_OK || now - g_last_taken > 1000)) {
        if (g_error != S_OK) logf("[audio] XAudio2 output failed (0x%08lx); opening it again", (unsigned long)g_error.load());
        else logf("[audio] XAudio2 output took no sound for %.1f s; opening it again", (now - g_last_taken) / 1000.0);
        close_engine();
        g_retry_at = now;
        g_clock_blocks = 0;
    }
    if (!g_src) {
        if (now >= g_retry_at) {
            g_retry_at = now + 1000;
            if (SUCCEEDED(start_engine())) logf("[audio] XAudio2 output open again");
        }
        if (!g_src) return clock_blocks();
    }
    XAUDIO2_VOICE_STATE st;
    g_src->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    static uint32_t underruns, submitted_before;
    if (st.BuffersQueued == 0 && g_next != submitted_before) { // the device ran dry since the last check: a gap
        if (++underruns <= 5 || underruns % 100 == 0) logf("[audio] output underrun #%u (the mixer fell behind)", underruns);
    }
    submitted_before = g_next;
    if (st.BuffersQueued < QUEUED) {
        g_last_taken = now;
        return QUEUED - st.BuffersQueued;
    }
    return 0;
}

void simulate_lost()
{
    logf("[audio] tests: acting as if the XAudio2 device had gone");
    g_error = XAUDIO2_E_DEVICE_INVALIDATED;
}

void submit(const float *lr)
{
    if (g_wav) {
        int16_t pcm[BLOCK * 2];
        for (uint32_t i = 0; i < BLOCK * 2; i++) pcm[i] = (int16_t)(std::max(-1.0f, std::min(1.0f, lr[i])) * 32767.0f);
        fwrite(pcm, sizeof pcm, 1, g_wav);
        g_wav_frames += BLOCK;
        if (g_wav_frames % (RATE / 2) == 0) wav_header(); // keep the header valid if the process is killed
    }
    if (!g_src) return;
    float *b = g_ring[g_next];
    g_next = (g_next + 1) % RING;
    memcpy(b, lr, sizeof g_ring[0]);
    XAUDIO2_BUFFER xb = {};
    xb.AudioBytes = sizeof g_ring[0];
    xb.pAudioData = (const BYTE *)b;
    g_src->SubmitSourceBuffer(&xb);
}

} // namespace audio_out

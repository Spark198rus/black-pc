// HLE replacement for the Xbox DirectSound library (DSOUND section), which talks to the MCPX audio
// hardware. Buffers and streams are mixed in software into one 48 kHz stereo host stream (XAudio2):
// PCM 8/16-bit and Xbox ADPCM sources, linear resampling, volume, mix-bin routing folded down to
// stereo, and a simple 3D model (distance rolloff + panning) for 3D voices. The output clock drives
// play positions and stream packet completion, so the game's audio/music/video code runs as on the Xbox.
// Without a host audio device the same mixer runs on a timer and the result is discarded.
#include "audio/reverb.h"
#include "audio/output.h"
#include "d3d/dispatch.h"
#include "kernel/kexports.h"
#include "xthread.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace xdsound {
namespace {

// ---- Xbox structures ----

#pragma pack(push, 1)
struct WaveFormatEx {
    uint16_t wFormatTag, nChannels;
    uint32_t nSamplesPerSec, nAvgBytesPerSec;
    uint16_t nBlockAlign, wBitsPerSample, cbSize;
};
#pragma pack(pop)

struct MixBinVolumePair {
    uint32_t dwMixBin;
    int32_t lVolume;
};
struct MixBins { // DSMIXBINS
    uint32_t dwMixBinCount;
    const MixBinVolumePair *lpMixBinVolumePairs;
};

struct BufferDesc { // DSBUFFERDESC
    uint32_t dwSize, dwFlags, dwBufferBytes;
    WaveFormatEx *lpwfxFormat;
    const MixBins *lpMixBins;
    uint32_t dwInputMixBin;
};

struct StreamDesc { // DSSTREAMDESC
    uint32_t dwFlags, dwMaxAttachedPackets;
    WaveFormatEx *lpwfxFormat;
    void(__stdcall *lpfnCallback)(void *streamContext, void *packetContext, uint32_t status);
    void *lpvContext;
    const MixBins *lpMixBins;
};

struct MediaPacket { // XMEDIAPACKET
    void *pvBuffer;
    uint32_t dwMaxSize;
    uint32_t *pdwCompletedSize;
    uint32_t *pdwStatus;
    void *hCompletionEventOrContext;
    int64_t *prtTimestamp;
};

struct Vec3 {
    float x, y, z;
};

struct Ds3dBuffer { // DS3DBUFFER (Xbox: the standard fields plus per-voice distance/rolloff/doppler factors)
    uint32_t dwSize;
    Vec3 vPosition, vVelocity;
    uint32_t dwInsideConeAngle, dwOutsideConeAngle;
    Vec3 vConeOrientation;
    int32_t lConeOutsideVolume;
    float flMinDistance, flMaxDistance;
    uint32_t dwMode;
    float flDistanceFactor, flRolloffFactor, flDopplerFactor;
};

constexpr uint32_t PACKET_PENDING = 0x8000000A;
constexpr uint32_t PACKET_FLUSHED = 0x80004004;
constexpr uint32_t DSBSTATUS_PLAYING = 1, DSBSTATUS_LOOPING = 4;
constexpr uint32_t DSBPLAY_LOOPING = 1, DSBPLAY_FROMSTART = 2;
constexpr uint32_t DSSTREAMSTATUS_READY = 1, DSSTREAMSTATUS_PLAYING = 0x10000, DSSTREAMSTATUS_PAUSED = 0x20000,
                   DSSTREAMSTATUS_STARVED = 0x40000;
constexpr uint32_t CAPS_CTRL3D = 0x10;
constexpr uint32_t DS3DMODE_HEADRELATIVE = 1, DS3DMODE_DISABLE = 2;
constexpr uint16_t FORMAT_XBOX_ADPCM = 0x69;
constexpr uint32_t ADPCM_SAMPLES = 65; // per 36-byte block per channel (header sample + 64 nibbles)

// ---- emulated objects (Xbox-visible header: vtable pointer first) ----

constexpr uint32_t OBJ_MAGIC = 0x48534458; // 'XDSH'

struct Object {
    const void *const *vtbl;
    uint32_t magic;
    uint32_t refs;
    uint32_t kind; // 1 DirectSound, 2 buffer, 3 stream
};

// Playback state shared by buffers and streams.
struct Voice : Object {
    WaveFormatEx fmt = {};
    int32_t volume = 0;     // hundredths of dB
    uint32_t headroom = 0;
    uint32_t frequency = 0; // 0 = format rate
    uint32_t nbins = 0;     // 0 = default routing
    MixBinVolumePair bins[8] = {};
    bool is3d = false;
    Ds3dBuffer ds3d = {};
    // resampler: the two source frames around the output position
    float s0[2] = {}, s1[2] = {};
    double phase = 0;
    bool primed = false;
    // Xbox ADPCM: one decoded block
    const uint8_t *dec_ptr = nullptr;
    int16_t dec[2][ADPCM_SAMPLES] = {};
};

struct Buffer : Voice {
    uint8_t *data = nullptr;
    uint32_t bytes = 0;
    uint32_t cur = 0; // next source frame to fetch
    uint32_t loop_start = 0, loop_len = 0, play_start = 0, play_len = 0; // bytes
    bool playing = false, looping = false;
};

struct Stream : Voice {
    StreamDesc desc = {};
    std::deque<MediaPacket> queue;
    uint32_t consumed = 0; // frames of the front packet already fetched
    bool paused = false;
};

struct Completion {
    MediaPacket p;
    Stream *s;
    uint32_t status;
};

struct Listener {
    Vec3 pos = {0, 0, 0}, front = {0, 0, 1}, top = {0, 1, 0};
    float rolloff = 1.0f, distance = 1.0f;
};

std::recursive_mutex g_mutex;
std::vector<Buffer *> g_buffers;
std::vector<Stream *> g_streams;
Object g_dsound;
Listener g_listener;
std::atomic<bool> g_worker_started{false};

// ---- decoding ----

uint32_t channels(const WaveFormatEx &f) { return f.nChannels ? f.nChannels : 1; }

uint32_t bytes_to_frames(const WaveFormatEx &f, uint32_t bytes)
{
    if (!f.nBlockAlign) return 0;
    if (f.wFormatTag == FORMAT_XBOX_ADPCM) return bytes / f.nBlockAlign * ADPCM_SAMPLES;
    return bytes / f.nBlockAlign;
}

uint32_t frames_to_bytes(const WaveFormatEx &f, uint32_t frames)
{
    if (f.wFormatTag == FORMAT_XBOX_ADPCM) return frames / ADPCM_SAMPLES * f.nBlockAlign;
    return frames * f.nBlockAlign;
}

// IMA ADPCM step tables (public IMA/DVI specification).
const int16_t kSteps[89] = {7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,
                            25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,
                            88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,   253,   279,
                            307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,   963,
                            1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,
                            3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487,
                            12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
const int8_t kIndexAdjust[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

// Xbox ADPCM block: per channel a 4-byte header {int16 sample, uint8 step index, pad}, then 4-byte
// groups of 8 nibbles (low nibble first) interleaved by channel.
void adpcm_block(const uint8_t *bp, uint32_t ch, int16_t out[2][ADPCM_SAMPLES])
{
    for (uint32_t c = 0; c < ch && c < 2; c++) {
        int pred = (int16_t)(bp[4 * c] | (bp[4 * c + 1] << 8));
        int index = std::min<int>(bp[4 * c + 2], 88);
        out[c][0] = (int16_t)pred;
        for (uint32_t k = 0; k < 8; k++) {
            const uint8_t *d = bp + 4 * ch + (k * ch + c) * 4;
            for (uint32_t j = 0; j < 8; j++) {
                int nib = (d[j >> 1] >> ((j & 1) * 4)) & 15;
                int step = kSteps[index];
                int diff = step >> 3;
                if (nib & 1) diff += step >> 2;
                if (nib & 2) diff += step >> 1;
                if (nib & 4) diff += step;
                pred += (nib & 8) ? -diff : diff;
                pred = std::clamp(pred, -32768, 32767);
                index = std::clamp(index + kIndexAdjust[nib], 0, 88);
                out[c][1 + k * 8 + j] = (int16_t)pred;
            }
        }
    }
}

void decode_frame(Voice &v, const uint8_t *base, uint32_t frame, float *f)
{
    const WaveFormatEx &w = v.fmt;
    uint32_t ch = channels(w);
    if (w.wFormatTag == FORMAT_XBOX_ADPCM) {
        const uint8_t *bp = base + (frame / ADPCM_SAMPLES) * w.nBlockAlign;
        if (v.dec_ptr != bp) {
            adpcm_block(bp, ch, v.dec);
            v.dec_ptr = bp;
        }
        uint32_t i = frame % ADPCM_SAMPLES;
        f[0] = v.dec[0][i] / 32768.0f;
        f[1] = ch > 1 ? v.dec[1][i] / 32768.0f : f[0];
    } else if (w.wBitsPerSample == 8) {
        const uint8_t *p = base + (size_t)frame * ch;
        f[0] = (p[0] - 128) / 128.0f;
        f[1] = ch > 1 ? (p[1] - 128) / 128.0f : f[0];
    } else {
        const int16_t *p = (const int16_t *)base + (size_t)frame * ch;
        f[0] = p[0] / 32768.0f;
        f[1] = ch > 1 ? p[1] / 32768.0f : f[0];
    }
}

// Xbox buffers play the play region [play_start, +play_len) of their data; the loop region and the
// play cursor (Get/SetCurrentPosition) are relative to the play region's start.
uint32_t region_frames(const Buffer &b)
{
    uint32_t start = std::min(b.play_start, b.bytes);
    uint32_t len = b.play_len ? std::min(b.play_len, b.bytes - start) : b.bytes - start;
    return bytes_to_frames(b.fmt, len);
}

bool buffer_fetch(Buffer &b, float *f)
{
    uint32_t end = region_frames(b);
    if (b.looping) {
        uint32_t ls = std::min(bytes_to_frames(b.fmt, b.loop_start), end);
        uint32_t le = b.loop_len ? std::min(end, ls + bytes_to_frames(b.fmt, b.loop_len)) : end;
        if (b.cur >= le) b.cur = ls < le ? ls : 0;
    }
    if (b.cur >= end) return false;
    decode_frame(b, b.data + std::min(b.play_start, b.bytes), b.cur++, f);
    return true;
}

void finish_packet(const MediaPacket &p, Stream *s, uint32_t status, std::vector<Completion> &out)
{
    if (p.pdwCompletedSize) *p.pdwCompletedSize = status == 0 ? p.dwMaxSize : 0;
    if (p.pdwStatus) *p.pdwStatus = status;
    out.push_back({p, s, status});
}

bool stream_fetch(Stream &s, float *f, std::vector<Completion> &done)
{
    while (!s.queue.empty()) {
        MediaPacket &p = s.queue.front();
        if (s.consumed < bytes_to_frames(s.fmt, p.dwMaxSize)) {
            decode_frame(s, (const uint8_t *)p.pvBuffer, s.consumed++, f);
            return true;
        }
        MediaPacket d = p;
        s.queue.pop_front();
        s.consumed = 0;
        s.dec_ptr = nullptr; // the game refills packet memory in place
        finish_packet(d, &s, 0, done);
    }
    return false;
}

void deliver(std::vector<Completion> &done)
{
    for (const Completion &c : done) {
        if (c.s->desc.lpfnCallback) {
            uint8_t old = xthread_raise_irql(2); // the Xbox calls these from its DPC
            c.s->desc.lpfnCallback(c.s->desc.lpvContext, c.p.hCompletionEventOrContext, c.status);
            xthread_lower_irql(old);
        } else if (c.p.hCompletionEventOrContext) {
            SetEvent((HANDLE)c.p.hCompletionEventOrContext);
        }
    }
    done.clear();
}

// ---- mixing ----

float db_to_amp(int32_t hundredths) { return hundredths <= -10000 ? 0.0f : powf(10.0f, hundredths / 2000.0f); }

Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

// m[source channel][output L/R]; send[source channel] = level into the I3DL2 reverb (mix bin 11, FXSEND_0:
// the DSP image's effect 16 reads it and writes the four corner speakers)
void voice_gains(const Voice &v, float m[2][2], float send[2])
{
    float amp = db_to_amp(v.volume);
    m[0][0] = m[0][1] = m[1][0] = m[1][1] = 0.0f;
    send[0] = send[1] = 0.0f;
    for (uint32_t k = 0; k < v.nbins; k++)
        if (v.bins[k].dwMixBin == 11) {
            float a = amp * db_to_amp(v.bins[k].lVolume) * (channels(v.fmt) > 1 ? 0.5f : 1.0f);
            send[0] = send[1] = a;
        }
    if (v.is3d && v.ds3d.dwMode != DS3DMODE_DISABLE) {
        const Ds3dBuffer &p = v.ds3d;
        Vec3 rel = p.dwMode == DS3DMODE_HEADRELATIVE ? p.vPosition : sub(p.vPosition, g_listener.pos);
        float d = sqrtf(dot(rel, rel));
        Vec3 right = cross(g_listener.top, g_listener.front); // left-handed: x = y cross z
        float rl = sqrtf(dot(right, right));
        float x = (d > 1e-4f && rl > 1e-6f) ? dot(rel, right) / (d * rl) : 0.0f;
        float mn = p.flMinDistance > 0 ? p.flMinDistance : 1.0f;
        float mx = p.flMaxDistance > mn ? p.flMaxDistance : 1e9f;
        float roll = g_listener.rolloff * (p.flRolloffFactor > 0 ? p.flRolloffFactor : 1.0f);
        float dd = std::clamp(d, mn, mx);
        float att = mn / (mn + roll * (dd - mn));
        x = std::clamp(x, -1.0f, 1.0f);
        float l = sqrtf(0.5f * (1 - x)) * 1.41421356f, r = sqrtf(0.5f * (1 + x)) * 1.41421356f;
        m[0][0] = m[1][0] = 0.5f * amp * att * l; // a stereo source is folded to mono first
        m[0][1] = m[1][1] = 0.5f * amp * att * r;
        if (channels(v.fmt) == 1) {
            m[0][0] *= 2;
            m[0][1] *= 2;
        }
        return;
    }
    uint32_t ch = channels(v.fmt) > 1 ? 2 : 1;
    MixBinVolumePair defaults[2] = {{0, 0}, {1, 0}};
    const MixBinVolumePair *bins = v.nbins ? v.bins : defaults;
    uint32_t n = v.nbins ? v.nbins : 2;
    for (uint32_t k = 0; k < n; k++) {
        float a = amp * db_to_amp(bins[k].lVolume);
        int src = ch == 2 ? (int)(k & 1) : 0;
        // The game's DSP effects image (not emulated) takes FXSEND_8-13 (bins 19-24) as FL/FR/C/LFE/BL/BR
        // and carries them to the speakers; fold those to stereo like the speaker bins.
        switch (bins[k].dwMixBin) {
        case 0: case 4: case 6: case 8: case 19: case 23: m[src][0] += a; break; // front/back/3D left
        case 1: case 5: case 7: case 9: case 20: case 24: m[src][1] += a; break; // front/back/3D right
        case 2: case 21: m[src][0] += a * 0.7071f; m[src][1] += a * 0.7071f; break; // center
        default: break; // LFE and the other effect sends are not mixed (11 = reverb, above)
        }
    }
}

float g_send[audio_out::BLOCK]; // reverb input of the block being mixed

template <typename Fetch> bool mix_voice(Voice &v, float *out, Fetch fetch)
{
    float m[2][2], send[2];
    voice_gains(v, m, send);
    bool wet = g_cfg.reverb && (send[0] > 0 || send[1] > 0);
    uint32_t rate = v.frequency ? v.frequency : v.fmt.nSamplesPerSec;
    double step = (rate ? rate : audio_out::RATE) / (double)audio_out::RATE;
    if (!v.primed) {
        if (!fetch(v.s0)) return false;
        if (!fetch(v.s1)) memcpy(v.s1, v.s0, sizeof v.s1);
        v.phase = 0;
        v.primed = true;
    }
    for (uint32_t i = 0; i < audio_out::BLOCK; i++) {
        float t = (float)v.phase;
        float a0 = v.s0[0] + (v.s1[0] - v.s0[0]) * t, a1 = v.s0[1] + (v.s1[1] - v.s0[1]) * t;
        out[2 * i] += a0 * m[0][0] + a1 * m[1][0];
        out[2 * i + 1] += a0 * m[0][1] + a1 * m[1][1];
        if (wet) g_send[i] += a0 * send[0] + a1 * send[1];
        v.phase += step;
        while (v.phase >= 1.0) {
            v.phase -= 1.0;
            memcpy(v.s0, v.s1, sizeof v.s0);
            if (!fetch(v.s1)) {
                v.primed = false;
                return false;
            }
        }
    }
    return true;
}

std::string voice_str(const Voice &v)
{
    std::string s = "vol " + std::to_string(v.volume) + " freq " + std::to_string(v.frequency) + (v.is3d ? " 3d" : "") + " bins";
    for (uint32_t i = 0; i < v.nbins; i++) s += " " + std::to_string(v.bins[i].dwMixBin) + ":" + std::to_string(v.bins[i].lVolume);
    return s;
}

// Every 2 s for the first minute (and then every 30 s): what the mixer is doing.
void mix_report(const float *out)
{
    static uint64_t blocks;
    static float peak;
    for (uint32_t i = 0; i < audio_out::BLOCK * 2; i++) peak = std::max(peak, fabsf(out[i]));
    uint64_t n = ++blocks;
    uint64_t every = n < 6000 ? 200 : 3000;
    if (n % every) return;
    uint32_t nb = 0, ns = 0;
    std::string first;
    for (Buffer *b : g_buffers)
        if (b->playing) {
            if (!nb++) first = voice_str(*b) + " data " + std::to_string(b->bytes);
        }
    for (Stream *s : g_streams)
        if (!s->paused && !s->queue.empty()) {
            if (!ns++ && first.empty()) first = "stream " + voice_str(*s);
        }
    logf("[dsound] mixer: %u buffers, %u streams playing, peak %.3f%s%s", nb, ns, peak, first.empty() ? "" : "; e.g. ", first.c_str());
    peak = 0;
}

void mix_block(float *out, std::vector<Completion> &done)
{
    memset(out, 0, sizeof(float) * audio_out::BLOCK * 2);
    memset(g_send, 0, sizeof g_send);
    std::lock_guard<std::recursive_mutex> g(g_mutex);
    for (Buffer *b : g_buffers) {
        if (!b->playing || !b->data || !b->bytes || !b->fmt.nBlockAlign) continue;
        if (!mix_voice(*b, out, [&](float *f) { return buffer_fetch(*b, f); })) b->playing = false;
    }
    for (Stream *s : g_streams) {
        if (s->paused || s->queue.empty() || !s->fmt.nBlockAlign) continue;
        mix_voice(*s, out, [&](float *f) { return stream_fetch(*s, f, done); });
    }
    if (g_cfg.reverb) {
        static float applied = -1.0f; // the menu can change the level while running
        if (applied != g_cfg.reverb_gain) reverb::set_gain(applied = g_cfg.reverb_gain);
        reverb::process(g_send, out, audio_out::BLOCK);
    }
    // soft knee above 0.8 instead of hard clipping (the MCPX/DSP stages that set final levels are not emulated)
    for (uint32_t i = 0; i < audio_out::BLOCK * 2; i++) {
        float x = out[i], a = fabsf(x);
        if (a > 0.8f) x = copysignf(0.8f + 0.2f * tanhf((a - 0.8f) / 0.2f), x);
        out[i] = x;
    }
    mix_report(out);
}

void __stdcall worker(void *)
{
    xthread_current()->base_priority = 2;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    { // the mixer is host code: keep it off the game's CPU (see pin_to_one_cpu)
        DWORD_PTR proc = 0, sys = 0;
        GetProcessAffinityMask(GetCurrentProcess(), &proc, &sys);
        if (g_game_cpu && (proc & ~g_game_cpu)) SetThreadAffinityMask(GetCurrentThread(), proc & ~g_game_cpu);
    }
    audio_out::open(); // without a device the output asks for blocks by the clock
    std::vector<Completion> done;
    static float block[audio_out::BLOCK * 2];
    for (;;) {
        Sleep(2);
        uint32_t want = audio_out::blocks_wanted();
        while (want--) {
            mix_block(block, done);
            audio_out::submit(block);
            deliver(done);
        }
    }
}

void start_worker()
{
    if (g_worker_started.exchange(true)) return;
    kthread_create(nullptr, nullptr, 0, 0, (uint32_t)(uintptr_t)&worker, 0, 0, false, "dsound");
    logf("[dsound] HLE DirectSound active (software mixer)");
}

std::string fmt_str(const WaveFormatEx *f)
{
    if (!f) return "no format";
    char b[96];
    snprintf(b, sizeof b, "tag 0x%x %uch %uHz %ubit align %u avg %u", f->wFormatTag, f->nChannels, f->nSamplesPerSec, f->wBitsPerSample,
             f->nBlockAlign, f->nAvgBytesPerSec);
    return b;
}

void set_bins(Voice &v, const MixBins *mb, bool volumes_only)
{
    if (!mb || !mb->lpMixBinVolumePairs) {
        if (!volumes_only) v.nbins = 0;
        return;
    }
    uint32_t n = std::min<uint32_t>(mb->dwMixBinCount, 8);
    if (!volumes_only) {
        v.nbins = n;
        for (uint32_t i = 0; i < n; i++) v.bins[i] = mb->lpMixBinVolumePairs[i];
        return;
    }
    for (uint32_t i = 0; i < n; i++) // update the volume of bins already assigned
        for (uint32_t k = 0; k < v.nbins; k++)
            if (v.bins[k].dwMixBin == mb->lpMixBinVolumePairs[i].dwMixBin) v.bins[k].lVolume = mb->lpMixBinVolumePairs[i].lVolume;
}

void init_voice(Voice &v, uint32_t kind, const WaveFormatEx *fmt, uint32_t flags, const MixBins *bins)
{
    v.magic = OBJ_MAGIC;
    v.refs = 1;
    v.kind = kind;
    if (fmt) v.fmt = *fmt;
    v.is3d = (flags & CAPS_CTRL3D) != 0;
    v.ds3d.flMinDistance = 1.0f;
    v.ds3d.flMaxDistance = 1e9f;
    v.ds3d.flRolloffFactor = 1.0f;
    set_bins(v, bins, false);
}

// ---- stream vtable (XMediaObject: AddRef, Release, GetInfo, GetStatus, Process, Discontinuity, Flush) ----

uint32_t __stdcall Stream_AddRef(Stream *s) { return ++s->refs; }

uint32_t __stdcall Stream_Release(Stream *s)
{
    std::vector<Completion> done;
    uint32_t refs;
    {
        std::lock_guard<std::recursive_mutex> g(g_mutex);
        refs = s->refs ? --s->refs : 0;
        if (refs == 0) {
            for (auto &p : s->queue) finish_packet(p, s, PACKET_FLUSHED, done);
            s->queue.clear();
            g_streams.erase(std::remove(g_streams.begin(), g_streams.end(), s), g_streams.end());
            s->magic = 0;
        }
    }
    deliver(done);
    return refs;
}

HRESULT __stdcall Stream_GetInfo(Stream *s, uint32_t *info)
{
    info[0] = 0x1 | 0x20; // XMO_STREAMF_FIXED_SAMPLE_SIZE | XMO_STREAMF_INPUT_ASYNC
    info[1] = s->fmt.nBlockAlign ? s->fmt.nBlockAlign : 4;
    info[2] = 0;
    info[3] = 0x4000;
    return S_OK;
}

HRESULT __stdcall Stream_GetStatus(Stream *s, uint32_t *pdwStatus)
{
    std::lock_guard<std::recursive_mutex> g(g_mutex);
    uint32_t st = 0;
    if (s->queue.size() < (s->desc.dwMaxAttachedPackets ? s->desc.dwMaxAttachedPackets : 1)) st |= DSSTREAMSTATUS_READY;
    if (s->paused) st |= DSSTREAMSTATUS_PAUSED;
    else if (!s->queue.empty()) st |= DSSTREAMSTATUS_PLAYING;
    else st |= DSSTREAMSTATUS_STARVED;
    *pdwStatus = st;
    return S_OK;
}

HRESULT __stdcall Stream_Process(Stream *s, MediaPacket *pIn, MediaPacket *pOut)
{
    if (!pIn) return E_POINTER;
    std::lock_guard<std::recursive_mutex> g(g_mutex);
    if (pIn->pdwStatus) *pIn->pdwStatus = PACKET_PENDING;
    if (pIn->pdwCompletedSize) *pIn->pdwCompletedSize = 0;
    s->queue.push_back(*pIn);
    return S_OK;
}

HRESULT __stdcall Stream_Discontinuity(Stream *s) { return S_OK; }

HRESULT __stdcall Stream_Flush(Stream *s)
{
    std::vector<Completion> done;
    {
        std::lock_guard<std::recursive_mutex> g(g_mutex);
        for (auto &p : s->queue) finish_packet(p, s, PACKET_FLUSHED, done);
        s->queue.clear();
        s->consumed = 0;
        s->primed = false;
        s->dec_ptr = nullptr;
    }
    deliver(done);
    return S_OK;
}

const void *const kStreamVtbl[] = {(void *)&Stream_AddRef, (void *)&Stream_Release, (void *)&Stream_GetInfo,
                                   (void *)&Stream_GetStatus, (void *)&Stream_Process, (void *)&Stream_Discontinuity,
                                   (void *)&Stream_Flush};

// Objects the game only calls through C functions still get a vtable whose entries log if used.
void __stdcall vtbl_unexpected() { logf("[dsound] unexpected virtual call on a DirectSound/buffer object"); }
const void *const kPlainVtbl[] = {(void *)&vtbl_unexpected, (void *)&vtbl_unexpected, (void *)&vtbl_unexpected,
                                  (void *)&vtbl_unexpected, (void *)&vtbl_unexpected, (void *)&vtbl_unexpected,
                                  (void *)&vtbl_unexpected, (void *)&vtbl_unexpected};

Buffer *as_buffer(void *p)
{
    auto *b = (Buffer *)p;
    return (b && b->magic == OBJ_MAGIC && b->kind == 2) ? b : nullptr;
}

Stream *as_stream(void *p)
{
    auto *s = (Stream *)p;
    return (s && s->magic == OBJ_MAGIC && s->kind == 3) ? s : nullptr;
}

Voice *as_voice(void *p)
{
    auto *v = (Voice *)p;
    return (v && v->magic == OBJ_MAGIC && (v->kind == 2 || v->kind == 3)) ? v : nullptr;
}

} // namespace

// ---- DirectSound ----

#define STRACE DTRACE

HRESULT __stdcall DirectSoundCreate(void *pguid, Object **ppDS, void *pUnk)
{
    STRACE("%p, %p, %p", pguid, ppDS, pUnk);
    start_worker();
    if (!g_dsound.magic) g_dsound = Object{kPlainVtbl, OBJ_MAGIC, 0, 1};
    g_dsound.refs++;
    *ppDS = &g_dsound;
    return S_OK;
}

HRESULT __stdcall DirectSoundCreateStream(StreamDesc *pdssd, Stream **ppStream)
{
    start_worker();
    auto *s = new Stream();
    init_voice(*s, 3, pdssd->lpwfxFormat, pdssd->dwFlags, pdssd->lpMixBins);
    s->vtbl = kStreamVtbl;
    s->desc = *pdssd;
    s->paused = (pdssd->dwFlags & 0x10000) != 0; // DSSTREAMFLAGS_PAUSED
    {
        std::lock_guard<std::recursive_mutex> g(g_mutex);
        g_streams.push_back(s);
    }
    *ppStream = s;
    logf("[dsound] DirectSoundCreateStream(flags 0x%x, %u packets, %s, %u mix bins, callback %s) -> %p", pdssd->dwFlags,
         pdssd->dwMaxAttachedPackets, fmt_str(pdssd->lpwfxFormat).c_str(), s->nbins,
         sym_name((uint32_t)(uintptr_t)pdssd->lpfnCallback).c_str(), s);
    return S_OK;
}

HRESULT __stdcall IDirectSound_CreateSoundBuffer(Object *pThis, BufferDesc *pdsbd, Buffer **ppBuffer, void *pUnk)
{
    auto *b = new Buffer();
    init_voice(*b, 2, pdsbd->lpwfxFormat, pdsbd->dwFlags, pdsbd->lpMixBins);
    b->vtbl = kPlainVtbl;
    b->bytes = pdsbd->dwBufferBytes;
    if (b->bytes) { // the library allocates the buffer memory unless the game supplies it later
        b->data = (uint8_t *)xk::MmAllocateContiguousMemoryEx(b->bytes, 0, 0xFFFFFFFF, 0, PAGE_READWRITE);
        memset(b->data, 0, b->bytes);
    }
    {
        std::lock_guard<std::recursive_mutex> g(g_mutex);
        g_buffers.push_back(b);
    }
    *ppBuffer = b;
    static std::atomic<int> n{0};
    if (++n <= 40 || g_trace_d3d)
        logf("[dsound] CreateSoundBuffer(flags 0x%x, %u bytes, %s, %u mix bins) -> %p", pdsbd->dwFlags, pdsbd->dwBufferBytes,
             fmt_str(pdsbd->lpwfxFormat).c_str(), b->nbins, b);
    return S_OK;
}

uint32_t __stdcall IDirectSound_Release(Object *pThis)
{
    STRACE("%p", pThis);
    return pThis->refs ? --pThis->refs : 0;
}

void __stdcall DirectSoundUseLightHRTF() { STRACE(""); }
void __stdcall DirectSoundDoWork() { STRACE(""); }

HRESULT __stdcall IDirectSound_GetSpeakerConfig(Object *pThis, uint32_t *pdw)
{
    STRACE("%p", pThis);
    *pdw = 2; // stereo
    return S_OK;
}

// The DSP effects image the game downloads. The descriptor it gets back is read for effect state
// pointers, so hand out a zeroed descriptor whose maps all point at scratch memory.
HRESULT __stdcall IDirectSound_DownloadEffectsImage(Object *pThis, void *pvImage, uint32_t size, void *pImageLoc, void **ppDesc)
{
    STRACE("%p, %p, %u, %p, %p", pThis, pvImage, size, pImageLoc, ppDesc);
    static uint8_t *desc;
    if (!desc) {
        constexpr uint32_t maps = 32;
        desc = (uint8_t *)xk::ExAllocatePoolWithTag(8 + maps * 32, 'cseD');
        memset(desc, 0, 8 + maps * 32);
        uint8_t *scratch = (uint8_t *)xk::ExAllocatePoolWithTag(0x10000, 'crcS');
        memset(scratch, 0, 0x10000);
        *(uint32_t *)desc = maps;
        for (uint32_t i = 0; i < maps; i++) {
            uint32_t *m = (uint32_t *)(desc + 8 + i * 32);
            m[0] = m[2] = m[4] = m[6] = (uint32_t)(uintptr_t)scratch;
            m[1] = m[3] = m[5] = m[7] = 0x1000;
        }
    }
    if (ppDesc) *ppDesc = desc;
    return S_OK;
}

HRESULT __stdcall IDirectSound_GetEffectData(Object *pThis, uint32_t idx, uint32_t off, void *pv, uint32_t size)
{
    STRACE("%p, %u, %u, %p, %u", pThis, idx, off, pv, size);
    memset(pv, 0, size);
    return S_OK;
}

HRESULT __stdcall IDirectSound_SetEffectData(Object *pThis, uint32_t idx, uint32_t off, void *pv, uint32_t size, uint32_t apply)
{
    STRACE("%p, %u, %u, %p, %u, %u", pThis, idx, off, pv, size, apply);
    static int logged;
    if (logged++ < 200) {
        char words[200] = "";
        size_t n = 0;
        for (uint32_t i = 0; i < size / 4 && i < 12; i++)
            n += snprintf(words + n, sizeof words - n, " %06x", ((uint32_t *)pv)[i]);
        logf("[dsound] SetEffectData(effect %u, offset %u, %u bytes):%s", idx, off, size, words);
    }
    return S_OK;
}

// DSI3DL2LISTENER (the I3DL2 reverb's room)
struct I3dl2Listener {
    int32_t lRoom, lRoomHF;
    float flRoomRolloffFactor, flDecayTime, flDecayHFRatio;
    int32_t lReflections;
    float flReflectionsDelay;
    int32_t lReverb;
    float flReverbDelay, flDiffusion, flDensity, flHFReference;
};

HRESULT __stdcall IDirectSound_SetI3DL2Listener(Object *pThis, const I3dl2Listener *p, uint32_t apply)
{
    STRACE("%p, %p, %u", pThis, p, apply);
    static int logged;
    if (p && logged++ < 100)
        logf("[dsound] SetI3DL2Listener: room %d hf %d rolloff %.2f decay %.2f s hf ratio %.2f, reflections %d delay %.3f, "
             "reverb %d delay %.3f, diffusion %.0f density %.0f hf ref %.0f",
             p->lRoom, p->lRoomHF, p->flRoomRolloffFactor, p->flDecayTime, p->flDecayHFRatio, p->lReflections,
             p->flReflectionsDelay, p->lReverb, p->flReverbDelay, p->flDiffusion, p->flDensity, p->flHFReference);
    if (p) {
        reverb::Params r;
        r.room = p->lRoom;
        r.room_hf = p->lRoomHF;
        r.room_rolloff = p->flRoomRolloffFactor;
        r.decay_time = p->flDecayTime;
        r.decay_hf_ratio = p->flDecayHFRatio;
        r.reflections = p->lReflections;
        r.reflections_delay = p->flReflectionsDelay;
        r.reverb = p->lReverb;
        r.reverb_delay = p->flReverbDelay;
        r.diffusion = p->flDiffusion;
        r.density = p->flDensity;
        r.hf_reference = p->flHFReference;
        reverb::set_params(r);
    }
    return S_OK;
}

HRESULT __stdcall IDirectSound_Simple1(Object *pThis) { STRACE("%p", pThis); return S_OK; }
HRESULT __stdcall IDirectSound_Simple2(Object *pThis, uint32_t a) { STRACE("%p, 0x%x", pThis, a); return S_OK; }
HRESULT __stdcall IDirectSound_Simple3(Object *pThis, uint32_t a, uint32_t b) { STRACE("%p, 0x%x, 0x%x", pThis, a, b); return S_OK; }
HRESULT __stdcall IDirectSound_Simple5(Object *pThis, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    STRACE("%p, 0x%x, 0x%x, 0x%x, 0x%x", pThis, a, b, c, d);
    return S_OK;
}

// listener
HRESULT __stdcall IDirectSound_SetPosition(Object *pThis, float x, float y, float z, uint32_t apply)
{
    STRACE("%p, %g %g %g", pThis, x, y, z);
    std::lock_guard<std::recursive_mutex> g(g_mutex);
    g_listener.pos = {x, y, z};
    return S_OK;
}

HRESULT __stdcall IDirectSound_SetOrientation(Object *pThis, float xf, float yf, float zf, float xt, float yt, float zt, uint32_t apply)
{
    STRACE("%p, front %g %g %g, top %g %g %g", pThis, xf, yf, zf, xt, yt, zt);
    std::lock_guard<std::recursive_mutex> g(g_mutex);
    g_listener.front = {xf, yf, zf};
    g_listener.top = {xt, yt, zt};
    return S_OK;
}

HRESULT __stdcall IDirectSound_SetRolloffFactor(Object *pThis, float f, uint32_t apply)
{
    STRACE("%p, %g", pThis, f);
    g_listener.rolloff = f;
    return S_OK;
}

HRESULT __stdcall IDirectSound_SetDistanceFactor(Object *pThis, float f, uint32_t apply)
{
    STRACE("%p, %g", pThis, f);
    g_listener.distance = f;
    return S_OK;
}

HRESULT __stdcall IDirectSound_GetOutputLevels(Object *pThis, uint32_t *pLevels, BOOL reset)
{
    STRACE("%p, %p, %d", pThis, pLevels, reset);
    memset(pLevels, 0, 64);
    return S_OK;
}

// ---- buffers ----

uint32_t __stdcall IDirectSoundBuffer_Release(void *p)
{
    STRACE("%p", p);
    Buffer *b = as_buffer(p);
    if (!b) return 0;
    std::lock_guard<std::recursive_mutex> g(g_mutex);
    if (b->refs && --b->refs == 0) {
        g_buffers.erase(std::remove(g_buffers.begin(), g_buffers.end(), b), g_buffers.end());
        b->magic = 0;
        return 0;
    }
    return b->refs;
}

// shared by buffers and streams
HRESULT __stdcall Voice_SetVolume(void *p, int32_t volume)
{
    STRACE("%p, %d", p, volume);
    if (Voice *v = as_voice(p)) v->volume = volume;
    return S_OK;
}

HRESULT __stdcall Voice_SetHeadroom(void *p, uint32_t headroom)
{
    STRACE("%p, %u", p, headroom);
    if (Voice *v = as_voice(p)) v->headroom = headroom;
    return S_OK;
}

HRESULT __stdcall Voice_SetMixBins(void *p, const MixBins *mb)
{
    STRACE("%p, %p", p, mb);
    std::lock_guard<std::recursive_mutex> g(g_mutex);
    if (Voice *v = as_voice(p)) set_bins(*v, mb, false);
    return S_OK;
}

HRESULT __stdcall Voice_SetMixBinVolumes(void *p, const MixBins *mb)
{
    STRACE("%p, %p", p, mb);
    std::lock_guard<std::recursive_mutex> g(g_mutex);
    if (Voice *v = as_voice(p)) set_bins(*v, mb, true);
    return S_OK;
}

HRESULT __stdcall IDirectSoundBuffer_SetAllParameters(void *p, const Ds3dBuffer *params, uint32_t apply)
{
    STRACE("%p, %p, %u", p, params, apply);
    std::lock_guard<std::recursive_mutex> g(g_mutex);
    if (Voice *v = as_voice(p)) {
        uint32_t size = v->ds3d.dwSize;
        memcpy((uint8_t *)&v->ds3d + 4, (const uint8_t *)params + 4, sizeof(Ds3dBuffer) - 4);
        v->ds3d.dwSize = size;
    }
    return S_OK;
}

HRESULT __stdcall IDirectSoundBuffer_Set1(void *p, uint32_t v) { STRACE("%p, 0x%x", p, v); return S_OK; }
HRESULT __stdcall IDirectSoundBuffer_Set2(void *p, uint32_t a, uint32_t b) { STRACE("%p, 0x%x, 0x%x", p, a, b); return S_OK; }
HRESULT __stdcall IDirectSoundBuffer_Set3(void *p, uint32_t a, uint32_t b, uint32_t c) { STRACE("%p, 0x%x, 0x%x, 0x%x", p, a, b, c); return S_OK; }

HRESULT __stdcall IDirectSoundBuffer_SetFrequency(void *p, uint32_t freq)
{
    STRACE("%p, %u", p, freq);
    if (Voice *v = as_voice(p)) v->frequency = freq;
    return S_OK;
}

HRESULT __stdcall IDirectSoundBuffer_SetFormat(void *p, const WaveFormatEx *fmt)
{
    STRACE("%p, %s", p, fmt_str(fmt).c_str());
    std::lock_guard<std::recursive_mutex> g(g_mutex);
    if (Buffer *b = as_buffer(p)) {
        if (fmt) b->fmt = *fmt;
        b->primed = false;
        b->dec_ptr = nullptr;
    }
    return S_OK;
}

HRESULT __stdcall IDirectSoundBuffer_SetBufferData(void *p, void *pv, uint32_t bytes)
{
    STRACE("%p, %p, %u", p, pv, bytes);
    if (Buffer *b = as_buffer(p)) {
        std::lock_guard<std::recursive_mutex> g(g_mutex);
        b->data = (uint8_t *)pv;
        b->bytes = bytes;
        b->cur = 0;
        b->primed = false;
        b->dec_ptr = nullptr;
        b->loop_start = b->loop_len = b->play_start = b->play_len = 0;
    }
    return S_OK;
}

HRESULT __stdcall IDirectSoundBuffer_SetLoopRegion(void *p, uint32_t start, uint32_t len)
{
    STRACE("%p, %u, %u", p, start, len);
    std::lock_guard<std::recursive_mutex> g(g_mutex);
    if (Buffer *b = as_buffer(p)) {
        b->loop_start = start;
        b->loop_len = len;
    }
    return S_OK;
}

HRESULT __stdcall IDirectSoundBuffer_SetPlayRegion(void *p, uint32_t start, uint32_t len)
{
    STRACE("%p, %u, %u", p, start, len);
    std::lock_guard<std::recursive_mutex> g(g_mutex);
    if (Buffer *b = as_buffer(p)) {
        b->play_start = start;
        b->play_len = len;
    }
    return S_OK;
}

HRESULT __stdcall IDirectSoundBuffer_Play(void *p, uint32_t r1, uint32_t r2, uint32_t flags)
{
    STRACE("%p, 0x%x", p, flags);
    if (Buffer *b = as_buffer(p)) {
        std::lock_guard<std::recursive_mutex> g(g_mutex);
        if ((flags & DSBPLAY_FROMSTART) || (!b->playing && b->cur >= region_frames(*b))) {
            b->cur = 0;
            b->primed = false;
        }
        b->looping = (flags & DSBPLAY_LOOPING) != 0;
        b->playing = true;
        static int logged;
        if (logged++ < 60)
            logf("[dsound] play %p: %s, %s, data %p+%u, play %u+%u, loop %u+%u, cur %u%s", b, fmt_str(&b->fmt).c_str(),
                 voice_str(*b).c_str(), b->data, b->bytes, b->play_start, b->play_len, b->loop_start, b->loop_len, b->cur,
                 b->looping ? ", looping" : "");
    }
    return S_OK;
}

HRESULT __stdcall IDirectSoundBuffer_StopEx(void *p, uint32_t tsLo, uint32_t tsHi, uint32_t flags)
{
    STRACE("%p, 0x%x", p, flags);
    std::lock_guard<std::recursive_mutex> g(g_mutex);
    if (Buffer *b = as_buffer(p)) b->playing = false;
    return S_OK;
}

HRESULT __stdcall IDirectSoundBuffer_GetStatus(void *p, uint32_t *pdw)
{
    STRACE("%p", p);
    uint32_t st = 0;
    if (Buffer *b = as_buffer(p)) {
        if (b->playing) st |= DSBSTATUS_PLAYING;
        if (b->playing && b->looping) st |= DSBSTATUS_LOOPING;
    }
    *pdw = st;
    return S_OK;
}

HRESULT __stdcall IDirectSoundBuffer_GetCurrentPosition(void *p, uint32_t *pPlay, uint32_t *pWrite)
{
    STRACE("%p", p);
    uint32_t pos = 0;
    if (Buffer *b = as_buffer(p)) {
        std::lock_guard<std::recursive_mutex> g(g_mutex);
        pos = frames_to_bytes(b->fmt, b->cur);
    }
    if (pPlay) *pPlay = pos;
    if (pWrite) *pWrite = pos;
    return S_OK;
}

HRESULT __stdcall IDirectSoundBuffer_SetCurrentPosition(void *p, uint32_t pos)
{
    STRACE("%p, %u", p, pos);
    std::lock_guard<std::recursive_mutex> g(g_mutex);
    if (Buffer *b = as_buffer(p)) {
        b->cur = bytes_to_frames(b->fmt, pos);
        b->primed = false;
    }
    return S_OK;
}

// ---- streams (C wrappers) ----

HRESULT __stdcall IDirectSoundStream_Pause(void *p, uint32_t dwPause)
{
    STRACE("%p, %u", p, dwPause);
    std::lock_guard<std::recursive_mutex> g(g_mutex);
    if (Stream *s = as_stream(p)) s->paused = dwPause == 1;
    return S_OK;
}

HRESULT __stdcall IDirectSoundStream_FlushEx(void *p, uint32_t tsLo, uint32_t tsHi, uint32_t flags)
{
    STRACE("%p, 0x%x", p, flags);
    if (Stream *s = as_stream(p)) return Stream_Flush(s);
    return S_OK;
}

// ---- table ----

#define H(addr, fn, params, ret) {addr, #fn, (void *)&fn, params, ret}
#define HN(addr, name, fn, params, ret) {addr, name, (void *)&fn, params, ret}

static const xd3d::Hle kTable[] = {
    H(0x0021b4f6, DirectSoundCreate, "s s s", true),
    H(0x0021b53d, DirectSoundCreateStream, "s s", true),
    H(0x0021b1ec, IDirectSound_CreateSoundBuffer, "s s s s", true),
    H(0x0021828a, IDirectSound_Release, "s", true),
    H(0x002182b6, DirectSoundUseLightHRTF, "", false),
    H(0x00219877, DirectSoundDoWork, "", false),
    H(0x002195e3, IDirectSound_GetSpeakerConfig, "s s", true),
    H(0x002195ff, IDirectSound_DownloadEffectsImage, "s s s s s", true),
    H(0x00219626, IDirectSound_GetEffectData, "s s s s s", true),
    H(0x0021964d, IDirectSound_SetEffectData, "s s s s s s", true),
    HN(0x00219677, "IDirectSound_CommitEffectData", IDirectSound_Simple1, "s", true),
    HN(0x0021968f, "IDirectSound_EnableHeadphones", IDirectSound_Simple2, "s s", true),
    HN(0x002196ab, "IDirectSound_SetMixBinHeadroom", IDirectSound_Simple3, "s s s", true),
    H(0x002196cb, IDirectSound_GetOutputLevels, "s s s", true),
    HN(0x002196eb, "IDirectSound_SynchPlayback", IDirectSound_Simple1, "s", true),
    HN(0x0021acf4, "IDirectSound_CommitDeferredSettings", IDirectSound_Simple1, "s", true),
    H(0x0021b210, IDirectSound_SetDistanceFactor, "s s s", true),
    HN(0x0021b234, "IDirectSound_SetDopplerFactor", IDirectSound_Simple3, "s s s", true),
    H(0x0021b258, IDirectSound_SetOrientation, "s s s s s s s s", true),
    H(0x0021b2a2, IDirectSound_SetPosition, "s s s s s", true),
    H(0x0021b2d7, IDirectSound_SetRolloffFactor, "s s s", true),
    HN(0x0021b2fb, "IDirectSound_SetVelocity", IDirectSound_Simple5, "s s s s s", true),
    H(0x0021b330, IDirectSound_SetI3DL2Listener, "s s s", true),
    H(0x002182a0, IDirectSoundBuffer_Release, "s", true),
    HN(0x00219703, "IDirectSoundBuffer_SetVolume", Voice_SetVolume, "s s", true),
    HN(0x0021971f, "IDirectSoundBuffer_SetFilter", IDirectSoundBuffer_Set1, "s s", true),
    HN(0x0021973b, "IDirectSoundBuffer_SetHeadroom", Voice_SetHeadroom, "s s", true),
    HN(0x00219757, "IDirectSoundBuffer_SetMixBinVolumes", Voice_SetMixBinVolumes, "s s", true),
    H(0x00219773, IDirectSoundBuffer_Play, "s s s s", true),
    H(0x002197af, IDirectSoundBuffer_StopEx, "s s s s", true),
    H(0x002197d3, IDirectSoundBuffer_SetLoopRegion, "s s s", true),
    H(0x002197f3, IDirectSoundBuffer_GetStatus, "s s", true),
    H(0x0021980f, IDirectSoundBuffer_GetCurrentPosition, "s s s", true),
    H(0x0021982f, IDirectSoundBuffer_SetCurrentPosition, "s s", true),
    H(0x0021a137, IDirectSoundBuffer_SetFrequency, "s s", true),
    HN(0x0021a153, "IDirectSoundBuffer_SetOutputBuffer", IDirectSoundBuffer_Set1, "s s", true),
    HN(0x0021a16f, "IDirectSoundBuffer_SetMixBins", Voice_SetMixBins, "s s", true),
    H(0x0021a18b, IDirectSoundBuffer_SetAllParameters, "s s s", true),
    HN(0x0021a1ab, "IDirectSoundBuffer_SetRolloffCurve", IDirectSoundBuffer_Set3, "s s s s", true),
    HN(0x0021a1cf, "IDirectSoundBuffer_SetI3DL2Source", IDirectSoundBuffer_Set2, "s s s", true),
    H(0x0021a1ef, IDirectSoundBuffer_SetPlayRegion, "s s s", true),
    H(0x0021a7e1, IDirectSoundBuffer_SetFormat, "s s", true),
    H(0x0021a7fd, IDirectSoundBuffer_SetBufferData, "s s s", true),
    HN(0x0021984b, "IDirectSoundStream_SetVolume", Voice_SetVolume, "s s", true),
    HN(0x00219850, "IDirectSoundStream_SetHeadroom", Voice_SetHeadroom, "s s", true),
    HN(0x00219855, "IDirectSoundStream_SetMixBinVolumes", Voice_SetMixBinVolumes, "s s", true),
    H(0x0021985a, IDirectSoundStream_Pause, "s s", true),
    H(0x0021985f, IDirectSoundStream_FlushEx, "s s s s", true),
    HN(0x0021a20f, "IDirectSoundStream_SetMixBins", Voice_SetMixBins, "s s", true),
};

} // namespace xdsound

namespace xd3d {
const Hle *hle_dsound_table(size_t *count)
{
    *count = sizeof xdsound::kTable / sizeof xdsound::kTable[0];
    return xdsound::kTable;
}
} // namespace xd3d

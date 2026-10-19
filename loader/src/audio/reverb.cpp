// I3DL2-style reverb: input low-pass (RoomHF) and level (Room), a pre-delay line with early-reflection
// taps (Reflections, ReflectionsDelay), then the late reverb (Reverb, ReverbDelay) through two allpass
// diffusers into an 8-line feedback delay network with a Hadamard mixing matrix. Each line's gain gives
// the DecayTime and a one-pole low-pass in the loop gives DecayTime x DecayHFRatio at HFReference.
#include "audio/reverb.h"

#include "audio/output.h"

#include <xmmintrin.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

namespace reverb {
namespace {

constexpr float FS = (float)audio_out::RATE;
constexpr uint32_t PRE = 1 << 15; // pre-delay ring (0.68 s at 48 kHz): reflections delay + reverb delay
constexpr int LINES = 8;
constexpr uint32_t kLen[LINES] = {1433, 1601, 1867, 2053, 2251, 2399, 2617, 2801}; // 30-58 ms, mutually prime
constexpr uint32_t kAp[2] = {225, 341};                                           // diffuser allpasses
constexpr float kTapMs[6] = {0.0f, 3.1f, 7.3f, 11.9f, 17.1f, 23.7f};             // early reflections
constexpr float kTapGain[6] = {1.0f, 0.85f, 0.72f, 0.6f, 0.5f, 0.42f};

struct OnePole { // y = b * x + a * y
    float a = 0, b = 1, y = 0;
    float run(float x) { return y = b * x + a * y; }
};

// Pole for a one-pole low-pass with unity DC gain whose gain at `freq` is r (0 < r <= 1).
float lowpass_pole(float r, float freq)
{
    if (r >= 0.999f) return 0.0f;
    r = std::max(r, 0.001f);
    float c = cosf(2.0f * 3.14159265f * std::min(freq, FS * 0.45f) / FS), r2 = r * r;
    float p = 1.0f - r2 * c, q = 1.0f - r2;
    return (p - sqrtf(std::max(p * p - q * q, 0.0f))) / q;
}

float mb(int32_t millibels) { return powf(10.0f, millibels / 2000.0f); }

struct State {
    float pre[PRE] = {};
    uint32_t pos = 0;
    OnePole input_lp;
    float room = 0, refl = 0, late = 0;
    uint32_t tap[6] = {}, late_tap = 0;
    float ap_buf[2][512] = {};
    uint32_t ap_pos[2] = {};
    float ap_g = 0.5f;
    float line[LINES][2816] = {};
    uint32_t line_pos[LINES] = {};
    float line_gain[LINES] = {};
    OnePole damp[LINES];
};

State g_s;
float g_gain = 1.0f;
Params g_pending;
bool g_dirty = true;
std::mutex g_lock;

void apply(const Params &p)
{
    State &s = g_s;
    s.room = mb(std::clamp(p.room, -10000, 0)) * g_gain;
    float hf = mb(std::clamp(p.room_hf, -10000, 0));
    s.input_lp.a = lowpass_pole(hf, p.hf_reference);
    s.input_lp.b = 1.0f - s.input_lp.a;
    float refl_delay = std::clamp(p.reflections_delay, 0.0f, 0.3f), rev_delay = std::clamp(p.reverb_delay, 0.0f, 0.1f);
    float norm = 0;
    for (float g : kTapGain) norm += g * g;
    s.refl = mb(std::clamp(p.reflections, -10000, 1000)) / sqrtf(norm);
    for (int i = 0; i < 6; i++) s.tap[i] = std::min<uint32_t>((uint32_t)((refl_delay + kTapMs[i] / 1000.0f) * FS), PRE - 1);
    s.late_tap = std::min<uint32_t>((uint32_t)((refl_delay + rev_delay) * FS), PRE - 1);
    s.late = mb(std::clamp(p.reverb, -10000, 2000)) / sqrtf((float)LINES);
    s.ap_g = 0.7f * std::clamp(p.diffusion, 0.0f, 100.0f) / 100.0f;
    float t60 = std::clamp(p.decay_time, 0.1f, 20.0f), ratio = std::clamp(p.decay_hf_ratio, 0.1f, 1.0f);
    for (int i = 0; i < LINES; i++) {
        float g = powf(10.0f, -3.0f * kLen[i] / (t60 * FS)), ghf = powf(10.0f, -3.0f * kLen[i] / (t60 * ratio * FS));
        s.line_gain[i] = g;
        s.damp[i].a = lowpass_pole(ghf / g, p.hf_reference);
        s.damp[i].b = 1.0f - s.damp[i].a;
    }
}

} // namespace

void set_gain(float k)
{
    std::lock_guard<std::mutex> g(g_lock);
    g_gain = k;
    g_dirty = true;
}

void set_params(const Params &p)
{
    std::lock_guard<std::mutex> g(g_lock);
    g_pending = p;
    g_dirty = true;
}

void process(const float *in, float *out, uint32_t n)
{
    static thread_local bool ftz = [] { // denormals in the decaying tail would cost a lot of CPU
        _mm_setcsr(_mm_getcsr() | 0x8040);
        return true;
    }();
    (void)ftz;
    {
        std::lock_guard<std::mutex> g(g_lock);
        if (g_dirty) {
            apply(g_pending);
            g_dirty = false;
        }
    }
    State &s = g_s;
    for (uint32_t i = 0; i < n; i++) {
        s.pre[s.pos] = s.input_lp.run(in[i] * s.room);
        auto at = [&](uint32_t d) { return s.pre[(s.pos - d) & (PRE - 1)]; };
        float l = 0, r = 0;
        for (int t = 0; t < 6; t++) (t & 1 ? r : l) += at(s.tap[t]) * kTapGain[t] * s.refl;
        float x = at(s.late_tap) * s.late;
        for (int k = 0; k < 2; k++) { // Schroeder allpass
            float &d = s.ap_buf[k][s.ap_pos[k]];
            float y = d - s.ap_g * x;
            d = x + s.ap_g * y;
            x = y;
            if (++s.ap_pos[k] >= kAp[k]) s.ap_pos[k] = 0;
        }
        float v[LINES];
        for (int j = 0; j < LINES; j++) v[j] = s.damp[j].run(s.line[j][s.line_pos[j]] * s.line_gain[j]);
        l += 0.5f * (v[0] - v[2] + v[4] - v[6]);
        r += 0.5f * (v[1] - v[3] + v[5] - v[7]);
        for (int h = 1; h < LINES; h <<= 1) // fast Hadamard transform, then 1/sqrt(8)
            for (int j = 0; j < LINES; j += h << 1)
                for (int k = j; k < j + h; k++) {
                    float a = v[k], b = v[k + h];
                    v[k] = a + b;
                    v[k + h] = a - b;
                }
        for (int j = 0; j < LINES; j++) {
            s.line[j][s.line_pos[j]] = v[j] * 0.35355339f + (j & 1 ? -x : x);
            if (++s.line_pos[j] >= kLen[j]) s.line_pos[j] = 0;
        }
        if (!(fabsf(l) + fabsf(r) < 1e4f)) { // NaN/inf or runaway: never let the reverb silence the mix
            memset(s.pre, 0, sizeof s.pre); // clear the delay memory in place (the state is too big for a stack copy)
            memset(s.ap_buf, 0, sizeof s.ap_buf);
            memset(s.line, 0, sizeof s.line);
            for (OnePole &d : s.damp) d.y = 0;
            s.input_lp.y = 0;
            l = r = 0;
        }
        out[2 * i] += l;
        out[2 * i + 1] += r;
        s.pos = (s.pos + 1) & (PRE - 1);
    }
}

} // namespace reverb

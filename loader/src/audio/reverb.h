// Room reverb for the I3DL2 send (BLACK's DSP image feeds mix bin 11 = FXSEND_0 into its I3DL2 reverb,
// effect 16, whose outputs go to the four corner speakers). A feedback-delay-network approximation of the
// I3DL2 listener model, not the DSP program itself.
#pragma once

#include <cstdint>

namespace reverb {

// DSI3DL2LISTENER fields (levels in millibels, times in seconds). BLACK never calls SetI3DL2Listener: its
// room is baked into the DSP image's state for effect 16 (DSP-format delays/coefficients, not decoded; the
// delays suggest a medium space with strong high-frequency damping), and the game sets how wet each sound
// is with the voice's send level to bin 11. These defaults approximate that by ear.
struct Params {
    int32_t room = -1000, room_hf = -600;
    float room_rolloff = 0.0f, decay_time = 1.5f, decay_hf_ratio = 0.6f;
    int32_t reflections = -1500;
    float reflections_delay = 0.012f;
    int32_t reverb = -300;
    float reverb_delay = 0.03f, diffusion = 100.0f, density = 100.0f, hf_reference = 5000.0f;
};

void set_params(const Params &p);
void set_gain(float k); // overall level of the reverb (--reverb-gain)
// in: mono send (n frames); out: interleaved stereo, the reverb is added to it.
void process(const float *in, float *out, uint32_t n);

} // namespace reverb

// this is based on Mutable Instruments

#include "MiSuperSawOsc.hpp"
#include <cstring>
#include <cmath>
#include "esp_system.h"

using namespace CTAG::SP;

void MiSuperSawOsc::Init() {
    memset(&phase, 0, sizeof(phase));
    phase_ = 0;
    strike_ = true;
}

void MiSuperSawOsc::SetPitch(const int16_t &pitch) {
    // Smooth HF noise when the pitch CV is noisy.
    if (pitch_ > (90 << 7) && pitch > (90 << 7)) {
        pitch_ = (static_cast<int32_t>(pitch_) + pitch) >> 1;
    } else {
        pitch_ = pitch;
    }
}

void MiSuperSawOsc::Render(
        int16_t *buffer,
        size_t size) {
    int32_t detune = detune_ + 1024;
    detune = (detune * detune) >> 9;
    uint32_t increments[7];
    for (int16_t i = 0; i < 7; ++i) {
        int32_t saw_detune = detune * (i - 3);
        int32_t detune_integral = saw_detune >> 16;
        int32_t detune_fractional = saw_detune & 0xffff;
        int32_t increment_a = ComputePhaseIncrement(pitch_ + detune_integral);
        int32_t increment_b = ComputePhaseIncrement(pitch_ + detune_integral + 1);
        increments[i] = increment_a + \
        (((increment_b - increment_a) * detune_fractional) >> 16);
    }
    if (strike_) {
        for (size_t i = 0; i < 6; ++i) {
            phase[i] = Random::GetWord();
        }
        strike_ = false;
    }

    while (size--) {
        phase_ += increments[0];
        phase[0] += increments[1];
        phase[1] += increments[2];
        phase[2] += increments[3];
        phase[3] += increments[4];
        phase[4] += increments[5];
        phase[5] += increments[6];

        int32_t sample = -28672;
        sample += phase_ >> 19;
        sample += phase[0] >> 19;
        sample += phase[1] >> 19;
        sample += phase[2] >> 19;
        sample += phase[3] >> 19;
        sample += phase[4] >> 19;
        sample += phase[5] >> 19;
        sample = Interpolate88(ws_moderate_overdrive, sample + 32768);
        sample >>= damp_;
        *buffer++ += sample;
    }
}

void MiSuperSawOsc::RenderAccum(int32_t *left, int32_t *right, size_t size,
                                int32_t left_gain_q12, int32_t right_gain_q12) {
    int32_t detune = detune_ + 1024;
    detune = (detune * detune) >> 9;
    uint32_t increments[7];
    for (int16_t i = 0; i < 7; ++i) {
        int32_t saw_detune = detune * (i - 3);
        int32_t detune_integral = saw_detune >> 16;
        int32_t detune_fractional = saw_detune & 0xffff;
        int32_t increment_a = ComputePhaseIncrement(pitch_ + detune_integral);
        int32_t increment_b = ComputePhaseIncrement(pitch_ + detune_integral + 1);
        increments[i] = increment_a + \
        (((increment_b - increment_a) * detune_fractional) >> 16);
    }
    if (strike_) {
        for (size_t i = 0; i < 6; ++i) {
            phase[i] = Random::GetWord();
        }
        strike_ = false;
    }

    while (size--) {
        phase_ += increments[0];
        phase[0] += increments[1];
        phase[1] += increments[2];
        phase[2] += increments[3];
        phase[3] += increments[4];
        phase[4] += increments[5];
        phase[5] += increments[6];

        int32_t sample = -28672;
        sample += phase_ >> 19;
        sample += phase[0] >> 19;
        sample += phase[1] >> 19;
        sample += phase[2] >> 19;
        sample += phase[3] >> 19;
        sample += phase[4] >> 19;
        sample += phase[5] >> 19;
        sample = Interpolate88(ws_moderate_overdrive, sample + 32768);
        sample >>= damp_;
        *left++ += (sample * left_gain_q12) >> 12;
        *right++ += (sample * right_gain_q12) >> 12;
    }
}

void MiSuperSawOsc::RenderStereoAccum(int32_t *left, int32_t *right, size_t size, float spread,
                                      int32_t left_gain_q12, int32_t right_gain_q12) {
    int32_t detune = detune_ + 1024;
    detune = (detune * detune) >> 9;
    uint32_t increments[7];
    for (int16_t i = 0; i < 7; ++i) {
        int32_t saw_detune = detune * (i - 3);
        int32_t detune_integral = saw_detune >> 16;
        int32_t detune_fractional = saw_detune & 0xffff;
        int32_t increment_a = ComputePhaseIncrement(pitch_ + detune_integral);
        int32_t increment_b = ComputePhaseIncrement(pitch_ + detune_integral + 1);
        increments[i] = increment_a + \
        (((increment_b - increment_a) * detune_fractional) >> 16);
    }
    if (strike_) {
        for (size_t i = 0; i < 6; ++i) {
            phase[i] = Random::GetWord();
        }
        strike_ = false;
    }

    CONSTRAIN(spread, 0.f, 1.f)
    static constexpr int32_t kPanPos[7] = {-256, -171, -85, 0, 85, 171, 256};
    int32_t leftGain[7];
    int32_t rightGain[7];
    int32_t leftGainSum = 0;
    int32_t rightGainSum = 0;
    const int32_t spreadQ = static_cast<int32_t>(spread * 256.f);
    for (int i = 0; i < 7; i++) {
        const int32_t pan = (kPanPos[i] * spreadQ) >> 8;
        leftGain[i] = pan <= 0 ? 256 : 256 - pan;
        rightGain[i] = pan >= 0 ? 256 : 256 + pan;
        leftGainSum += leftGain[i];
        rightGainSum += rightGain[i];
    }
    const int32_t leftNorm = (7 << 12) / leftGainSum;
    const int32_t rightNorm = (7 << 12) / rightGainSum;

    while (size--) {
        phase_ += increments[0];
        phase[0] += increments[1];
        phase[1] += increments[2];
        phase[2] += increments[3];
        phase[3] += increments[4];
        phase[4] += increments[5];
        phase[5] += increments[6];

        const int32_t saw[7] = {
                static_cast<int32_t>(phase_ >> 19) - 4096,
                static_cast<int32_t>(phase[0] >> 19) - 4096,
                static_cast<int32_t>(phase[1] >> 19) - 4096,
                static_cast<int32_t>(phase[2] >> 19) - 4096,
                static_cast<int32_t>(phase[3] >> 19) - 4096,
                static_cast<int32_t>(phase[4] >> 19) - 4096,
                static_cast<int32_t>(phase[5] >> 19) - 4096
        };

        int32_t sampleL = 0;
        int32_t sampleR = 0;
        for (int i = 0; i < 7; i++) {
            sampleL += saw[i] * leftGain[i];
            sampleR += saw[i] * rightGain[i];
        }
        sampleL = (sampleL * leftNorm) >> 12;
        sampleR = (sampleR * rightNorm) >> 12;
        CONSTRAIN(sampleL, -32768, 32767)
        CONSTRAIN(sampleR, -32768, 32767)

        sampleL = Interpolate88(ws_moderate_overdrive, sampleL + 32768);
        sampleR = Interpolate88(ws_moderate_overdrive, sampleR + 32768);
        sampleL >>= damp_;
        sampleR >>= damp_;
        *left++ += (sampleL * left_gain_q12) >> 12;
        *right++ += (sampleR * right_gain_q12) >> 12;
    }
}

uint32_t MiSuperSawOsc::ComputePhaseIncrement(int16_t midi_pitch) {
    if (midi_pitch >= kPitchTableStart) {
        midi_pitch = kPitchTableStart - 1;
    }

    int32_t ref_pitch = midi_pitch;
    ref_pitch -= kPitchTableStart;

    size_t num_shifts = 0;
    while (ref_pitch < 0) {
        ref_pitch += kOctave;
        ++num_shifts;
    }

    uint32_t a = lut_oscillator_increments[ref_pitch >> 4];
    uint32_t b = lut_oscillator_increments[(ref_pitch >> 4) + 1];
    uint32_t phase_increment = a + \
      (static_cast<int32_t>(b - a) * (ref_pitch & 0xf) >> 4);
    phase_increment >>= num_shifts;
    return phase_increment;
}

uint32_t MiSuperSawOsc::ComputeDelay(int16_t midi_pitch) {
    if (midi_pitch >= kHighestNote - kOctave) {
        midi_pitch = kHighestNote - kOctave;
    }

    int32_t ref_pitch = midi_pitch;
    ref_pitch -= kPitchTableStart;

    size_t num_shifts = 0;
    while (ref_pitch < 0) {
        ref_pitch += kOctave;
        ++num_shifts;
    }

    uint32_t a = lut_oscillator_delays[ref_pitch >> 4];
    uint32_t b = lut_oscillator_delays[(ref_pitch >> 4) + 1];
    uint32_t delay = a + (static_cast<int32_t>(b - a) * (ref_pitch & 0xf) >> 4);
    delay >>= 12 - num_shifts;
    return delay;
}

void MiSuperSawOsc::SetDetune(const int16_t &detune) {
    detune_ = detune;
}

void MiSuperSawOsc::SetDamp(const uint16_t &damp) {
    damp_ = damp;
}

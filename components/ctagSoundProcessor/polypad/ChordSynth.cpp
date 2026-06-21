/***************
CTAG TBD >>to be determined<< is an open source eurorack synthesizer module.

A project conceived within the Creative Technologies Arbeitsgruppe of
Kiel University of Applied Sciences: https://www.creative-technologies.de

(c) 2020 by Robert Manzke. All rights reserved.

The CTAG TBD software is licensed under the GNU General Public License
(GPL 3.0), available here: https://www.gnu.org/licenses/gpl-3.0.txt

The CTAG TBD hardware design is released under the Creative Commons
Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0).
Details here: https://creativecommons.org/licenses/by-nc-sa/4.0/

CTAG TBD is provided "as is" without any express or implied warranties.

License and copyright details for specific submodules are included in their
respective component folders / files if different from this license.
***************/

#include "ChordSynth.hpp"
#include <cstring>
#include <cstdio>
#include <cmath>
#include "stmlib/utils/random.h"
#include <esp_random.h>

void CTAG::SP::ChordSynth::NoteOff() {
    adsr.Gate(false);
}

void CTAG::SP::ChordSynth::SetCutoff(const uint32_t &cutoff) {
    params_.filter_freq = cutoff;
    svf.set_frequency(cutoff);
    svf_right.set_frequency(cutoff);
}

void CTAG::SP::ChordSynth::SetResonance(const uint32_t &resonance) {
    params_.filter_reso = resonance;
    svf.set_resonance(resonance);
    svf_right.set_resonance(resonance);
}

void  CTAG::SP::ChordSynth::Process(float *buf, const uint32_t &ofs) {
    memset(buffer, 0, 32 * 2);

    // vibrato and render buffer
    for (int i=0;i<params_.nnotes;i++) {
        v_osc[i].SetPitch(
                params_.pitch + scale[i] * 128 + static_cast<int16_t>(lfo1.Process() * params_.lfo1_amt * 128.f));
        v_osc[i].Render(buffer, 32);
    }

    // apply filter with lfo and eg
    float eg = adsr.Process();
    float ffreq = static_cast<float>(params_.filter_freq); // base
    ffreq += params_.eg_filt_amt * eg * 16383.f; // eg
    ffreq += lfo2.Process() * params_.lfo2_amt * 16383.f; // lfo
    CONSTRAIN(ffreq, 0, 16383)

    svf.set_frequency(static_cast<uint16_t>(ffreq));
    for (int i = 0; i < 32; i++) {
        buffer[i] = svf.Process(buffer[i]);
    }

    for (int i = 0; i < 32; i++) {
        buf[i * 2 + ofs] += static_cast<float>(buffer[i] >> 1) / 32767.f * eg;
    }
}

void CTAG::SP::ChordSynth::ProcessStereo(float *buf) {
    const float spread = params_.stereo_spread;
    const float tilt = params_.stereo_tilt * spread * 0.5f;
    const float phase = params_.stereo_phase;
    const int32_t numNotes = params_.nnotes;

    if (spread <= 0.f && phase <= 0.f) {
        memset(buffer, 0, sizeof(buffer));
        for (int i = 0; i < numNotes; i++) {
            v_osc[i].SetPitch(
                    params_.pitch + scale[i] * 128 + static_cast<int16_t>(lfo1.Process() * params_.lfo1_amt * 128.f));
            v_osc[i].Render(buffer, 32);
        }

        float eg = adsr.Process();
        float ffreq = static_cast<float>(params_.filter_freq);
        ffreq += params_.eg_filt_amt * eg * 16383.f;
        ffreq += lfo2.Process() * params_.lfo2_amt * 16383.f;
        CONSTRAIN(ffreq, 0, 16383)

        svf.set_frequency(static_cast<uint16_t>(ffreq));
        svf_right.set_frequency(static_cast<uint16_t>(ffreq));
        for (int i = 0; i < 32; i++) {
            const float left = static_cast<float>(svf.Process(buffer[i])) / 32767.f * eg;
            const float right = static_cast<float>(svf_right.Process(buffer[i])) / 32767.f * eg;
            buf[i * 2 + 0] += left;
            buf[i * 2 + 1] += right;
        }
        return;
    }

    memset(left_buffer, 0, sizeof(left_buffer));
    memset(right_buffer, 0, sizeof(right_buffer));

    const float motion = stereo_lfo.Process() * params_.stereo_motion * spread * 0.75f;

    for (int i = 0; i < numNotes; i++) {
        v_osc[i].SetPitch(
                params_.pitch + scale[i] * 128 + static_cast<int16_t>(lfo1.Process() * params_.lfo1_amt * 128.f));

        float basePan = 0.f;
        if (numNotes > 1) {
            basePan = (static_cast<float>(i) * 2.f / static_cast<float>(numNotes - 1)) - 1.f;
        }
        float pan = basePan * spread + tilt + motion;
        CONSTRAIN(pan, -1.f, 1.f)

        const int32_t panQ12 = static_cast<int32_t>(pan * 4096.f);
        const int32_t leftGainQ12 = panQ12 <= 0 ? 4096 : 4096 - panQ12;
        const int32_t rightGainQ12 = panQ12 >= 0 ? 4096 : 4096 + panQ12;
        if (phase <= 0.f) {
            v_osc[i].RenderAccum(left_buffer, right_buffer, 32, leftGainQ12, rightGainQ12);
        } else {
            v_osc[i].RenderStereoAccum(left_buffer, right_buffer, 32, phase, leftGainQ12, rightGainQ12);
        }
    }

    float eg = adsr.Process();
    float ffreq = static_cast<float>(params_.filter_freq);
    ffreq += params_.eg_filt_amt * eg * 16383.f;
    ffreq += lfo2.Process() * params_.lfo2_amt * 16383.f;
    CONSTRAIN(ffreq, 0, 16383)

    svf.set_frequency(static_cast<uint16_t>(ffreq));
    svf_right.set_frequency(static_cast<uint16_t>(ffreq));

    for (int i = 0; i < 32; i++) {
        const float left = static_cast<float>(svf.Process(left_buffer[i]));
        const float right = static_cast<float>(svf_right.Process(right_buffer[i]));
        buf[i * 2 + 0] += left / 32767.f * eg;
        buf[i * 2 + 1] += right / 32767.f * eg;
    }
}

bool CTAG::SP::ChordSynth::IsDead() {
    return adsr.IsIdle();
}

void CTAG::SP::ChordSynth::SetFilterType(const SvfMode &mode) {
    mode_ = mode;
    svf.set_mode(mode_);
    svf_right.set_mode(mode_);
}

void CTAG::SP::ChordSynth::SetLfo1(const float &freq, const float &amt) {
    params_.lfo1_freq = freq;
    params_.lfo1_amt = amt;
    lfo1.SetFrequency(freq);
}

void CTAG::SP::ChordSynth::SetLfo2(const float &freq, const float &amt) {
    params_.lfo2_freq = freq;
    params_.lfo2_amt = amt;
    lfo2.SetFrequency(freq);
}

void CTAG::SP::ChordSynth::SetEgFiltAmt(const float &amt) {
    params_.eg_filt_amt = amt;
}

void CTAG::SP::ChordSynth::SetStereo(const float &spread, const float &tilt, const float &motion, const float &phase) {
    params_.stereo_spread = spread;
    params_.stereo_tilt = tilt;
    params_.stereo_motion = motion;
    params_.stereo_phase = phase;
    stereo_lfo.SetFrequency(0.03f + motion * 0.37f);
}

void CTAG::SP::ChordSynth::SetDetune(const uint32_t &detune) {
    for (auto &osc:v_osc) {
        osc.SetDetune(detune);
    }
}

void  CTAG::SP::ChordSynth::calcInversion(int8_t *ht_steps, const int16_t &chord, const int16_t &inversion,
                                                   const int16_t &nnotes) {
    int8_t inv[8];
    for (int i = 0; i < nnotes; i++) {
        int idx = i + 2 + inversion;
        if (idx < 0) idx = 0;
        if (idx >= kChordNumNotes) idx = kChordNumNotes - 1;
        inv[i] = chords[chord][idx];
    }
    memcpy(ht_steps, inv, nnotes);
}

float CTAG::SP::ChordSynth::GetTTL() {
    return adsr.GetOutput();
}

void CTAG::SP::ChordSynth::Hold() {
    adsr.Hold();
}

void CTAG::SP::ChordSynth::Init(const CTAG::SP::ChordSynth::ChordParams &params) {
    params_ = params;
    stmlib::Random::Seed(esp_random());
    lfo1.SetSampleRate(44100.f / 32.f);
    lfo1.SetFrequencyPhase(params.lfo1_freq, 6.2f * stmlib::Random::GetFloat());
    lfo2.SetSampleRate(44100.f / 32.f);
    if (params.lfo2_random_phase)
        lfo2.SetFrequencyPhase(params.lfo2_freq, 6.2f * stmlib::Random::GetFloat());
    else
        lfo2.SetFrequencyPhase(params.lfo2_freq, 3.1415f);
    stereo_lfo.SetSampleRate(44100.f / 32.f);
    stereo_lfo.SetFrequencyPhase(0.03f + params.stereo_motion * 0.37f, 6.2f * stmlib::Random::GetFloat());
    adsr.SetSampleRate(44100.f / 32.f);
    adsr.SetAttack(params.attack);
    adsr.SetDecay(params.decay);
    adsr.SetSustain(params.sustain);
    adsr.SetRelease(params.release);
    adsr.SetModeExp();
    adsr.Gate(true);
    svf.Init();
    svf.set_mode(static_cast<SvfMode>(params.filter_type));
    svf_right.Init();
    svf_right.set_mode(static_cast<SvfMode>(params.filter_type));

    calcInversion(scale, params.chord, params.inversion, params.nnotes);

    for (int i = 0; i < params.nnotes; i++) {
        v_osc[i].Init();
        v_osc[i].SetPitch(params.pitch + scale[i] * 128);
    }
}

void CTAG::SP::ChordSynth::Reset() {
    adsr.Reset();
}

/***************
TBD-16 — Macro/Preset System & GrooveBoxRack

(c) 2024-2026 Per-Olov Jernberg (possan). https://possan.codes
(c) 2024-2026 Johannes Elias Lohbihler for dadamachines.
Based in part on the CTAG TBD DrumRack / engine by Robert Manzke (CTAG Kiel).

Licensed under the GNU General Public License (GPL 3.0):
https://www.gnu.org/licenses/gpl-3.0.txt

A commercial licence is available — contact https://dadamachines.com/contact/

Provided "as is" without any express or implied warranties.
See LICENSE in the repository root for full terms.

SPDX-License-Identifier: GPL-3.0-only
***************/

#include "RackSynth.hpp"
#include "RackRompler.hpp"
#include "../ctagSoundProcessorGrooveBoxRack.hpp"
#include <algorithm>
#include <cmath>

using namespace CTAG::SP;

void RackRompler::Init(const GrooveBoxRackInitData *initdata) {
    rompler.Init(44100.f);

    initdata->rack->registerParamAndCC(initdata, "bank", 8, [&](const int val){ s1_bank = val;});
    initdata->rack->registerParamAndCC(initdata, "slice", 9, [&](const int val) { s1_slice = val; });
    initdata->rack->registerParamAndCC(initdata, "start", 10, [&](const int val){ s1_start = val / 4095.f;});
    initdata->rack->registerParamAndCC(initdata, "end", 11, [&](const int val) { s1_end = val / 4095.f; });

    initdata->rack->registerParamAndCC(initdata, "fc", 12, [&](const int val){ s1_fc = val;});
    initdata->rack->registerParamAndCC(initdata, "fq", 13, [&](const int val){ s1_fq = val;});
    initdata->rack->registerParamAndCC(initdata, "ft", 14, [&](const int val){ s1_ft = val;});
    initdata->rack->registerParamAndCC(initdata, "brr", 15, [&](const int val){ s1_brr = val;});

    initdata->rack->registerParamAndCC(initdata, "atk", 16, [&](const int val){ s1_atk = val;});
    initdata->rack->registerParamAndCC(initdata, "dcy", 17, [&](const int val){ s1_dcy = val;});
    initdata->rack->registerParamAndCC(initdata, "speed", 18, [&](const int val){ s1_speed = val;});
    initdata->rack->registerParamAndCC(initdata, "pitch", 19, [&](const int val){ s1_pitch = val;});

    initdata->rack->registerParamAndCC(initdata, "lp", 20, [&](const int val){ s1_lp = val;});
    initdata->rack->registerParamAndCC(initdata, "lp_pp", 21, [&](const int val){ s1_lp_pp = val;});
    initdata->rack->registerParamAndCC(initdata, "lp_pos", 22, [&](const int val){ s1_lp_pos = val / 4095.f;});
    initdata->rack->registerParamAndCC(initdata, "eg2fm", 23, [&](const int val){ s1_eg2fm = val;});

    initdata->rack->registerParamAndCC(initdata, "tsmode", 24, [&](const int val){ s1_tsmode = val;});
    initdata->rack->registerParamAndCC(initdata, "tsamount", 25, [&](const int val){ s1_tsamount = val;});
    initdata->rack->registerParamAndCC(initdata, "eg2flt", 26, [&](const int val){ s1_eg2flt = val;});

    s1_lp = 0;
    s1_lp_pp = 0;

    this->enabled = false;
}

void RackRompler::SetMarkerControl(float startOffsetRelative, float lengthRelative,
                                  float loopMarker, uint32_t revision) {
    if (!std::isfinite(startOffsetRelative) || !std::isfinite(lengthRelative) ||
        !std::isfinite(loopMarker)) {
        return;
    }
    s1_start = std::clamp(startOffsetRelative, 0.f, 1.f);
    s1_end = std::clamp(lengthRelative, 0.f, 1.f);
    s1_lp_pos = std::clamp(loopMarker, 0.f, 1.f);
    marker_revision = revision;
}

CTAG::SYNTHESIS::RomplerVoiceMinimal::Telemetry RackRompler::GetTelemetry() const {
    return rompler.GetTelemetry();
}

void RackRompler::noteOn(uint8_t note, uint8_t vel) {
    midi_trig = true;
    midi_note = note;
    midi_freq = 440.f * powf(2.f, (note - 69) / 12.f);
}

void RackRompler::noteOff(uint8_t, uint8_t) {
    // Rompler playback follows its one-shot AD envelope, not MIDI gate length.
}

void RackRompler::Process(const GrooveBoxRackProcessData &data) {
    if (!this->enabled) {
        return;
    }

    std::fill_n(s1_out, BUF_SZ, 0.f);

    // Mode arrives as compact macro value 0..4, expanded by the CC path:
    // Off / Free Tape / Sync Tape / Free KFR / Sync KFR.
    // The sequencer owns and persists the Sync reference tempo.
    timeStretchMode = static_cast<uint8_t>(
        std::clamp((s1_tsmode.load() + 16) / 32, 0, 4));
    rompler.params.timeStretchEnable = (timeStretchMode != 0);
    rompler.params.timeStretchAlgorithm =
        timeStretchMode >= 3
            ? CTAG::SYNTHESIS::RomplerVoiceMinimal::Params::TimeStretchAlgorithm::EXTREMA
            : CTAG::SYNTHESIS::RomplerVoiceMinimal::Params::TimeStretchAlgorithm::CLASSIC;

    uint32_t firstNonWtSlice = data.firstNonWtSlice;
    MK_INT_PAR_ABS_NOCV(iS1Bank, s1_bank, 128.f)
    CONSTRAIN(iS1Bank, 0, 31)
    MK_INT_PAR_ABS_NOCV(iS1Slice, s1_slice, 128.f) // midi cc
    CONSTRAIN(iS1Slice, 0, 31)
    iS1Slice = iS1Bank * 32 + iS1Slice + firstNonWtSlice;
    rompler.params.slice = iS1Slice;

    // Periodic diagnostic — DISABLED. Audio-thread: never printf here.
    // Even gated to every 50000 calls (~37 s at 44.1 kHz / BUF_SZ 32) the
    // printf can block the audio task long enough to glitch the output buffer.
    // static uint32_t _romplerDiagCtr = 0;
    // if ((_romplerDiagCtr++ % 50000) == 0) {
    //     printf("DIAG RackRompler: bank=%d slice=%d abs=%d firstNonWt=%lu s1_bank=%d s1_slice=%d\n",
    //            iS1Bank, (int)(iS1Slice - iS1Bank * 32 - firstNonWtSlice), iS1Slice, firstNonWtSlice,
    //            (int)s1_bank.load(), (int)s1_slice.load());
    // }

    // Speed — symmetric bipolar ±2.00x, quantized to exact hundredths.
    //
    // The sequencer's 0..400 source range is expanded to full NRPN by the
    // macro, then normalized to 0..4095 by the rack's standard NRPN handler.
    // Rounding here reconstructs every requested 0.01 step exactly. Speed
    // zero is suppressed at note trigger by the deadband below.
    float fS1Speed = roundf((((float)s1_speed.load() / 4095.f) * 4.f - 2.f) * 100.f) / 100.f;
    CONSTRAIN(fS1Speed, -2.f, 2.f)
    float effectiveSpeed = fS1Speed;
    const uint32_t referenceTempo = timeStretchReferenceTempo.load();
    const bool timeStretchSync = timeStretchMode == 2 || timeStretchMode == 4;
    if (timeStretchSync && referenceTempo > 0 && data.tempo > 0) {
        effectiveSpeed *= (float)data.tempo / (float)referenceTempo;
        CONSTRAIN(effectiveSpeed, -2.f, 2.f)
    }

    // Pitch follows DrumRack.cpp:435-440: RomplerVoiceMinimal receives
    // semitones directly. GrooveBoxRack transports the parameter as NRPN,
    // so convert its normalized CV-space representation back to ±12
    // semitones and quantize to tenth-semitone steps. Synth-track
    // Romplers additionally follow incoming MIDI notes; C3 / MIDI 48 is the
    // natural sample pitch used by the hardware keyboard's center octave.
    float fS1PitchSemi =
        roundf(((float)s1_pitch.load() - 2048.f) / 2048.f * 120.f) / 10.f;
    CONSTRAIN(fS1PitchSemi, -12.f, 12.f)
    fS1PitchSemi += midi_note - NaturalPitchMidiNote;
    rompler.params.pitch = fS1PitchSemi;

    rompler.params.startOffsetRelative = s1_start.load();
    rompler.params.lengthRelative = s1_end.load();
    rompler.params.loopMarker = s1_lp_pos.load();
    MK_BOOL_PAR_NOCV(bS1Loop, s1_lp)
    rompler.params.loop = bS1Loop;
    MK_BOOL_PAR_NOCV(bS1LoopPipo, s1_lp_pp)
    rompler.params.loopPiPo = bS1LoopPipo;
    MK_FLT_PAR_ABS_NOCV(fS1Attack, s1_atk, 4095.f, 2.f)
    if (fS1Attack < 0.001f) fS1Attack = 0.001f; // prevent div-by-zero in AD envelope
    rompler.params.a = fS1Attack;
    MK_FLT_PAR_ABS_NOCV(fS1Decay, s1_dcy, 4095.f, 50.f)
    if (fS1Decay < 0.01f) fS1Decay = 0.01f; // prevent div-by-zero in AD envelope
    rompler.params.d = fS1Decay;
    MK_FLT_PAR_ABS_SFT_NOCV(fS1EGFM, s1_eg2fm, 4095.f, 12.f)
    rompler.params.egFM = fS1EGFM;
    float fS1EGFilter = ((float)s1_eg2flt.load() - 2048.f) / 2048.f;
    CONSTRAIN(fS1EGFilter, -1.f, 1.f)
    rompler.params.egFilter = fS1EGFilter;
    // Use the complete normalized control range while preserving every
    // discrete bit-reduction state (0..14). Rounding avoids skipped states
    // when the compact macro value is expanded over the 7-bit CC range.
    int iS1Brr = (s1_brr * 14 + 2047) / 4095;
    CONSTRAIN(iS1Brr, 0, 14)
    rompler.params.bitReduction = iS1Brr;
    // filter params
    MK_FLT_PAR_ABS_NOCV(fS1Cut, s1_fc, 4095.f, 1.f)
    rompler.params.cutoff = fS1Cut;
    MK_FLT_PAR_ABS_NOCV(fS1Reso, s1_fq, 4095.f, 10.f)
    rompler.params.resonance = fS1Reso;
    MK_INT_PAR_ABS_NOCV(iS1FType, s1_ft, 4.f)
    CONSTRAIN(iS1FType, 0, 3);
    float fTSWindow = 0.005f + (float)s1_tsamount.load() / 4095.f * 0.995f;
    rompler.params.timeStretchWindowSize = fTSWindow;

    // Speed scrub-stop deadband — at |Speed| < kSpeedDeadband the
    // wire is at/near centre (wire 64 ±2). The engine's pitch
    // shifter would divide-by-zero, and an epsilon clamp produced a
    // stuck-playhead near-silent state on previous trigs. Instead,
    // suppress gate entirely at scrub-stop: the trig produces clean
    // silence, the engine state stays clean, and moving the knob
    // off centre next trig resumes normal playback. Pitch knob
    // unaffected — only Speed gates the voice.
    constexpr float kSpeedDeadband = 0.05f;  // ~wire ±1.6 around centre
    bool speedActive = (fabsf(effectiveSpeed) >= kSpeedDeadband);

    // MK_BOOL_PAR_NOCV(bGateS1, s1_gate)
    rompler.params.gate = midi_trig && speedActive;
    rompler.params.playbackSpeed = effectiveSpeed;
    if (midi_trig && !trig_prev && speedActive) {
        // printf("S2 slice=%ld ps=%1.1f pitch=%1.1f startoffrel=%1.1f lengthrel=%1.1f\n",
        //     rompler.params.slice,
        //     rompler.params.playbackSpeed,
        //     rompler.params.pitch,
        //     rompler.params.startOffsetRelative,
        //     rompler.params.lengthRelative);

        // printf("S3 a=%1.1f d=%1.1f fc=%1.1f fq=%1.1f ft=%d\n",
        //     (float)rompler.params.a,
        //     (float)rompler.params.d,
        //     (float)rompler.params.cutoff,
        //     (float)rompler.params.resonance,
        //     (int)rompler.params.filterType);

        // printf("S4 lo=%d pipo=%d lm=%1.1f egfm=%1.1f bitred=%ld gate=%d\n",
        //     rompler.params.loop,
        //     rompler.params.loopPiPo,
        //     (float)rompler.params.loopMarker,
        //     (float)rompler.params.egFM,
        //     rompler.params.bitReduction,
        //     rompler.params.gate);
    }
    trig_prev = midi_trig;
    midi_trig = false;

    rompler.params.filterType = static_cast<CTAG::SYNTHESIS::RomplerVoiceMinimal::Params::FilterType>(iS1FType);
    rompler.Process(s1_out, BUF_SZ);
};

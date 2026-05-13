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

#include "ctagSoundProcessorGrooveBoxRack.hpp"
#include "braids/quantizer_scales.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_cpu.h"
#include "esp_timer.h"
// #include "freertos/FreeRTOS.h"

using namespace CTAG::SP;

// File-scope peak meters for the OLED Input / Output indicators. Defined
// here, declared extern in ctagSoundProcessorGrooveBoxRack.hpp so SPManager
// can read them without needing access to the rack class internals.
volatile float g_peakInputTrack = 0.f;
volatile float g_peakSynthOnly  = 0.f;

// =====================================================================================
// CONTENTS — what's where in this file (search for the banner to jump)
// -------------------------------------------------------------------------------------
//   [1]  AUDIO BUS                — track-out mixers, FX bus pre-process, master render
//   [2]  PROCESS()                — the audio-block entry point: MIDI → tracks → FX → out
//   [3]  PARAM REGISTRATION       — registerParamAndCC(), handleMidiControlChange()
//   [4]  INIT / KNOW YOURSELF     — track wiring, model + global-param map setup
//   [4b] VOICE REGISTRY           — buildVoiceRegistry(): single source of truth for
//                                   which (track × machine) pairs exist + their note hooks
//   [5]  MIDI PARSER              — parseIncomingMidiMessages() raw-bytes split
//   [6]  TRACK CONFIG             — setTrackMachine() + setTrackMachineByDeviceValue()
//                                   + setTrackBank() (per-track machine selector tables)
//   [7]  MIDI ROUTING             — handleMidiNoteOn() / handleMidiNoteOff() per channel
//
//   (Look for "SIMULATOR-ONLY OVERRIDE" near buildVoiceRegistry for the sim's
//    loadPresetInternal — clean master/FX defaults that don't apply on device.
//    For "how do I add a new rack voice?" see docs/plugins/rack-plugins.rst.)
// =====================================================================================

// TODOs: fx return before compressor, stereo panning with delay -> when panned right, levels are lower, metallic sound of reverb.

#define maxFXSendLevelRev 1.5f

// =====================================================================================
// [1] AUDIO BUS — per-track output mixers + FX1/FX2/Master preprocessing + final mix
// =====================================================================================

void ctagSoundProcessorGrooveBoxRack::mixRenderOutputMono(float *source, float level, float pan, float fx1, float fx2) {
    float mL = (1.0f - pan);
    float mR = (1.0f + pan);

    CONSTRAIN(mL, 0.0f, 1.f);
    CONSTRAIN(mR, 0.0f, 1.f);

    mL *= level;
    mR *= level;

    float sL1 = mL * fx1;
    float sR1 = mR * fx1;
    float sL2 = mL * fx2;
    float sR2 = mR * fx2;

    for (int i = 0; i < bufSz; i++) {
        combined_out[i*2+0] += source[i] * mL;
        combined_out[i*2+1] += source[i] * mR;
        send1_out[i*2+0] += source[i] * sL1;
        send1_out[i*2+1] += source[i] * sR1;
        send2_out[i*2+0] += source[i] * sL2;
        send2_out[i*2+1] += source[i] * sR2;
    }
}

void ctagSoundProcessorGrooveBoxRack::mixRenderOutputStereo(float *source, float level, float pan, float fx1, float fx2) {
    float mL = (1.0f - pan);
    float mR = (1.0f + pan);

    CONSTRAIN(mL, 0.0f, 1.f);
    CONSTRAIN(mR, 0.0f, 1.f);

    mL *= level;
    mR *= level;

    float sL1 = mL * fx1;
    float sR1 = mR * fx1;
    float sL2 = mL * fx2;
    float sR2 = mR * fx2;

    for (int i = 0; i < bufSz; i++) {
        combined_out[i*2+0] += source[i*2+0] * mL;
        combined_out[i*2+1] += source[i*2+1] * mR;
        send1_out[i*2+0] += source[i*2+0] * sL1;
        send1_out[i*2+1] += source[i*2+1] * sR1;
        send2_out[i*2+0] += source[i*2+0] * sL2;
        send2_out[i*2+1] += source[i*2+1] * sR2;
    }
}

void ctagSoundProcessorGrooveBoxRack::preprocessFX1(const ProcessData& data) {
    // int global_bpm_lo2 = global_bpm_lo / 32;
    // int global_bpm_hi2 = global_bpm_hi / 32;
    // int scaledbpm = global_bpm_lo2 + (global_bpm_hi2 << 7);
    int scaledbpm = data.sequencer_tempo;
    if (scaledbpm != last_scaledbpm) {
        last_scaledbpm = scaledbpm;
        scaledbpm = scaledbpm / 10;
        if (scaledbpm < 32) scaledbpm = 32;
        // printf("Scaled BPM (%ld) set to %1.1f\n",
        //     data.sequencer_tempo,
        //     (float)scaledbpm/10.0f);
		last_msPerBeat = 60000.0f / ((float)(scaledbpm) / 10.0f);
    }

    // Sync knob branches the Time resolution:
    //   ON  → 12 musical divisors of the live msPerBeat (tempo-tracking).
    //   OFF → free mode, wire 0..127 → 0..2000 ms linear.
    bool bSync = fx1_sync;
    int wire = fx1_time_ms / 32;                     // atomic is 0..4064, knob is 0..127
    if (wire < 0) wire = 0;
    if (wire > 127) wire = 127;

    float dt_ms;
    if (bSync) {
        int idx = (wire * 12) / 128;
        if (idx < 0) idx = 0;
        if (idx > 11) idx = 11;
        // Per-beat fractions: quarter note = msPerBeat. 1/16 = msPerBeat/4 etc.
        const float divisor_factor[12] = {
            1.f/8.f,  1.f/6.f,  1.f/4.f,  3.f/8.f,   // 1/32, 1/16T, 1/16, 1/16D
            1.f/3.f,  1.f/2.f,  3.f/4.f,  2.f/3.f,   // 1/8T,  1/8,   1/8D, 1/4T
            1.f,      3.f/2.f,  2.f,      4.f        // 1/4,   1/4D,  1/2,  1/1
        };
        dt_ms = last_msPerBeat * divisor_factor[idx];
    } else {
        // Free mode: 0..2000 ms linear.
        dt_ms = (float)wire / 127.f * 2000.f;
    }
    float dt = dt_ms * 44.1f;                         // ms → samples
    CONSTRAIN(dt, 4.0f, 88200.f);
    int idt = (int)dt;
    if (idt != delaySamples) {
        delaySamples = idt;
    }

    MK_FLT_PAR_ABS_NOCV(fBase, fx1_base, 4095.f, 1.f)
    MK_FLT_PAR_ABS_NOCV(fWidth, fx1_width, 4095.f, 1.f)
    bool bSyncTrig {false};
    // if(trig_fx1_sync != -1) bSyncTrig = data.trig[trig_fx1_sync] == 1 ? false : true;
    // if(!bSync){
    // if(cv_fx1_time_ms != -1) fDelayTime = fabsf(data.cv[cv_fx1_time_ms]) * 2000.f;
    // }

    fBase = 20.f * stmlib::SemitonesToRatio(fBase * 120.f);
    fWidth = 20.f * stmlib::SemitonesToRatio(fWidth * 120.f);
    CONSTRAIN(fBase, 20.f, 20000.f)
    CONSTRAIN(fWidth, 50.f, 20000.f)
    float hp_cut = fBase;
    float lp_cut = fBase + fWidth;
    CONSTRAIN(lp_cut, 20.f, 20000.f)
    CONSTRAIN(hp_cut, 20.f, 20000.f)
    lp_l.set_f<stmlib::FREQUENCY_ACCURATE>(lp_cut / 44100.f);
    hp_l.set_f<stmlib::FREQUENCY_ACCURATE>(hp_cut / 44100.f);
    lp_r.copy_f(lp_l);
    hp_r.copy_f(hp_l);

    // Delay-input HP corner — independent of the feedback-path HP. 20 Hz
    // .. 20 kHz log sweep (10 octaves) — matches the OLED PT_FILTER_CUTOFF
    // renderer which displays `20 × 1000^(wire/127)` ≈ 20..20k. Earlier
    // 80-semitone DSP scale topped out at ~2 kHz, so the OLED was lying
    // about the upper half of the knob.
    MK_FLT_PAR_ABS_NOCV(fInputHpNorm, fx1_input_hp, 4095.f, 1.f)
    float dly_in_hp = 20.f * stmlib::SemitonesToRatio(fInputHpNorm * 120.f);
    CONSTRAIN(dly_in_hp, 20.f, 20000.f)
    dly_input_hp_l.set_f<stmlib::FREQUENCY_ACCURATE>(dly_in_hp / 44100.f);
    dly_input_hp_r.copy_f(dly_input_hp_l);

    // sync mechanism
    // if(bSyncTrig != pre_sync){
    //     pre_sync = bSyncTrig;
    //     if(bSyncTrig && bSync){
    //         int delta = timer - pre_timer;
    //         if(std::abs(delta) > 1){
    //             fDelayTime = static_cast<float>(timer) * 32.f / 44.1f;
    //         }
    //         pre_timer = timer;
    //         timer = 0;
    //     }
    // }
    timer++;
}

void ctagSoundProcessorGrooveBoxRack::preprocessFX2(const ProcessData& data) {
    MK_FLT_PAR_ABS_NOCV(fRevTime, fx2_time, 4095.f, 1.f)
    MK_FLT_PAR_ABS_NOCV(fReverbLPF, fx2_lp, 4095.f, 1.f)
    // fx2_diffuse → reverb.set_diffusion (was hardcoded 0.7). 0.0 ≈ slap
    // echo, ~0.9 ≈ dense diffuse tail.
    MK_FLT_PAR_ABS_NOCV(fDiffuse, fx2_diffuse, 4095.f, 0.95f)
    reverb.set_time(fRevTime);
    // Damp semantics: invert so wire 0 = no damping (bright tail) and
    // wire 127 = full damping (dark tail). The mutable reverb's internal
    // set_lp() takes a coefficient where high values let highs through,
    // so the user-facing "Damp" knob inverts that.
    reverb.set_lp(1.0f - fReverbLPF);
    reverb.set_diffusion(fDiffuse);
    // ModRate knob retired 2026-04-28: the reverb's internal LFO modulation
    // depth is fixed (±80 / ±40 / ±50 samples, sized exactly to the all-pass
    // and recirculating delay-line buffers — increasing depth reads OUT of
    // those buffers and produces harsh feedback runaway). At the safe
    // depths the LFO frequency change is barely audible, so the knob has no
    // useful musical range. LFO frequencies hardcoded to upstream
    // DrumRack::Init values 0.5 Hz / 0.3 Hz in GrooveBoxRack::Init below.
    // The fx2_modulation atomic + DEFINE_GLOBAL_PARAM stay registered for
    // SPI compatibility but are no longer read by the DSP path.
    // fx2_input_gain promotes the previously-hardcoded set_input_gain(0.5)
    // (default wire 64 ≈ 0.5 preserves legacy behaviour).
    MK_FLT_PAR_ABS_NOCV(fInGain, fx2_input_gain, 4095.f, 1.f)
    reverb.set_input_gain(fInGain);
    // TankLvl knob retired 2026-04-28: set_amount() is a dry/wet crossfade
    // INSIDE the tank (`output = input + (wet - input) * amount`), not a
    // wet-level control — duplicates the master Reverb return. Removed
    // from the OLED + WebUI for UX clarity. Hardcode set_amount(1.0f) to
    // match the upstream ctagSoundProcessorDrumRack::Init baseline so the
    // reverb is fully wet at the tank output regardless of preset state.
    // The fx2_tank_level atomic + DEFINE_GLOBAL_PARAM stay registered for
    // SPI compatibility but are no longer read by the DSP path.
    // MK_FLT_PAR_ABS_NOCV(fTankLvl, fx2_tank_level, 4095.f, 1.f)
    // reverb.set_amount(fTankLvl);
    reverb.set_amount(1.0f);
    // Reverb-input HP shelf, applied per-sample before the tank in
    // renderMasterOutput. 20 Hz..~20 kHz log sweep (10 octaves) — wider
    // than fx1_input_hp's 80-semitone range so the upper half of the knob
    // reaches into the vocal/mid band where the cut is musically obvious
    // (the OnePole is only 6 dB/oct, so we need the cutoff to climb high
    // for an audible effect on a sustained pad). CONSTRAIN to 20..20 kHz
    // keeps the OnePole stable. Earlier 80-semitone range topped out at
    // ~1.9 kHz which only shaved a few dB off the bass — too subtle.
    MK_FLT_PAR_ABS_NOCV(fRevHpNorm, fx2_hp, 4095.f, 1.f)
    float rev_hp = 20.f * stmlib::SemitonesToRatio(fRevHpNorm * 120.f);
    CONSTRAIN(rev_hp, 20.f, 20000.f)
    rev_hp_l.set_f<stmlib::FREQUENCY_ACCURATE>(rev_hp / 44100.f);
    rev_hp_r.copy_f(rev_hp_l);
}

void ctagSoundProcessorGrooveBoxRack::preprocessMaster(const ProcessData& data) {
    MK_FLT_PAR_ABS_MIN_MAX_NOCV(fCompThresdB, c_thres, 4095.f, -80.f, 0.f)
    sumCompressor.setThresh(fCompThresdB);
    MK_FLT_PAR_ABS_MIN_MAX_NOCV(fCompAtk, c_atk, 4095.f, 0.3f, 30.f)
    sumCompressor.setAttack(fCompAtk);
    MK_FLT_PAR_ABS_MIN_MAX_NOCV(fCompRel, c_rel, 4095.f, 40.f, 2000.f)
    sumCompressor.setRelease(fCompRel);
    MK_FLT_PAR_ABS_MIN_MAX_NOCV(fCompRatio, c_ratio, 4095.f, 0.0001f, 1.25f)
    sumCompressor.setRatio(fCompRatio);
}

void ctagSoundProcessorGrooveBoxRack::renderMasterOutput(const ProcessData& data) {
    // delay
    MK_BOOL_PAR_NOCV(bFreeze, fx1_freeze)
    MK_FLT_PAR_ABS_NOCV(fDelayStereoWidth, fx1_st_width, 4095.f, 1.f)
    // Concave taper on Width: lower half of travel stays near-mono, full
    // stereo expansion is concentrated in the upper half. k=0.06 = the
    // upstream "aggressive" preset. (Commit 38f2975e.)
    fDelayStereoWidth = HELPERS::FastConcaveTransfer(fDelayStereoWidth, 0.06f);
    MK_FLT_PAR_ABS_NOCV(fDelayReverbSend, fx1_fx_send, 4095.f, maxFXSendLevelRev)
    fDelayReverbSend *= fDelayReverbSend;
    // Feedback ceiling 1.2× pairs with the FastConcaveTransfer Width curve
    // so the cross-feed dominates the upper half of knob travel before
    // feedback-only buildup can run away. (Upstream commit 38f2975e.)
    MK_FLT_PAR_ABS_NOCV(fFeedback, fx1_feedback, 4095.f, 1.2f)
    MK_FLT_PAR_ABS_NOCV(fDelayAmount, fx1_amount, 4095.f, 2.f)

    // reverb
    MK_FLT_PAR_ABS_NOCV(fRevAmount, fx2_amount, 4095.f, 2.f)

    // sum compressor
    float buf_fx1_l[BUF_SZ], buf_fx1_r[BUF_SZ], buf_fx2[BUF_SZ];
    MK_BOOL_PAR_NOCV(bTapeDigital, fx1_tape_digital)
    MK_BOOL_PAR_NOCV(bSideChainLPF, c_lpf)
    MK_FLT_PAR_ABS_MIN_MAX_NOCV(fCompMUPGain, c_gain, 4095.f, 0.f, 60.f) // in dB
    if (fCompMUPGain != fCompMUPGain_pre){
        fCompMUPGain = chunkware_simple::dB2lin(fCompMUPGain);
        fCompMUPGain_pre = fCompMUPGain;
    }
    // Mix as conventional dry/wet (0..1), not the upstream CTAG bipolar
    // PAN curve. Wire 0 = full dry / bypass (compressor inaudible), wire
    // 127 = full wet (compressed). Matches Elektron / Roland groovebox
    // convention and the Pico's PT_PERCENT renderer.
    MK_FLT_PAR_ABS_NOCV(fCompMix, c_mix, 4095.f, 1.f)
    // CCs 67/68 (c_dly_level / c_rev_level) retired — they had no DSP
    // referent. FX returns are scaled by fRevAmount / fDelayAmount at the
    // end of renderMasterOutput().

    // overall mix — scale 2.0 squared (wire 0 = -∞, wire 64 = unity 0 dB,
    // wire 127 = +12 dB max). Octatrack-match convention: default sits at
    // unity, +12 dB on tap. SoftClip becomes a safety net again rather
    // than a default-on saturator. Coupled with Pico initsong.cpp Master
    // Vol default = wire 64 and PT_LEVEL_MASTER renderer scale=2.
    // See docs/architecture/gain-staging-vs-octatrack.md § "Master
    // Volume convention revisit — 2026-04-28".
    MK_FLT_PAR_ABS_NOCV(fMixLevel, sum_lev, 4095.f, 2.f)
    fMixLevel *= fMixLevel;

    // Render final buffer
    for (int i = 0; i < bufSz; i++){
        float fVal_l = combined_out[i * 2 + 0];
        float fVal_r = combined_out[i * 2 + 1];

        // FX1 models
        buf_fx1_l[i] = send1_out[i * 2 + 0];
        buf_fx1_r[i] = send1_out[i * 2 + 1];

        // FX2 models, reverb is mono in stereo out, but input buffer is stereo
        buf_fx2[i] = send2_out[i * 2 + 0];

        float dry_l = fVal_l;
        float dry_r = fVal_r;
        if (bSideChainLPF){
            ONE_POLE(side_l, fVal_l, 0.0005f);
            ONE_POLE(side_r, fVal_r, 0.0005f);
        }
        else{
            side_l = fVal_l;
            side_r = fVal_r;
        }
        side_l = fabsf(side_l);
        side_r = fabsf(side_r);
        float side = std::max(side_l, side_r);
        sumCompressor.process(fVal_l, fVal_r, side);
        fVal_l = fVal_l * fCompMUPGain * fCompMix + dry_l * (1.f - fCompMix);
        fVal_r = fVal_r * fCompMUPGain * fCompMix + dry_r * (1.f - fCompMix);
        data.buf[i * 2] = fVal_l * fMixLevel;
        data.buf[i * 2 + 1] = fVal_r * fMixLevel;
    }

    // fx buffers
    float dly_buf_l[BUF_SZ], dly_buf_r[BUF_SZ];
    float rev_buf_l[BUF_SZ], rev_buf_r[BUF_SZ];

    // delay
    // CONSTRAIN(delaySamples, 4.0f, 88200.f);
    float ofs = delaySamples;
    if(fabsf(ofs - delayOffset) < 16) ofs = delayOffset;
    // if (obs < 16) {
    //     obs = 16;
    // }
    for(int i=0; i<bufSz; i++){
        // Calculate the delay offset in samples
        if(delayOffset != ofs){
            if(bTapeDigital){
                if(ofs != delayOffset){
                    duck = 1.f;
                }
                delayOffset = ofs;
            } else {
                float temp = delayOffset;
                delayOffset = ONE_POLE(temp, ofs, 0.0001f);
            }
            readPos = static_cast<float>(writeIndex) - delayOffset;
            if(readPos < 0.f) readPos += float(delayBufferSizeMax);
            if(readPos >= float(delayBufferSizeMax)) readPos -= float(delayBufferSizeMax);
        }

        float inputSample_l = buf_fx1_l[i];
        float inputSample_r = buf_fx1_r[i];
        // HP applied to dry input before the delay loop, independent of
        // the feedback-path HP/LP below.
        inputSample_l = dly_input_hp_l.Process<stmlib::FILTER_MODE_HIGH_PASS>(inputSample_l);
        inputSample_r = dly_input_hp_r.Process<stmlib::FILTER_MODE_HIGH_PASS>(inputSample_r);
        float outputSample_l, outputSample_r;

        outputSample_l = HELPERS::InterpolateWaveLinearWrap(delayBuffer_l, readPos, delayBufferSizeMax);
        outputSample_r = HELPERS::InterpolateWaveLinearWrap(delayBuffer_r, readPos, delayBufferSizeMax);
        readPos += 1.f;
        readPos > float(delayBufferSizeMax) ? readPos -= float(delayBufferSizeMax) : readPos;

        float temp = duck;
        duck = ONE_POLE(temp, 0.f, 0.35f)
        outputSample_l = outputSample_l * (1.f - duck);
        outputSample_r = outputSample_r * (1.f - duck);
        // Write the input sample to the delay buffer
        float out_l, out_r;
        if(!bFreeze){
            out_l = inputSample_l + fFeedback * ((1.f - fDelayStereoWidth) * outputSample_l + fDelayStereoWidth * outputSample_r);
            out_l = lp_l.Process<stmlib::FILTER_MODE_LOW_PASS>(out_l);
            out_l = hp_l.Process<stmlib::FILTER_MODE_HIGH_PASS>(out_l);
            out_r = (1.f - fDelayStereoWidth) * inputSample_r + fFeedback * ((1.f - fDelayStereoWidth) * outputSample_r + fDelayStereoWidth * outputSample_l);
            out_r = lp_r.Process<stmlib::FILTER_MODE_LOW_PASS>(out_r);
            out_r = hp_r.Process<stmlib::FILTER_MODE_HIGH_PASS>(out_r);
        }
        else{
            out_l = ((1.f - fDelayStereoWidth) * outputSample_l + fDelayStereoWidth * outputSample_r);
            out_r = ((1.f - fDelayStereoWidth) * outputSample_r + fDelayStereoWidth * outputSample_l);
        }

        delayBuffer_l[writeIndex] = stmlib::SoftLimit(out_l);
        delayBuffer_r[writeIndex] = stmlib::SoftLimit(out_r);
        writeIndex = (writeIndex + 1) % delayBufferSizeMax;

        // Mix the dry (input) and wet (delayed) signal
        dly_buf_l[i] = outputSample_l;
        dly_buf_r[i] = outputSample_r;
        rev_buf_l[i] = buf_fx2[i] + dly_buf_l[i] * fDelayReverbSend;
        rev_buf_r[i] = buf_fx2[i] + dly_buf_r[i] * fDelayReverbSend;
    }

    // Pre-delay before the reverb tank. fx2_predelay maps wire 0..4064 →
    // 0..200 ms via a mono ring buffer (the tank sums L+R internally).
    // Bypassed when < 2 samples to avoid one-sample latency at zero.
    {
        int predly_raw = fx2_predelay;        // 0..4064
        if (predly_raw < 0) predly_raw = 0;
        if (predly_raw > 4095) predly_raw = 4095;
        int preDelaySamples = (predly_raw * 200 * 441) / (4095 * 10);  // ms × 44.1
        if (preDelaySamples < 2) {
            // bypass — rev_buf_l/r already carries the FX2 bus; nothing to do.
        } else {
            if (preDelaySamples >= preDelayBufSize) preDelaySamples = preDelayBufSize - 1;
            for (int i = 0; i < bufSz; i++) {
                // Mono sum of the current FX2 contribution (delay + direct FX2 send)
                float preIn = 0.5f * (rev_buf_l[i] + rev_buf_r[i]);
                preDelayBuf[preDelayWriteIdx] = preIn;
                int readIdx = preDelayWriteIdx - preDelaySamples;
                if (readIdx < 0) readIdx += preDelayBufSize;
                float preOut = preDelayBuf[readIdx];
                // Replace both channels with the pre-delayed mono; reverb tank
                // will take it from here (it sums L+R on input anyway).
                rev_buf_l[i] = preOut;
                rev_buf_r[i] = preOut;
                preDelayWriteIdx++;
                if (preDelayWriteIdx >= preDelayBufSize) preDelayWriteIdx = 0;
            }
        }
    }

    // HP shelf on the reverb input, applied per-sample before the tank.
    // Catches FX2 sends, the FX1→FX2 cross-send, and the pre-delayed
    // signal when fx2_predelay > 1. Independent of the in-loop LP fx2_lp.
    for (int i = 0; i < bufSz; i++) {
        rev_buf_l[i] = rev_hp_l.Process<stmlib::FILTER_MODE_HIGH_PASS>(rev_buf_l[i]);
        rev_buf_r[i] = rev_hp_r.Process<stmlib::FILTER_MODE_HIGH_PASS>(rev_buf_r[i]);
    }

    // reverb
    reverb.Process(rev_buf_l, rev_buf_r, bufSz);

    // add fx to sum
    fRevAmount *= fRevAmount;
    fDelayAmount *= fDelayAmount;
    for (int i = 0; i < bufSz; i++) {
        data.buf[i * 2] += rev_buf_l[i] * fRevAmount + dly_buf_l[i] * fDelayAmount;
        data.buf[i * 2 + 1] += rev_buf_r[i] * fRevAmount + dly_buf_r[i] * fDelayAmount;
    }

    // sum_drive: variable-depth SoftLimit soft saturation on the master bus.
    // Skipped entirely at wire 0 to keep the default path bit-identical to
    // the pre-Drive code (no regression on existing material). At Drive>0
    // the cubic clipper progressively saturates; 1/sqrt(drive) makeup
    // partially compensates the perceived loudness bump.
    MK_FLT_PAR_ABS_NOCV(fDriveNorm, sum_drive, 4095.f, 1.f)
    if (fDriveNorm > 1e-4f) {
        float fDrive = 1.f + fDriveNorm * 3.f;          // 1× .. 4×
        float fDriveMakeup = 1.f / sqrtf(fDrive);
        for (int i = 0; i < bufSz; i++) {
            data.buf[i * 2 + 0] = stmlib::SoftLimit(data.buf[i * 2 + 0] * fDrive) * fDriveMakeup;
            data.buf[i * 2 + 1] = stmlib::SoftLimit(data.buf[i * 2 + 1] * fDrive) * fDriveMakeup;
        }
    }
}

// =====================================================================================
// [2] PROCESS() — audio-block entry point.  Parses MIDI in, clears the bus buffers,
//     calls each track's PreProcess() + active-machine Process() + mixRenderOutput*(),
//     then preprocessFX1/2/Master() + renderMasterOutput() to write the stereo result.
// =====================================================================================

void ctagSoundProcessorGrooveBoxRack::Process(const ProcessData& data){
    framecounter ++;

    // TODO: process midi in data.midibytes here
    if (data.midi_bytes_length > 0) {
        parseIncomingMidiMessages(data.midi_bytes, data.midi_bytes_length);
    }

    memcpy(audio_in, data.buf, bufSz * 2 * sizeof(float));
    std::fill_n(combined_out, bufSz * 2, 0.f);
    std::fill_n(send1_out, bufSz * 2, 0.f);
    std::fill_n(send2_out, bufSz * 2, 0.f);

	struct GrooveBoxRackProcessData idata;
    idata.firstNonWtSlice = sampleRom.GetFirstNonWaveTableSlice();
    idata.sampleRom = &sampleRom;
    idata.tempo = data.sequencer_tempo;
    idata.quantum = data.sequencer_quantum;
    idata.msPerBeat = last_msPerBeat;
    idata.inputbuffer = audio_in;

    // process input first

    int64_t T2 = esp_timer_get_time();
    int64_t Tstart = T2;
    ch16.PreProcess(idata);
    if (ch16.enabled) {
        ch16_in.Process(idata); // - it does nothing...
        if (ch16_in.enabled) {
            mixRenderOutputStereo(ch16_in.out, ch16.level, ch16.pan, ch16.send1, ch16.send2);
        }
    }
    // Snapshot combined_out right after ch16 mixes — this is the
    // INPUT TRACK contribution to the master bus, before any other
    // tracks render. Used by the OLED Input/Output meters: input
    // meter = peak of this snapshot, output meter = peak of (final
    // combined_out − this snapshot) so synth tracks alone register
    // on the output meter and the input never bleeds into it.
    {
        float p = 0.f;
        for (int i = 0; i < bufSz * 2; i++) {
            ch16_combined_snapshot[i] = combined_out[i];
            float a = combined_out[i] < 0 ? -combined_out[i] : combined_out[i];
            if (a > p) p = a;
        }
        float prev = g_peakInputTrack;
        g_peakInputTrack = (p > prev) ? p : (0.85f * prev + 0.15f * p);
    }
    std::fill_n(data.buf, bufSz * 2, 0.f);

    // int64_t T = esp_timer_get_time();
    // ch16_render_time = T - T2;

    ch1.PreProcess(idata);
    if (ch1.enabled) {
        ch1_db.Process(idata);
        if (ch1_db.enabled) {
            mixRenderOutputMono(ch1_db.out, ch1.level, ch1.pan, ch1.send1, ch1.send2);
        }

        ch1_ab.Process(idata);
        if (ch1_ab.enabled) {
            mixRenderOutputMono(ch1_ab.out, ch1.level, ch1.pan, ch1.send1, ch1.send2);
        }

        ch1_smp.track_length = ch1.track_length;
        ch1_smp.Process(idata);
        if (ch1_smp.enabled) {
            mixRenderOutputMono(ch1_smp.s1_out, ch1.level, ch1.pan, ch1.send1, ch1.send2);
        }
    }

    // T2 = esp_timer_get_time();
    // ch1_render_time = T2 - T;

    ch2.PreProcess(idata);
    if (ch2.enabled) {
        ch2_fmb1.Process(idata);
        if (ch2_fmb1.enabled) {
            mixRenderOutputMono(ch2_fmb1.out, ch2.level, ch2.pan, ch2.send1, ch2.send2);
        }

        ch2_smp.track_length = ch2.track_length;
        ch2_smp.Process(idata);
        if (ch2_smp.enabled) {
            mixRenderOutputMono(ch2_smp.s1_out, ch2.level, ch2.pan, ch2.send1, ch2.send2);
        }
    }

    // T = esp_timer_get_time();
    // ch2_render_time = T - T2;

    ch3.PreProcess(idata);
    if (ch3.enabled) {
        ch3_ds.Process(idata);
        if (ch3_ds.enabled) {
            mixRenderOutputMono(ch3_ds.out, ch3.level, ch3.pan, ch3.send1, ch3.send2);
        }

        ch3_as.Process(idata);
        if (ch3_as.enabled) {
            mixRenderOutputMono(ch3_as.out, ch3.level, ch3.pan, ch3.send1, ch3.send2);
        }

        ch3_smp.track_length = ch3.track_length;
        ch3_smp.Process(idata);
        if (ch3_smp.enabled) {
            mixRenderOutputMono(ch3_smp.s1_out, ch3.level, ch3.pan, ch3.send1, ch3.send2);
        }
    }

    // T2 = esp_timer_get_time();
    // ch3_render_time = T2 - T;

    ch4.PreProcess(idata);
    if (ch4.enabled) {
        ch4_hh1.Process(idata);
        if (ch4_hh1.enabled) {
            mixRenderOutputMono(ch4_hh1.out, ch4.level, ch4.pan, ch4.send1, ch4.send2);
        }

        ch4_hh2.Process(idata);
        if (ch4_hh2.enabled) {
            mixRenderOutputMono(ch4_hh2.out, ch4.level, ch4.pan, ch4.send1, ch4.send2);
        }

        ch4_smp.track_length = ch4.track_length;
        ch4_smp.Process(idata);
        if (ch4_smp.enabled) {
            mixRenderOutputMono(ch4_smp.s1_out, ch4.level, ch4.pan, ch4.send1, ch4.send2);
        }
    }

    // T = esp_timer_get_time();
    // ch4_render_time = T - T2;

    ch5.PreProcess(idata);
    if (ch5.enabled) {
        ch5_rs.Process(idata);
        if (ch5_rs.enabled) {
            mixRenderOutputMono(ch5_rs.rs_out, ch5.level, ch5.pan, ch5.send1, ch5.send2);
        }

        ch5_smp.track_length = ch5.track_length;
        ch5_smp.Process(idata);
        if (ch5_smp.enabled) {
            mixRenderOutputMono(ch5_smp.s1_out, ch5.level, ch5.pan, ch5.send1, ch5.send2);
        }
    }

    // T2 = esp_timer_get_time();
    // ch5_render_time = T2 - T;

    ch6.PreProcess(idata);
    if (ch6.enabled) {
        ch6_cl.Process(idata);
        if (ch6_cl.enabled) {
            mixRenderOutputMono(ch6_cl.out, ch6.level, ch6.pan, ch6.send1, ch6.send2);
        }

        ch6_smp.track_length = ch6.track_length;
        ch6_smp.Process(idata);
        if (ch6_smp.enabled) {
            mixRenderOutputMono(ch6_smp.s1_out, ch6.level, ch6.pan, ch6.send1, ch6.send2);
        }
    }

    // T = esp_timer_get_time();
    // ch6_render_time = T - T2;

    ch7.PreProcess(idata);
    if (ch7.enabled) {
        ch7_smp.track_length = ch7.track_length;
        ch7_smp.Process(idata);
        if (ch7_smp.enabled) {
            mixRenderOutputMono(ch7_smp.s1_out, ch7.level, ch7.pan, ch7.send1, ch7.send2);
        }
    }

    // T2 = esp_timer_get_time();
    // ch7_render_time = T2 - T;

    ch8.PreProcess(idata);
    if (ch8.enabled) {
        ch8_smp.track_length = ch8.track_length;
        ch8_smp.Process(idata);
        if (ch8_smp.enabled) {
            mixRenderOutputMono(ch8_smp.s1_out, ch8.level, ch8.pan, ch8.send1, ch8.send2);
        }
    }

    // T = esp_timer_get_time();
    // ch8_render_time = T - T2;

    ch9.PreProcess(idata);
    if (ch9.enabled) {
        ch9_td3.Process(idata);
        if (ch9_td3.enabled) {
            mixRenderOutputMono(ch9_td3.td3_out, ch9.level, ch9.pan, ch9.send1, ch9.send2);
        }

        ch9_smp.track_length = ch9.track_length;
        ch9_smp.Process(idata);
        if (ch9_smp.enabled) {
            mixRenderOutputMono(ch9_smp.s1_out, ch9.level, ch9.pan, ch9.send1, ch9.send2);
        }
    }

    // T2 = esp_timer_get_time();
    // ch9_render_time = T2 - T;

    ch10.PreProcess(idata);
    if (ch10.enabled) {
        ch10_td3.Process(idata);
        if (ch10_td3.enabled) {
            mixRenderOutputMono(ch10_td3.td3_out, ch10.level, ch10.pan, ch10.send1, ch10.send2);
        }

        ch10_smp.track_length = ch10.track_length;
        ch10_smp.Process(idata);
        if (ch10_smp.enabled) {
            mixRenderOutputMono(ch10_smp.s1_out, ch10.level, ch10.pan, ch10.send1, ch10.send2);
        }
    }

    // T = esp_timer_get_time();
    // ch10_render_time = T - T2;

    ch11.PreProcess(idata);
    if (ch11.enabled) {
        ch11_mo.Process(idata);
        if (ch11_mo.enabled) {
            mixRenderOutputMono(ch11_mo.mo_out, ch11.level, ch11.pan, ch11.send1, ch11.send2);
        }

        ch11_smp.track_length = ch11.track_length;
        ch11_smp.Process(idata);
        if (ch11_smp.enabled) {
            mixRenderOutputMono(ch11_smp.s1_out, ch11.level, ch11.pan, ch11.send1, ch11.send2);
        }
    }

    // T2 = esp_timer_get_time();
    // ch11_render_time = T2 - T;

    ch12.PreProcess(idata);
    if (ch12.enabled) {
        ch12_wtosc.Process(idata);
        if (ch12_wtosc.enabled) {
            mixRenderOutputMono(ch12_wtosc.out, ch12.level, ch12.pan, ch12.send1, ch12.send2);
        }

        ch12_mo.Process(idata);
        if (ch12_mo.enabled) {
            mixRenderOutputMono(ch12_mo.mo_out, ch12.level, ch12.pan, ch12.send1, ch12.send2);
        }

        ch12_tbd.Process(idata);
        if (ch12_tbd.enabled) {
            mixRenderOutputStereo(ch12_tbd.tbd_out_stereo, ch12.level, ch12.pan, ch12.send1, ch12.send2);
        }

        ch12_aits.Process(idata);
        if (ch12_aits.enabled) {
            mixRenderOutputStereo(ch12_aits.aits_out_stereo, ch12.level, ch12.pan, ch12.send1, ch12.send2);
        }

        ch12_smp.track_length = ch12.track_length;
        ch12_smp.Process(idata);
        if (ch12_smp.enabled) {
            mixRenderOutputMono(ch12_smp.s1_out, ch12.level, ch12.pan, ch12.send1, ch12.send2);
        }
    }

    // T = esp_timer_get_time();
    // ch12_render_time = T - T2;

    ch13.PreProcess(idata);
    if (ch13.enabled) {
        ch13_smp.track_length = ch13.track_length;
        ch13_smp.Process(idata);
        if (ch13_smp.enabled) {
            mixRenderOutputMono(ch13_smp.s1_out, ch13.level, ch13.pan, ch13.send1, ch13.send2);
        }
    }

    // T2 = esp_timer_get_time();
    // ch13_render_time = T2 - T;

    ch14.PreProcess(idata);
    if (ch14.enabled) {
        ch14_smp.track_length = ch14.track_length;
        ch14_smp.Process(idata);
        if (ch14_smp.enabled) {
            mixRenderOutputMono(ch14_smp.s1_out, ch14.level, ch14.pan, ch14.send1, ch14.send2);
        }
    }

    // T = esp_timer_get_time();
    // ch14_render_time = T - T2;

    ch15.PreProcess(idata);
    if (ch15.enabled) {
        ch15_pp.Process(idata);
        if (ch15_pp.enabled) {
            mixRenderOutputStereo(ch15_pp.pp_out_stereo, ch15.level, ch15.pan, ch15.send1, ch15.send2);
        }

        ch15_tbd.Process(idata);
        if (ch15_tbd.enabled) {
            mixRenderOutputStereo(ch15_tbd.tbd_out_stereo, ch15.level, ch15.pan, ch15.send1, ch15.send2);
        }

        ch15_smp.track_length = ch15.track_length;
        ch15_smp.Process(idata);
        if (ch15_smp.enabled) {
            mixRenderOutputMono(ch15_smp.s1_out, ch15.level, ch15.pan, ch15.send1, ch15.send2);
        }
    }

    // T2 = esp_timer_get_time();
    // ch15_render_time = T2 - T;

    // Synth-only peak — combined_out at this point holds (ch16 + every
    // synth track). Subtract the ch16 snapshot taken right after ch16
    // mixed; what remains is purely the synth tracks' contribution to
    // the bus. Used by the OLED Output meter so input audio routed
    // through ch16 never bleeds into it.
    {
        float p = 0.f;
        for (int i = 0; i < bufSz * 2; i++) {
            float diff = combined_out[i] - ch16_combined_snapshot[i];
            float a = diff < 0 ? -diff : diff;
            if (a > p) p = a;
        }
        float prev = g_peakSynthOnly;
        g_peakSynthOnly = (p > prev) ? p : (0.85f * prev + 0.15f * p);
    }

    // Process effects
    preprocessFX1(data); // delay

    // T = esp_timer_get_time();
    // fx_delay_render_time = T - T2;

    preprocessFX2(data); // reverb

    // T2 = esp_timer_get_time();
    // fx_reverb_render_time = T2 - T;

    preprocessMaster(data); // sum compressor

    // T = esp_timer_get_time();
    // fx_master_render_time = T - T2;

    MK_BOOL_PAR_NOCV(bSumMute, sum_mute)
    if (bSumMute){
        memset(data.buf, 0, bufSz * 2 * sizeof(float));
        return;
    }

    // T = esp_timer_get_time();
    // int64_t Ttotal = T - Tstart;

    renderMasterOutput(data);

    // if audio somehow ends up in NaN, reset buffers...
    if (data.buf[0] != data.buf[0]) {
        std::fill_n(data.buf, bufSz * 2, 0.f);
        std::fill_n(combined_out, bufSz * 2, 0.f);
        std::fill_n(send1_out, bufSz * 2, 0.f);
        std::fill_n(send2_out, bufSz * 2, 0.f);
        std::fill_n(delayBuffer_l, delayBufferSizeMax, 0.f);
        std::fill_n(delayBuffer_r, delayBufferSizeMax, 0.f);
        std::fill_n(reverbBuffer, 32768, 0.f);
        std::fill_n(preDelayBuf, preDelayBufSize, 0.f);
    }

    // if (framecounter % 5000 == 0) {
    //     printf("GrooveBoxRack CPU time %d uS\n", (int)Ttotal);
        // printf("GrooveBoxRack CPU times (us): Ch1:%d Ch2:%d Ch3:%d Ch4:%d Ch5:%d Ch6:%d Ch7:%d Ch8:%d FX1:%d FX2:%d Master:%d\n",
        // (int)ch1_render_time, (int)ch2_render_time, (int)ch3_render_time, (int)ch4_render_time,
        // (int)ch5_render_time, (int)ch6_render_time, (int)ch7_render_time, (int)ch8_render_time,
        // (int)fx_delay_render_time, (int)fx_reverb_render_time, (int)fx_master_render_time);
        // } else if (framecounter % 5000 == 2500) {
        // printf("GrooveBoxRack CPU times (us): Ch9:%d Ch10:%d Ch11:%d Ch12:%d Ch13:%d Ch14:%d Ch15:%d Ch16:%d FX1:%d FX2:%d Master:%d\n",
        // (int)ch9_render_time, (int)ch10_render_time, (int)ch11_render_time, (int)ch12_render_time,
        // (int)ch13_render_time, (int)ch14_render_time, (int)ch15_render_time, (int)ch16_render_time,
        // (int)fx_delay_render_time, (int)fx_reverb_render_time, (int)fx_master_render_time);
    // }
}

// =====================================================================================
// [3] PARAM REGISTRATION — rack-machine voices call registerParamAndCC() from their
//     Init().  We register each param in BOTH the name map (pMapPar: presets + WebUI
//     param-edits) and the CC map (pMapParCC: MIDI control changes via the rack's
//     handleMidiControlChange()).
// =====================================================================================

void ctagSoundProcessorGrooveBoxRack::registerParamAndCC(const GrooveBoxRackInitData *initdata, const char *suffix, int cc, function<GrooveBoxRackParamSetter> setter) {
    // Register by full id ("<prefix><suffix>", e.g. "ch1_db_f0") in the name map so that
    // LoadPreset() and the WebUI's setParam path actually reach the DSP (ctagSoundProcessor
    // walks pMapPar). Previously only the CC map was populated, so presets/knob edits were
    // silently ignored and every parameter sat at its (zero-initialised) default.
    string fullId = string(initdata->prefix) + string(suffix);
    pMapPar[fullId] = setter;
    uint16_t key = CC_TO_MAP_KEY(initdata->midi_channel, initdata->cc_base + cc);
    pMapParCC.emplace(key, PsramVector<function<void(const int)>>());
    pMapParCC[key].push_back(setter);
}

void ctagSoundProcessorGrooveBoxRack::handleMidiControlChange(const uint8_t channel, const uint8_t control, const uint8_t value) {
    int32_t cv_value = ((int32_t)value * 4096) / 128;
    int key = CC_TO_MAP_KEY(channel, control);

    auto it = pMapParCC.find(key);
    if (it != pMapParCC.end()) {
        // ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "MIDI: CC %d, %d, %d (cv %d) (Set)", channel, control, value, cv_value);
        for(auto& listener : it->second){
            listener(cv_value);
        }
    // } else {
    // ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "MIDI: CC %d, %d, %d (Unhandled)", channel, control, value);
    }
};

void ctagSoundProcessorGrooveBoxRack::handleMidiControlChangeNRPM(const uint8_t channel, const uint8_t control, const uint16_t value) {
    int32_t cv_value = ((int32_t)value * 4096) / 16384;
    int key = CC_TO_MAP_KEY(channel, control);

    auto it = pMapParCC.find(key);
    if (it != pMapParCC.end()) {
        // ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "MIDI: nrpm CC %d, %d, %d (cv %d) (Set)", channel, control, value, cv_value);
        for(auto& listener : it->second){
            listener(cv_value);
        }
    // } else {
    // ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "MIDI: nrpm CC %d, %d, %d (Unhandled)", channel, control, value);
    }
};

static void dumpMemoryUsage() {
    uint32_t freeSize = esp_get_free_heap_size();
	printf("The available total size of heap:%" PRIu32 "\n", freeSize);

	printf("\tDescription\tInternal\tSPIRAM\n");
	printf("Current Free Memory\t%d\t\t%d\n",
			heap_caps_get_free_size(MALLOC_CAP_8BIT) - heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
			heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
	printf("Largest Free Block\t%d\t\t%d\n",
			heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
			heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
	printf("Min. Ever Free Size\t%d\t\t%d\n",
			heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
			heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
}

// =====================================================================================
// [4] INIT — called by the factory after Create().  Walks each track, wires per-track
//     RackChannelMixer + the machines that can run on it (db/ab/ro/…), allocates the
//     delay + reverb buffers, then LoadPreset(0) so the data model is populated and
//     every chN_device param can fire setTrackMachineByDeviceValue() on the way in.
//
//     knowYourself() (further down) registers the global FX / master params with
//     DEFINE_GLOBAL_PARAM; per-track / per-machine params are registered by each
//     machine's Init() via registerParamAndCC().
// =====================================================================================

void ctagSoundProcessorGrooveBoxRack::Init(std::size_t blockSize, void* blockPtr){
    // construct internal data model

	printf("ctagSoundProcessorGrooveBoxRack::Init(%zu, %x)\n", blockSize, (uintptr_t) blockPtr);

    dumpMemoryUsage();

    ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "Before know yourself");
    knowYourself();
    ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "After know yourself");

    framecounter = 0;

    GrooveBoxRackInitData dri;
    dri.rack = this;
    dri.sampleRom = &sampleRom;

    // ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "Dummy -2");

    dri.track_index = 0;
    dri.midi_channel = 9;
    dri.cc_base = 0;
    dri.prefix = "ch1_"; ch1.Init(&dri);
    dri.prefix = "ch1_db_"; ch1_db.Init(&dri);
    dri.prefix = "ch1_ab_"; ch1_ab.Init(&dri);
    dri.prefix = "ch1_smp_"; ch1_smp.Init(&dri);
    ch1_render_time = 0;

    // ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "Dummy 0");
    // dumpMemoryUsage();

    dri.track_index = 1;
    dri.midi_channel = 9;
    dri.cc_base = 40;
    dri.prefix = "ch2_"; ch2.Init(&dri);
    dri.prefix = "ch2_fmb1_"; ch2_fmb1.Init(&dri);
    dri.prefix = "ch2_smp_"; ch2_smp.Init(&dri);
    ch2_render_time = 0;

    // ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "Dummy 1");
    // dumpMemoryUsage();

    dri.track_index = 2;
    dri.midi_channel = 9;
    dri.cc_base = 80;
    dri.prefix = "ch3_"; ch3.Init(&dri);
    dri.prefix = "ch3_ds_"; ch3_ds.Init(&dri);
    dri.prefix = "ch3_as_"; ch3_as.Init(&dri);
    dri.prefix = "ch3_smp_"; ch3_smp.Init(&dri);
    ch3_render_time = 0;

    // dumpMemoryUsage();

    dri.track_index = 3;
    dri.midi_channel = 10;
    dri.cc_base = 0;
    dri.prefix = "ch4_"; ch4.Init(&dri);
    dri.prefix = "ch4_hh1_"; ch4_hh1.Init(&dri);
    dri.prefix = "ch4_hh2_"; ch4_hh2.Init(&dri);
    dri.prefix = "ch4_smp_"; ch4_smp.Init(&dri);
    ch4_render_time = 0;

    // ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "Dummy 2");
    // dumpMemoryUsage();

    dri.track_index = 4;
    dri.midi_channel = 10;
    dri.cc_base = 40;
    dri.prefix = "ch5_"; ch5.Init(&dri);
    dri.prefix = "ch5_rs_"; ch5_rs.Init(&dri);
    dri.prefix = "ch5_smp_"; ch5_smp.Init(&dri);
    ch5_render_time = 0;

    // dumpMemoryUsage();

    dri.track_index = 5;
    dri.midi_channel = 10;
    dri.cc_base = 80;
    dri.prefix = "ch6_"; ch6.Init(&dri);
    dri.prefix = "ch6_cl_"; ch6_cl.Init(&dri);
    dri.prefix = "ch6_smp_"; ch6_smp.Init(&dri);
    ch6_render_time = 0;

    // ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "Dummy 3");
    // dumpMemoryUsage();

    dri.track_index = 6;
    dri.midi_channel = 11;
    dri.cc_base = 0;
    dri.prefix = "ch7_"; ch7.Init(&dri);
    dri.prefix = "ch7_smp_"; ch7_smp.Init(&dri);
    ch7_render_time = 0;

    // dumpMemoryUsage();

    dri.track_index = 7;
    dri.midi_channel = 11;
    dri.cc_base = 40;
    dri.prefix = "ch8_"; ch8.Init(&dri);
    dri.prefix = "ch8_smp_"; ch8_smp.Init(&dri);
    ch8_render_time = 0;

    // ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "Dummy 4");
    // dumpMemoryUsage();

    dri.track_index = 8;
    dri.midi_channel = 0;
    dri.cc_base = 0;
    dri.prefix = "ch9_"; ch9.Init(&dri);
    dri.prefix = "ch9_tbd03_"; ch9_td3.Init(&dri);
    dri.prefix = "ch9_smp_"; ch9_smp.Init(&dri);
    ch9_render_time = 0;

    // dumpMemoryUsage();

    dri.track_index = 9;
    dri.midi_channel = 1;
    dri.cc_base = 0;
    dri.prefix = "ch10_"; ch10.Init(&dri);
    dri.prefix = "ch10_tbd03_"; ch10_td3.Init(&dri);
    dri.prefix = "ch10_smp_"; ch10_smp.Init(&dri);
    ch10_render_time = 0;

    // ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "Dummy 5");
    // dumpMemoryUsage();

    dri.track_index = 10;
    dri.midi_channel = 2;
    dri.cc_base = 0;
    dri.prefix = "ch11_"; ch11.Init(&dri);
    dri.prefix = "ch11_mo_"; ch11_mo.Init(&dri);
    dri.prefix = "ch11_smp_"; ch11_smp.Init(&dri);
    ch11_render_time = 0;
    // dumpMemoryUsage();

    dri.track_index = 11;
    dri.midi_channel = 3;
    dri.cc_base = 0;
    dri.prefix = "ch12_"; ch12.Init(&dri);
    dri.prefix = "ch12_wtosc_"; ch12_wtosc.Init(&dri);
    dri.prefix = "ch12_mo_"; ch12_mo.Init(&dri);
    dri.prefix = "ch12_tbd_"; ch12_tbd.Init(&dri);
    dri.prefix = "ch12_aits_"; ch12_aits.Init(&dri);
    dri.prefix = "ch12_smp_"; ch12_smp.Init(&dri);
    ch12_render_time = 0;

    // ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "Dummy 6");
    // dumpMemoryUsage();

    dri.track_index = 12;
    dri.midi_channel = 4;
    dri.cc_base = 0;
    dri.prefix = "ch13_"; ch13.Init(&dri);
    dri.prefix = "ch13_smp_"; ch13_smp.Init(&dri);
    ch13_render_time = 0;
    // dumpMemoryUsage();

    dri.track_index = 13;
    dri.midi_channel = 5;
    dri.cc_base = 0;
    dri.prefix = "ch14_"; ch14.Init(&dri);
    dri.prefix = "ch14_smp_"; ch14_smp.Init(&dri);
    ch14_render_time = 0;

    // ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "Dummy 7");

    dri.track_index = 14;
    dri.midi_channel = 6;
    dri.cc_base = 0;
    dri.prefix = "ch15_"; ch15.Init(&dri);
    dri.prefix = "ch15_pp_"; ch15_pp.Init(&dri);
    dri.prefix = "ch15_tbd_"; ch15_tbd.Init(&dri);
    dri.prefix = "ch15_smp_"; ch15_smp.Init(&dri);
    ch15_render_time = 0;
    // dumpMemoryUsage();

    dri.track_index = 15;
    dri.midi_channel = 7;
    dri.cc_base = 0;
    dri.prefix = "ch16_"; ch16.Init(&dri);
    ch16.level = 0;
    dri.prefix = "ch16_in_"; ch16_in.Init(&dri); // audio input, no prefix
    ch16_render_time = 0;

    // ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "Dummy 8");
    // dumpMemoryUsage();

    dri.prefix = "fx1_";
    fx_delay.Init(&dri);
    fx_delay_render_time = 0;

    dri.prefix = "fx2_";
    fx_reverb.Init(&dri);
    fx_reverb_render_time = 0;

    dri.prefix = "mmm_";
    fx_master.Init(&dri);
    fx_master_render_time = 0;

    // print out some stats.
    ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "DrumRack: number of parameters registered %d", pMapPar.size());
    ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "DrumRack: number of CC's registered %d", pMapParCC.size());
    // ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "DrumRack: number of macro CC's registered %d", pMapMacroParCC.size());
    dumpMemoryUsage();

    // Build the voice registry — single source of truth for setTrackMachine /
    // setTrackMachineByDeviceValue / handleMidiNoteOn / handleMidiNoteOff dispatch.
    // Must run AFTER every chN.Init() above (the registry captures pointers / lambdas
    // bound to voice members) and BEFORE LoadPreset(0) (which fires
    // setTrackMachineByDeviceValue → setTrackMachine via the preset's chN_device).
    buildVoiceRegistry();

    // Create the preset/parameter data model and load preset 0 — same as DrumRack.
    // (This must happen unconditionally: the host calls LoadPreset() right after
    //  Init(), and a missing model would null-deref. See ctagSoundProcessor::LoadPreset.)
    // Loading the preset also applies "chN_device" → setTrackMachineByDeviceValue() →
    // setTrackMachine(), so every track comes up with its first machine assigned (the
    // device's macro/RP2350 layer reassigns them afterwards and is authoritative there;
    // the simulator has no macro layer, so the preset's chN_device is what it runs with).
    model = std::make_unique<ctagSPDataModel>(id, isStereo);
    LoadPreset(0);
    // (sim-only sane master/FX defaults are applied via the loadPresetInternal() override
    //  below — that way they survive every LoadPreset() call the host might make later.)

    // delay
    delayBuffer_l = static_cast<float*>(heap_caps_malloc(delayBufferSizeMax * sizeof(float), MALLOC_CAP_SPIRAM));
    ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "Allocate: delayBuffer_l=0x%x", (unsigned int)(uintptr_t)delayBuffer_l);
    assert(delayBuffer_l != nullptr);
    std::fill_n(delayBuffer_l, delayBufferSizeMax, 0.f);

    delayBuffer_r = static_cast<float*>(heap_caps_malloc(delayBufferSizeMax * sizeof(float), MALLOC_CAP_SPIRAM));
    ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "Allocate: delayBuffer_r=0x%x", (unsigned int)(uintptr_t)delayBuffer_r);
    assert(delayBuffer_r != nullptr);
    std::fill_n(delayBuffer_r, delayBufferSizeMax, 0.f);

    // reverb
    reverbBuffer = static_cast<float*>(heap_caps_malloc(32768 * sizeof(float), MALLOC_CAP_SPIRAM));
    ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "Allocate: reverbBuffer=0x%x", (unsigned int)(uintptr_t)reverbBuffer);
    assert(reverbBuffer != nullptr);
    std::fill_n(reverbBuffer, 32768, 0.f);

    // Pre-delay ring buffer ahead of the reverb tank.
    preDelayBuf = static_cast<float*>(heap_caps_malloc(preDelayBufSize * sizeof(float), MALLOC_CAP_SPIRAM));
    ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "Allocate: preDelayBuf=0x%x", (unsigned int)(uintptr_t)preDelayBuf);
    assert(preDelayBuf != nullptr);
    std::fill_n(preDelayBuf, preDelayBufSize, 0.f);
    preDelayWriteIdx = 0;

    // assert(blockSize >= 32768 * 4);
    reverb.Init(reverbBuffer); // requires 32768*4 bytes = 128KB
    reverb.Clear();
    // blockPtr = static_cast<void*>(static_cast<uint8_t*>(blockPtr) + 32768 * 4);
    // blockSize -= 32768 * 4;
    reverb.set_diffusion(0.7f);
    reverb.set_input_gain(.5f); // left and right are summed
    reverb.set_amount(1.f);
    reverb.set_lp(0.5f);
    reverb.set_time(0.4f);
    // Hardcoded LFO frequencies match upstream DrumRack::Init. Modulation
    // depth in mifx::Reverb is fixed by the delay-line buffer sizes, so a
    // user-controllable ModRate knob has no useful range — knob retired.
    reverb.set_lfo1_freq(0.5f);
    reverb.set_lfo2_freq(0.3f);

    // init compressor
    sumCompressor.setSampleRate(44100.f);
    sumCompressor.initRuntime();

    ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "After Init()");
    dumpMemoryUsage();
}

ctagSoundProcessorGrooveBoxRack::~ctagSoundProcessorGrooveBoxRack(){
}

// =====================================================================================
// [4b] VOICE REGISTRY — single source of truth for which (trackIndex × machineId) pairs
//      exist and how each voice reacts to a (channel × note × velocity) MIDI event.
//
//      buildVoiceRegistry() runs once at the end of Init(), before LoadPreset(0).  The
//      lambdas it builds capture the rack's per-track voice objects (RackDBD, RackTBD03,
//      etc.) by reference — those voices are *this*-members, so they outlive the registry.
//
//      Order matters: setTrackMachineByDeviceValue() buckets the chN_device 0..4095
//      param across the entries it finds for a given trackIndex, in registration order.
//      The registration order below mirrors the old switch's pick({"db","ab","ro"}, …)
//      lists exactly so the bucket index stays byte-identical.  DO NOT permute without
//      re-running simulator/build/routing-test to validate against the golden.
//
//      Adding a new rack voice (e.g. an FM tom) is now a single block in this function
//      plus a member in the .hpp — no more touching three separate switch bodies.
// =====================================================================================
void ctagSoundProcessorGrooveBoxRack::buildVoiceRegistry() {
    // Hook up the per-track index arrays — fast-path lookups for setTrackMachine /
    // setTrackBank that don't want to scan the registry.
    trackMixers   = { &ch1,  &ch2,  &ch3,  &ch4,  &ch5,  &ch6,  &ch7,  &ch8,
                      &ch9,  &ch10, &ch11, &ch12, &ch13, &ch14, &ch15, &ch16 };
    trackSamplers = { &ch1_smp,  &ch2_smp,  &ch3_smp,  &ch4_smp,
                      &ch5_smp,  &ch6_smp,  &ch7_smp,  &ch8_smp,
                      &ch9_smp,  &ch10_smp, &ch11_smp, &ch12_smp,
                      &ch13_smp, &ch14_smp, &ch15_smp, nullptr /* ch16: audio input, no sampler */ };

    voiceRegistry.clear();
    voiceRegistry.reserve(40);  // ~16 tracks × ~2.4 voices avg = ~38 entries

    // Helper builders — pick the right (noteOn, noteOff) pair for the voice kind.
    //   drumTrig   : fires .trigger() on velocity>0; noteOff is a no-op
    //   drumRom    : rompler on a drum channel — always uses fixed note 36
    //   synthOn    : pitched voice on a synth channel — passes incoming note through
    // The trigger() / noteOn() / noteOff() methods are NOT virtual, so each lambda
    // captures the exact concrete voice — there's no v-table lookup at dispatch time.
    auto addDrumTrig = [&](uint8_t track, const char* id, bool* en, uint8_t channel,
                            uint8_t triggerNote, std::function<void()> trig) {
        voiceRegistry.push_back({ track, id, en,
            static_cast<int16_t>(channel), static_cast<int16_t>(triggerNote),
            [trig](uint8_t /*n*/, uint8_t v) { if (v > 0) trig(); },
            {} /* drum voices have no noteOff */ });
    };
    auto addDrumRom = [&](uint8_t track, const char* id, bool* en, uint8_t channel,
                           uint8_t triggerNote, RackRompler* smp) {
        voiceRegistry.push_back({ track, id, en,
            static_cast<int16_t>(channel), static_cast<int16_t>(triggerNote),
            [smp](uint8_t /*n*/, uint8_t v) {
                if (v > 0) smp->noteOn(36, v); else smp->noteOff(36, 0);
            },
            [smp](uint8_t /*n*/, uint8_t /*v*/) { smp->noteOff(36, 0); } });
    };
    auto addSynth = [&](uint8_t track, const char* id, bool* en, uint8_t channel,
                         std::function<void(uint8_t,uint8_t)> on,
                         std::function<void(uint8_t,uint8_t)> off) {
        voiceRegistry.push_back({ track, id, en,
            static_cast<int16_t>(channel), int16_t(-1),
            std::move(on), std::move(off) });
    };
    // No-MIDI-routing entries (e.g. ch16_in audio input): channel = -1, no callbacks.
    // They still appear in the registry so setTrackMachine can find them by id.
    auto addNoMidi = [&](uint8_t track, const char* id, bool* en) {
        voiceRegistry.push_back({ track, id, en, int16_t(-1), int16_t(-1), {}, {} });
    };

    // ---- Track 0 (ch1) — drum channel 9, note 36 — db / ab / ro ----------------------
    addDrumTrig(0, "db", &ch1_db.enabled, 9, 36, [this]() { ch1_db.trigger(); });
    addDrumTrig(0, "ab", &ch1_ab.enabled, 9, 36, [this]() { ch1_ab.trigger(); });
    addDrumRom (0, "ro", &ch1_smp.enabled, 9, 36, &ch1_smp);

    // rackgen:registry-track-0 — auto-inserted voices for track 0 go above this line

    // ---- Track 1 (ch2) — drum channel 9, note 37 — fmb / ro ---------------------------
    addDrumTrig(1, "fmb", &ch2_fmb1.enabled, 9, 37, [this]() { ch2_fmb1.trigger(); });
    addDrumRom (1, "ro",  &ch2_smp.enabled,  9, 37, &ch2_smp);

    // rackgen:registry-track-1 — auto-inserted voices for track 1 go above this line

    // ---- Track 2 (ch3) — drum channel 9, note 38 — ds / as / ro -----------------------
    addDrumTrig(2, "ds", &ch3_ds.enabled, 9, 38, [this]() { ch3_ds.trigger(); });
    addDrumTrig(2, "as", &ch3_as.enabled, 9, 38, [this]() { ch3_as.trigger(); });
    addDrumRom (2, "ro", &ch3_smp.enabled, 9, 38, &ch3_smp);

    // rackgen:registry-track-2 — auto-inserted voices for track 2 go above this line

    // ---- Track 3 (ch4) — drum channel 10, note 36 — hh1 / hh2 / ro --------------------
    addDrumTrig(3, "hh1", &ch4_hh1.enabled, 10, 36, [this]() { ch4_hh1.trigger(); });
    addDrumTrig(3, "hh2", &ch4_hh2.enabled, 10, 36, [this]() { ch4_hh2.trigger(); });
    addDrumRom (3, "ro",  &ch4_smp.enabled, 10, 36, &ch4_smp);

    // rackgen:registry-track-3 — auto-inserted voices for track 3 go above this line

    // ---- Track 4 (ch5) — drum channel 10, note 37 — rs / ro ---------------------------
    addDrumTrig(4, "rs", &ch5_rs.enabled, 10, 37, [this]() { ch5_rs.trigger(); });
    addDrumRom (4, "ro", &ch5_smp.enabled, 10, 37, &ch5_smp);

    // rackgen:registry-track-4 — auto-inserted voices for track 4 go above this line

    // ---- Track 5 (ch6) — drum channel 10, note 38 — cl / ro ---------------------------
    addDrumTrig(5, "cl", &ch6_cl.enabled, 10, 38, [this]() { ch6_cl.trigger(); });
    addDrumRom (5, "ro", &ch6_smp.enabled, 10, 38, &ch6_smp);

    // rackgen:registry-track-5 — auto-inserted voices for track 5 go above this line

    // ---- Track 6 (ch7) — drum channel 11, note 36 — ro only ---------------------------
    addDrumRom (6, "ro", &ch7_smp.enabled, 11, 36, &ch7_smp);

    // rackgen:registry-track-6 — auto-inserted voices for track 6 go above this line

    // ---- Track 7 (ch8) — drum channel 11, note 37 — ro only ---------------------------
    addDrumRom (7, "ro", &ch8_smp.enabled, 11, 37, &ch8_smp);

    // rackgen:registry-track-7 — auto-inserted voices for track 7 go above this line

    // ---- Track 8 (ch9) — synth channel 0 — td3 / ro -----------------------------------
    addSynth(8, "td3", &ch9_td3.enabled, 0,
        [this](uint8_t n, uint8_t v) { if (v > 0) ch9_td3.noteOn(n, v); else ch9_td3.noteOff(n, 0); },
        [this](uint8_t n, uint8_t /*v*/) { ch9_td3.noteOff(n, 0); });
    addSynth(8, "ro",  &ch9_smp.enabled, 0,
        [this](uint8_t n, uint8_t v) { if (v > 0) ch9_smp.noteOn(n, v); else ch9_smp.noteOff(n, 0); },
        [this](uint8_t n, uint8_t /*v*/) { ch9_smp.noteOff(n, 0); });

    // rackgen:registry-track-8 — auto-inserted voices for track 8 go above this line

    // ---- Track 9 (ch10) — synth channel 1 — td3 / ro ----------------------------------
    addSynth(9, "td3", &ch10_td3.enabled, 1,
        [this](uint8_t n, uint8_t v) { if (v > 0) ch10_td3.noteOn(n, v); else ch10_td3.noteOff(n, 0); },
        [this](uint8_t n, uint8_t /*v*/) { ch10_td3.noteOff(n, 0); });
    addSynth(9, "ro",  &ch10_smp.enabled, 1,
        [this](uint8_t n, uint8_t v) { if (v > 0) ch10_smp.noteOn(n, v); else ch10_smp.noteOff(n, 0); },
        [this](uint8_t n, uint8_t /*v*/) { ch10_smp.noteOff(n, 0); });

    // rackgen:registry-track-9 — auto-inserted voices for track 9 go above this line

    // ---- Track 10 (ch11) — synth channel 2 — mo / ro ----------------------------------
    addSynth(10, "mo", &ch11_mo.enabled, 2,
        [this](uint8_t n, uint8_t v) { if (v > 0) ch11_mo.noteOn(n, v); else ch11_mo.noteOff(n, 0); },
        [this](uint8_t n, uint8_t /*v*/) { ch11_mo.noteOff(n, 0); });
    addSynth(10, "ro", &ch11_smp.enabled, 2,
        [this](uint8_t n, uint8_t v) { if (v > 0) ch11_smp.noteOn(n, v); else ch11_smp.noteOff(n, 0); },
        [this](uint8_t n, uint8_t /*v*/) { ch11_smp.noteOff(n, 0); });

    // rackgen:registry-track-10 — auto-inserted voices for track 10 go above this line

    // ---- Track 11 (ch12) — synth channel 3 — wtosc / mo / tbd / aits / ro --------------
    addSynth(11, "wtosc", &ch12_wtosc.enabled, 3,
        [this](uint8_t n, uint8_t v) { if (v > 0) ch12_wtosc.noteOn(n, v); else ch12_wtosc.noteOff(n, 0); },
        [this](uint8_t n, uint8_t /*v*/) { ch12_wtosc.noteOff(n, 0); });
    addSynth(11, "mo",   &ch12_mo.enabled, 3,
        [this](uint8_t n, uint8_t v) { if (v > 0) ch12_mo.noteOn(n, v); else ch12_mo.noteOff(n, 0); },
        [this](uint8_t n, uint8_t /*v*/) { ch12_mo.noteOff(n, 0); });
    addSynth(11, "tbd",  &ch12_tbd.enabled, 3,
        [this](uint8_t n, uint8_t v) { if (v > 0) ch12_tbd.noteOn(n, v); else ch12_tbd.noteOff(n, 0); },
        [this](uint8_t n, uint8_t /*v*/) { ch12_tbd.noteOff(n, 0); });
    addSynth(11, "tbdait", &ch12_aits.enabled, 3,
        [this](uint8_t n, uint8_t v) { if (v > 0) ch12_aits.noteOn(n, v); else ch12_aits.noteOff(n, 0); },
        [this](uint8_t n, uint8_t /*v*/) { ch12_aits.noteOff(n, 0); });
    addSynth(11, "ro",   &ch12_smp.enabled, 3,
        [this](uint8_t n, uint8_t v) { if (v > 0) ch12_smp.noteOn(n, v); else ch12_smp.noteOff(n, 0); },
        [this](uint8_t n, uint8_t /*v*/) { ch12_smp.noteOff(n, 0); });

    // rackgen:registry-track-11 — auto-inserted voices for track 11 go above this line

    // ---- Track 12 (ch13) — synth channel 4 — ro only ----------------------------------
    addSynth(12, "ro", &ch13_smp.enabled, 4,
        [this](uint8_t n, uint8_t v) { if (v > 0) ch13_smp.noteOn(n, v); else ch13_smp.noteOff(n, 0); },
        [this](uint8_t n, uint8_t /*v*/) { ch13_smp.noteOff(n, 0); });

    // rackgen:registry-track-12 — auto-inserted voices for track 12 go above this line

    // ---- Track 13 (ch14) — synth channel 5 — ro only ----------------------------------
    addSynth(13, "ro", &ch14_smp.enabled, 5,
        [this](uint8_t n, uint8_t v) { if (v > 0) ch14_smp.noteOn(n, v); else ch14_smp.noteOff(n, 0); },
        [this](uint8_t n, uint8_t /*v*/) { ch14_smp.noteOff(n, 0); });

    // rackgen:registry-track-13 — auto-inserted voices for track 13 go above this line

    // ---- Track 14 (ch15) — synth channel 6 — pp / tbd / ro ----------------------------
    addSynth(14, "pp", &ch15_pp.enabled, 6,
        [this](uint8_t n, uint8_t v) { if (v > 0) ch15_pp.noteOn(n, v); else ch15_pp.noteOff(n, 0); },
        [this](uint8_t n, uint8_t /*v*/) { ch15_pp.noteOff(n, 0); });
    addSynth(14, "tbd", &ch15_tbd.enabled, 6,
        [this](uint8_t n, uint8_t v) { if (v > 0) ch15_tbd.noteOn(n, v); else ch15_tbd.noteOff(n, 0); },
        [this](uint8_t n, uint8_t /*v*/) { ch15_tbd.noteOff(n, 0); });
    addSynth(14, "ro", &ch15_smp.enabled, 6,
        [this](uint8_t n, uint8_t v) { if (v > 0) ch15_smp.noteOn(n, v); else ch15_smp.noteOff(n, 0); },
        [this](uint8_t n, uint8_t /*v*/) { ch15_smp.noteOff(n, 0); });

    // rackgen:registry-track-14 — auto-inserted voices for track 14 go above this line

    // ---- Track 15 (ch16) — audio input — no MIDI routing, but still registered so
    //      setTrackMachine(15, "in", …) finds the entry and flips ch16_in.enabled.
    addNoMidi(15, "in", &ch16_in.enabled);

    // rackgen:registry-track-15 — auto-inserted voices for track 15 go above this line

    ESP_LOGI("ctagSoundProcessorGrooveBoxRack",
             "buildVoiceRegistry: registered %zu voices", voiceRegistry.size());
}

#ifdef TBD_SIM
// ===================== SIMULATOR-ONLY OVERRIDE ============================================
// On the device the macro/preset (RP2350) layer overrides FX1 / FX2 / Master for the loaded
// kit; the sim has no such layer, so the values come straight from mp-GrooveBoxRack.json,
// tuned for the hardware gain staging (raw rack peak ~20×, tanh-clipped). Re-apply clean
// defaults AFTER every LoadPreset(): unity-ish master + bypassed compressor + audible FX
// returns (so a track's FX Send 1/2 in the WebUI actually produces delay/reverb). Excluded
// from the device build entirely by the #ifdef TBD_SIM guards (here and in the .hpp).
//
// Staging added new atomics that default to 0 — without re-seeding them here the sim is
// quiet:
//   fx2_input_gain = 0 → reverb tank gets zero signal in (silences the entire FX2 bus).
//   fx2_diffuse = 0    → slap echo instead of a lush diffuse tail.
//   sum_lev = old 1500 → only ~-8 dB (squared); wire 2048 is the documented unity point.
void ctagSoundProcessorGrooveBoxRack::loadPresetInternal() {
    ctagSoundProcessor::loadPresetInternal();        // apply mp-GrooveBoxRack.json normally
    // Master — wire 2048 = unity per renderMasterOutput's "wire 64 = 0 dB" Octatrack
    // convention; sum_drive = 0 keeps the SoftLimit path bypassed (bit-identical dry).
    sum_mute = 0; sum_lev = 2048; sum_drive = 0;
    // Compressor — fully bypassed (c_mix=0 → 100% dry, plus threshold pinned at max).
    c_mix = 0; c_gain = 0; c_thres = 4095; c_ratio = 0; c_lpf = 0;  // c_dly_level/c_rev_level retired
    // FX1 (delay) — audible default tail at a 1/4 note @ 120 BPM with moderate feedback.
    fx1_amount = 1500; fx1_fx_send = 0; fx1_feedback = 1000;
    fx1_time_ms = 256; fx1_sync = 0; fx1_freeze = 0; fx1_tape_digital = 0;
    fx1_st_width = 2048; fx1_base = 1024; fx1_width = 2048;
    fx1_input_hp = 0;                                 // open (≈20 Hz) — no HP on delay input
    // FX2 (reverb) — staging-new params must be primed or the tank stays silent.
    fx2_amount = 2000;                                // wet level ≈ 0.98 (×2.0 scale)
    fx2_time = 2048; fx2_lp = 2048;                   // medium tail, medium damp
    fx2_input_gain = 2048;                            // ≈0.5 — matches the old hardcoded value
    fx2_diffuse = 3017;                               // ≈0.7 — old hardcoded set_diffusion
    fx2_predelay = 0; fx2_hp = 0;                     // no predelay, open input HP
    fx2_modulation = 0; fx2_tank_level = 0;           // retired params, ignored by DSP
}
// ===================== END SIMULATOR-ONLY ================================================
#endif


// Register by both id AND CC key so LoadPreset() (walks pMapPar) and CC dispatch
// (walks pMapParCC) both reach the DSP — staging dropped the pMapPar half and
// presets / WebUI knob edits stopped working until master re-added it.
#define DEFINE_GLOBAL_PARAM(name, channel, cc, parametername) \
    pMapPar[name] = [&](const int val){ parametername = val; }; \
    pMapParCC.emplace(CC_TO_MAP_KEY(channel, cc), PsramVector<function<void(const int)>>{[&](const int val){ parametername = val;}});

void ctagSoundProcessorGrooveBoxRack::knowYourself(){
    // autogenerated code here
    // sectionCpp0

    DEFINE_GLOBAL_PARAM("fx1_time_ms", 13, 20, fx1_time_ms);
    DEFINE_GLOBAL_PARAM("fx1_sync", 13, 21, fx1_sync);
    DEFINE_GLOBAL_PARAM("fx1_freeze", 13, 22, fx1_freeze);
    DEFINE_GLOBAL_PARAM("fx1_tape_digital", 13, 23, fx1_tape_digital);
    DEFINE_GLOBAL_PARAM("fx1_st_width", 13, 24, fx1_st_width);
    DEFINE_GLOBAL_PARAM("fx1_fx_send", 13, 25, fx1_fx_send);
    DEFINE_GLOBAL_PARAM("fx1_feedback", 13, 26, fx1_feedback);
    DEFINE_GLOBAL_PARAM("fx1_base", 13, 27, fx1_base);
    DEFINE_GLOBAL_PARAM("fx1_width", 13, 28, fx1_width);
    DEFINE_GLOBAL_PARAM("fx1_amount", 13, 29, fx1_amount);

    DEFINE_GLOBAL_PARAM("fx2_time", 13, 40, fx2_time);
    DEFINE_GLOBAL_PARAM("fx2_lp", 13, 41, fx2_lp);
    DEFINE_GLOBAL_PARAM("fx2_amount", 13, 42, fx2_amount);
    DEFINE_GLOBAL_PARAM("fx2_diffuse",  13, 43, fx2_diffuse);
    DEFINE_GLOBAL_PARAM("fx2_predelay", 13, 44, fx2_predelay);
    DEFINE_GLOBAL_PARAM("fx2_modulation", 13, 45, fx2_modulation);
    DEFINE_GLOBAL_PARAM("fx2_input_gain", 13, 46, fx2_input_gain);
    DEFINE_GLOBAL_PARAM("fx2_tank_level", 13, 47, fx2_tank_level);
    DEFINE_GLOBAL_PARAM("fx1_input_hp", 13, 30, fx1_input_hp);
    DEFINE_GLOBAL_PARAM("fx2_hp",       13, 48, fx2_hp);

    DEFINE_GLOBAL_PARAM("c_thres", 13, 60, c_thres);
    DEFINE_GLOBAL_PARAM("c_ratio", 13, 61, c_ratio);
    DEFINE_GLOBAL_PARAM("c_atk", 13, 62, c_atk);
    DEFINE_GLOBAL_PARAM("c_rel", 13, 63, c_rel);
    DEFINE_GLOBAL_PARAM("c_lpf", 13, 64, c_lpf);
    DEFINE_GLOBAL_PARAM("c_gain", 13, 65, c_gain);
    DEFINE_GLOBAL_PARAM("c_mix", 13, 66, c_mix);
    // CCs 67/68 retired (c_dly_level / c_rev_level had no DSP referent).

    DEFINE_GLOBAL_PARAM("sum_mute", 13, 80, sum_mute);
    DEFINE_GLOBAL_PARAM("sum_lev", 13, 81, sum_lev);
    DEFINE_GLOBAL_PARAM("sum_drive", 13, 82, sum_drive);

    // RackTBDings — global "AIR" master (FaseAcht §4.2 single-slider behaviour).
    // Channel 13, CC 67 (was retired). Fans out to every RackTBDings slot's
    // air_blend so one knob opens all "pickups" simultaneously.
    pMapParCC.emplace(CC_TO_MAP_KEY(13, 67), PsramVector<function<void(const int)>>{[&](const int val){
        tbd_air_master = val;
        ch12_tbd.air_blend = val;
        ch15_tbd.air_blend = val;
    }});

    isStereo = true;
	id = "GrooveBoxRack";
	// sectionCpp0
}


// =====================================================================================
// [5] MIDI PARSER — splits the raw byte stream from ProcessData.midi_bytes into
//     individual status+data messages and dispatches them to handleMidiNoteOn/Off()
//     (drums + synth voices), handleMidiControlChange() (param CCs), etc.
//     On the device the bytes come from the RP2350 sequencer (SPI) and/or USB MIDI;
//     in the simulator they're injected by /ctrl-midi (see simulator/WebServer.cpp).
// =====================================================================================

void ctagSoundProcessorGrooveBoxRack::parseIncomingMidiMessages(const uint8_t *buf, const size_t len) {
    // if (len > 0) {
    //     ESP_LOGI("ctagSoundProcessorGrooveBoxRack",
    //         "parseIncomingMidiMessages: %02X %02X %02X %02X %02X %02X %02X %02X %02X (%d)",
    //             buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], buf[8], len);
    // }

    if (buf == nullptr || len < 1)  {
        return;
    }

    int left = len;
    int o = 0;

    while(left > 0) {
        uint8_t b0 = buf[o++];
        left --;

        if (b0 == 0) {
            // probably end of event stream
            return;
        }

        uint8_t channel = (b0 & 0x0F);
        uint8_t cmd = (b0 & 0xF0);

        switch(cmd) {
            case 0x80: // note off
            {
                if (left < 2) return; // not enough data

                uint8_t b1 = buf[o++];
                uint8_t b2 = buf[o++];
                left -= 2;
                handleMidiNoteOff(channel, b1, b2);
                break;
            }
            case 0x90: // note on
            {
                if (left < 2) return; // not enough data

                uint8_t b1 = buf[o++];
                uint8_t b2 = buf[o++];
                left -= 2;
                // note-on with velocity 0 == note-off (running-status MIDI convention)
                if (b2 == 0) handleMidiNoteOff(channel, b1, 0);
                else handleMidiNoteOn(channel, b1, b2);
                break;
            }
            case 0xA0: // aftertouch
            {
                if (left < 2) return; // not enough data

                uint8_t b1 = buf[o++];
                uint8_t b2 = buf[o++];
                left -= 2;
                break;
            }
            case 0xB0: // control change
            {
                if (left < 2) return; // not enough data

                uint8_t b1 = buf[o++];
                uint8_t b2 = buf[o++];
                left -= 2;
                handleMidiControlChange(channel, b1, b2);
                break;
            }
            case 0xC0: // program change
            {
                if (left < 2) return; // not enough data

                uint8_t b1 = buf[o++];
                uint8_t b2 = buf[o++]; // not used?
                left -= 2;
                break;
            }
            case 0xE0: // pitch bend
            {
                if (left < 2) return; // not enough data

                uint8_t b1 = buf[o++];
                uint8_t b2 = buf[o++];
                left -= 2;
                break;
            }
            case 0xF0: // system common / real time
                // TODO handle sysex, clock, start, stop, continue, ...
                switch(b0) {
                    case 0xF0: // sysex start
                        return; // just ignore and bail out for now

                    case 0xF8: // timing clock
                    case 0xFA: // start
                    case 0xFB: // continue
                    case 0xFC: // stop
                    case 0xFE: // active sensing
                    case 0xFF: // system reset
                        // ignore for now
                        break;
                }
                break; // fail for now
            default:
                // for anything else, just stop parsing.
                return;
        }
    }
}

// =====================================================================================
// [6] TRACK CONFIG — which machine is active on each track.
//
//   setTrackMachine(track, id, vol)       : called by the device's MacroTranslator and by
//                                            setTrackMachineByDeviceValue() in the sim.
//                                            Sets the right chN_xxx.enabled flags and
//                                            chN.volumeMultiplier; everything else (level,
//                                            pan, sends, params) comes through pMapPar.
//   setTrackMachineByDeviceValue(track,v) : the bridge from the chN_device 0..4095 param
//                                            (sent by the WebUI's machine dropdown) to a
//                                            machineId, bucketing v across the track's N
//                                            machines.  THIS IS WHERE YOU EXTEND THE TABLE
//                                            WHEN ADDING A NEW RACK MACHINE.
//   setTrackBank(track, idx)              : sampler-track bank selector.
//
// (When adding a new rack machine, also touch handleMidiNoteOn/Off below + Init above;
//  see docs/plugins/rack-plugins.rst — generators/rackgen.js prints the lines for you.)
// =====================================================================================

void ctagSoundProcessorGrooveBoxRack::setTrackMachine(const uint8_t trackIndex, const std::string machineId, float volumeMultiplier) {
    printf("GrooveBoxRack: setTrackMachine(%d, \"%s\", %f)\n", trackIndex, machineId.c_str(), volumeMultiplier);

    if (trackIndex >= 16) return;

    // Set channel mixer first — enabled iff *some* machine is assigned, plus volume.
    if (RackChannelMixer* mixer = trackMixers[trackIndex]) {
        mixer->enabled = !machineId.empty();
        mixer->volumeMultiplier = volumeMultiplier;
    }

    // Then walk the registry and flip every voice that's bound to this track:
    // each voice is "enabled" iff its registered machineId matches the requested id.
    // Empty machineId disables every voice on the track — same as the old switch's
    // implicit "no clause matched" behaviour.
    for (const auto& v : voiceRegistry) {
        if (v.trackIndex == trackIndex) {
            *v.enabledFlag = (machineId == v.machineId);
        }
    }
}

// The "chN_device" parameter (0..4095 int) is the channel's machine selector. The WebUI's
// machine dropdown buckets the option index over the full 0..4095 range — so for an N-machine
// track, the i-th option sends round(i/(N-1) * 4095), and we recover the index the same way.
// The factory preset has chN_device = 0, i.e. the first machine. On the device the macro layer
// (MacroTranslator) calls setTrackMachine() directly afterwards and is authoritative; this
// just keeps the WebUI machine dropdown working and gives a sane default.
//
// The registry walk replaces the old hand-rolled switch.  Track 15 (ch16, audio input) has
// no chN_device dropdown — we skip it early so the param can't reroute the input track.
void ctagSoundProcessorGrooveBoxRack::setTrackMachineByDeviceValue(const uint8_t trackIndex, const int deviceValue) {
    if (trackIndex >= 16) return;
    if (trackIndex == 15) return;   // ch16 = audio input — chN_device dropdown unused here

    // Collect this track's machine ids in registration order.  The old switch hard-coded
    // these lists (e.g. {"db","ab","ro"} for track 0); now they come from the registry,
    // which is the single source of truth.
    const char* ids[8] = {0};   // tracks have ≤ 3 machines today; 8 is comfortable headroom
    std::size_t n = 0;
    for (const auto& v : voiceRegistry) {
        if (v.trackIndex == trackIndex && n < sizeof(ids)/sizeof(ids[0])) {
            ids[n++] = v.machineId;
        }
    }
    if (n == 0) return;
    const char* m = nullptr;
    if (n == 1) {
        m = ids[0];
    } else {
        const int v = (deviceValue < 0) ? 0 : (deviceValue > 4095 ? 4095 : deviceValue);
        // 0 → 0, 4095 → n-1 (matches the WebUI's round(i/(n-1) * 4095))
        std::size_t idx = static_cast<std::size_t>(
            (static_cast<long>(v) * static_cast<long>(n - 1) + 2047) / 4095);
        if (idx >= n) idx = n - 1;
        m = ids[idx];
    }
    setTrackMachine(trackIndex, m, 1.f);
}

// Routing-state snapshot for the regression test.  Walks every per-track mixer and
// every voice's `enabled` flag in a fixed order and emits one "name=value" line each.
// The format is intentionally human-diffable: when the registry refactor lands, this
// dump must be byte-identical before/after (`diff -u golden.txt actual.txt` shows
// zero lines).  Adding a new rack voice requires adding one line here too — that's
// the regression net catching missed updates.
std::string ctagSoundProcessorGrooveBoxRack::GetRoutingSnapshot() const {
    std::string s;
    s.reserve(2048);
    auto emitBool = [&](const char* name, bool v) {
        s.append(name); s.append(v ? "=1\n" : "=0\n");
    };
    auto emitFloat = [&](const char* name, float v) {
        char buf[64]; std::snprintf(buf, sizeof(buf), "%s=%.6f\n", name, v);
        s.append(buf);
    };

    // Track 1
    emitBool("ch1",       ch1.enabled);    emitFloat("ch1.vm",  ch1.volumeMultiplier);
    emitBool("ch1_db",    ch1_db.enabled);
    emitBool("ch1_ab",    ch1_ab.enabled);
    emitBool("ch1_smp",   ch1_smp.enabled);
    // Track 2
    emitBool("ch2",       ch2.enabled);    emitFloat("ch2.vm",  ch2.volumeMultiplier);
    emitBool("ch2_fmb1",  ch2_fmb1.enabled);
    emitBool("ch2_smp",   ch2_smp.enabled);
    // Track 3
    emitBool("ch3",       ch3.enabled);    emitFloat("ch3.vm",  ch3.volumeMultiplier);
    emitBool("ch3_ds",    ch3_ds.enabled);
    emitBool("ch3_as",    ch3_as.enabled);
    emitBool("ch3_smp",   ch3_smp.enabled);
    // Track 4
    emitBool("ch4",       ch4.enabled);    emitFloat("ch4.vm",  ch4.volumeMultiplier);
    emitBool("ch4_hh1",   ch4_hh1.enabled);
    emitBool("ch4_hh2",   ch4_hh2.enabled);
    emitBool("ch4_smp",   ch4_smp.enabled);
    // Track 5
    emitBool("ch5",       ch5.enabled);    emitFloat("ch5.vm",  ch5.volumeMultiplier);
    emitBool("ch5_rs",    ch5_rs.enabled);
    emitBool("ch5_smp",   ch5_smp.enabled);
    // Track 6
    emitBool("ch6",       ch6.enabled);    emitFloat("ch6.vm",  ch6.volumeMultiplier);
    emitBool("ch6_cl",    ch6_cl.enabled);
    emitBool("ch6_smp",   ch6_smp.enabled);
    // Track 7 (sampler-only)
    emitBool("ch7",       ch7.enabled);    emitFloat("ch7.vm",  ch7.volumeMultiplier);
    emitBool("ch7_smp",   ch7_smp.enabled);
    // Track 8 (sampler-only)
    emitBool("ch8",       ch8.enabled);    emitFloat("ch8.vm",  ch8.volumeMultiplier);
    emitBool("ch8_smp",   ch8_smp.enabled);
    // Track 9
    emitBool("ch9",       ch9.enabled);    emitFloat("ch9.vm",  ch9.volumeMultiplier);
    emitBool("ch9_td3",   ch9_td3.enabled);
    emitBool("ch9_smp",   ch9_smp.enabled);
    // Track 10
    emitBool("ch10",      ch10.enabled);   emitFloat("ch10.vm", ch10.volumeMultiplier);
    emitBool("ch10_td3",  ch10_td3.enabled);
    emitBool("ch10_smp",  ch10_smp.enabled);
    // Track 11
    emitBool("ch11",      ch11.enabled);   emitFloat("ch11.vm", ch11.volumeMultiplier);
    emitBool("ch11_mo",   ch11_mo.enabled);
    emitBool("ch11_smp",  ch11_smp.enabled);
    // Track 12
    emitBool("ch12",      ch12.enabled);   emitFloat("ch12.vm", ch12.volumeMultiplier);
    emitBool("ch12_wtosc",ch12_wtosc.enabled);
    emitBool("ch12_mo",   ch12_mo.enabled);
    emitBool("ch12_tbd",  ch12_tbd.enabled);
    emitBool("ch12_aits", ch12_aits.enabled);
    emitBool("ch12_smp",  ch12_smp.enabled);
    // Track 13 (sampler-only)
    emitBool("ch13",      ch13.enabled);   emitFloat("ch13.vm", ch13.volumeMultiplier);
    emitBool("ch13_smp",  ch13_smp.enabled);
    // Track 14 (sampler-only)
    emitBool("ch14",      ch14.enabled);   emitFloat("ch14.vm", ch14.volumeMultiplier);
    emitBool("ch14_smp",  ch14_smp.enabled);
    // Track 15
    emitBool("ch15",      ch15.enabled);   emitFloat("ch15.vm", ch15.volumeMultiplier);
    emitBool("ch15_pp",   ch15_pp.enabled);
    emitBool("ch15_tbd",  ch15_tbd.enabled);
    emitBool("ch15_smp",  ch15_smp.enabled);
    // Track 16 (audio input)
    emitBool("ch16",      ch16.enabled);   emitFloat("ch16.vm", ch16.volumeMultiplier);
    emitBool("ch16_in",   ch16_in.enabled);

    return s;
}


// Lightweight volmult-only update — single float write into the track's
// RackChannelMixer.volumeMultiplier field. Called from
// MacroTranslator::RefreshDefinitionById's same-machine branch so editing
// a macro's volmult and reloading via REST takes effect on the running
// rack mixer without a power cycle.
//
// Concurrency: caller already holds SPManager::processMutex (entered in
// MacroSPManager::RefreshSingleMacro before invoking RefreshDefinitionById).
// Do NOT take processMutex here — FreeRTOS mutex is non-recursive, second
// take by the same task would deadlock the audio task on the next block.
// The single float write is naturally atomic on RISC-V/ARM for an aligned
// 4-byte field; the audio thread reads it during PreProcess() unaffected.
void ctagSoundProcessorGrooveBoxRack::setTrackVolumeMultiplier(const uint8_t trackIndex, float volumeMultiplier) {
    switch (trackIndex) {
        case  0: ch1.volumeMultiplier  = volumeMultiplier; break;
        case  1: ch2.volumeMultiplier  = volumeMultiplier; break;
        case  2: ch3.volumeMultiplier  = volumeMultiplier; break;
        case  3: ch4.volumeMultiplier  = volumeMultiplier; break;
        case  4: ch5.volumeMultiplier  = volumeMultiplier; break;
        case  5: ch6.volumeMultiplier  = volumeMultiplier; break;
        case  6: ch7.volumeMultiplier  = volumeMultiplier; break;
        case  7: ch8.volumeMultiplier  = volumeMultiplier; break;
        case  8: ch9.volumeMultiplier  = volumeMultiplier; break;
        case  9: ch10.volumeMultiplier = volumeMultiplier; break;
        case 10: ch11.volumeMultiplier = volumeMultiplier; break;
        case 11: ch12.volumeMultiplier = volumeMultiplier; break;
        case 12: ch13.volumeMultiplier = volumeMultiplier; break;
        case 13: ch14.volumeMultiplier = volumeMultiplier; break;
        case 14: ch15.volumeMultiplier = volumeMultiplier; break;
        case 15: ch16.volumeMultiplier = volumeMultiplier; break;
    }
}

// Pico-side user mute → rack mixer muted flag. RackChannelMixer::PreProcess
// evaluates `enabled = (level > minVolume) && !muted`, so toggling this
// silences the track regardless of LEVEL. Essential for the Input track
// (ch16, continuous passthrough audio) and cuts synth tails instantly on
// the other 15 tracks.
void ctagSoundProcessorGrooveBoxRack::setTrackMute(const uint8_t trackIndex, bool muted) {
    switch (trackIndex) {
        case  0: ch1.muted  = muted; break;
        case  1: ch2.muted  = muted; break;
        case  2: ch3.muted  = muted; break;
        case  3: ch4.muted  = muted; break;
        case  4: ch5.muted  = muted; break;
        case  5: ch6.muted  = muted; break;
        case  6: ch7.muted  = muted; break;
        case  7: ch8.muted  = muted; break;
        case  8: ch9.muted  = muted; break;
        case  9: ch10.muted = muted; break;
        case 10: ch11.muted = muted; break;
        case 11: ch12.muted = muted; break;
        case 12: ch13.muted = muted; break;
        case 13: ch14.muted = muted; break;
        case 14: ch15.muted = muted; break;
        case 15: ch16.muted = muted; break;
        default: break;
    }
}

void ctagSoundProcessorGrooveBoxRack::setTrackBank(const uint8_t trackIndex, const uint16_t bankIndex) {
    printf("GrooveBoxRack: setTrackBank(%d, %d)\n", trackIndex, bankIndex);
    if (trackIndex >= 16) return;
    // trackSamplers[15] is intentionally nullptr (ch16 = audio input, no sampler).
    if (RackRompler* smp = trackSamplers[trackIndex]) {
        smp->bank_index = bankIndex;
    }
}

// =====================================================================================
// [7] MIDI NOTE ROUTING — the heart of "MIDI note → which track / which voice".
//
//   Drum tracks share MIDI channels — note picks the track:
//     channel  9 (= MIDI ch 10) note 36/37/38 → tracks 1/2/3  (db/fmb/ds + ab/-/as + ro)
//     channel 10 (= MIDI ch 11) note 36/37/38 → tracks 4/5/6  (hh1/rs/cl + hh2/-/-  + ro)
//     channel 11 (= MIDI ch 12) note 36/37     → tracks 7/8   (sampler)
//   Synth tracks: one channel per track, pitched notes:
//     channel  0..6 (= MIDI ch 1..7) → tracks 9..15           (td3, td3, mo, wtosc/mo, ro, ro, pp)
//   Everything else is silently ignored.
//
//   When you add a new rack machine, drop a new entry in buildVoiceRegistry() above:
//     - drum:  channel = 9..11, triggerNote >= 0, only fires when note matches.
//     - synth: channel = 0..6,  triggerNote < 0,  fires on any note, note → pitch.
//   rackgen.js prints the exact snippet for you.
// =====================================================================================

// Walks the voice registry: every entry matching this (channel, note) gate that is
// also currently enabled fires its noteOn() callback.
//   - Drum entries set triggerNote >= 0 — only fire when note == triggerNote.
//   - Synth entries set triggerNote < 0 — fire on any note (note is the pitch).
//   - No-MIDI entries (channel < 0) never match.
// The old switch over channels + nested switches over notes is equivalent to walking
// every entry and applying the gate, because each (channel, note) pair maps to a fixed
// subset of voices in the registry.  Order of dispatch matches the original code
// because the registry is populated in the same order — verified by
// simulator/build/routing-test passing against tests/golden/groovebox-routing.txt.
void ctagSoundProcessorGrooveBoxRack::handleMidiNoteOn(const uint8_t channel, uint8_t note, uint8_t velocity) {
    for (const auto& v : voiceRegistry) {
        if (v.channel != channel) continue;
        if (v.triggerNote >= 0 && v.triggerNote != note) continue;
        if (!*v.enabledFlag) continue;
        if (v.noteOn) v.noteOn(note, velocity);
    }
}

void ctagSoundProcessorGrooveBoxRack::handleMidiNoteOff(const uint8_t channel, uint8_t note, uint8_t velocity) {
    for (const auto& v : voiceRegistry) {
        if (v.channel != channel) continue;
        if (v.triggerNote >= 0 && v.triggerNote != note) continue;
        if (!*v.enabledFlag) continue;
        if (v.noteOff) v.noteOff(note, velocity);
    }
}


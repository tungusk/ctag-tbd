/***************
TBD-16 — Macro/Preset System & GrooveBoxRack

(c) 2025-2026 Per-Olov Jernberg (possan). https://possan.codes
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
#include <initializer_list>
#include <cstddef>
#include "braids/quantizer_scales.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_cpu.h"
#include "esp_timer.h"
// #include "freertos/FreeRTOS.h"

using namespace CTAG::SP;

// =====================================================================================
// CONTENTS — what's where in this file (search for the banner to jump)
// -------------------------------------------------------------------------------------
//   [1]  AUDIO BUS                — track-out mixers, FX bus pre-process, master render
//   [2]  PROCESS()                — the audio-block entry point: MIDI → tracks → FX → out
//   [3]  PARAM REGISTRATION       — registerParamAndCC(), handleMidiControlChange()
//   [4]  INIT / KNOW YOURSELF     — track wiring, model + global-param map setup
//   [5]  MIDI PARSER              — parseIncomingMidiMessages() raw-bytes split
//   [6]  TRACK CONFIG             — setTrackMachine() + setTrackMachineByDeviceValue()
//                                   + setTrackBank() (per-track machine selector tables)
//   [7]  MIDI ROUTING             — handleMidiNoteOn() / handleMidiNoteOff() per channel
//
//   (Look for "SIMULATOR-ONLY OVERRIDE" near the dtor for the sim's loadPresetInternal —
//    clean master/FX defaults that don't apply on device. For "how do I add a new rack
//    voice?" see docs/plugins/rack-plugins.rst.)
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

    // fDelayTime = fx1_time_ms / 10.0f; // TODO: Better calculation here
    float dt = fx1_time_ms / 32.0f; // 0-127
    dt *= last_msPerBeat;
    dt /= 8.0; // 8 per "1/16th note"
    dt *= 44100.0f;
    dt /= 1000.0f;
    CONSTRAIN(dt, 4.0f, 88200.f);
    int idt = (int)dt;
    if (idt != delaySamples) {
        delaySamples = idt;
        // printf("Delay time set to %d samples (scaled BPM: %d)\n", idt, scaledbpm);
    }

    MK_FLT_PAR_ABS_NOCV(fBase, fx1_base, 4095.f, 1.f)
    MK_FLT_PAR_ABS_NOCV(fWidth, fx1_width, 4095.f, 1.f)
    bool bSync = fx1_sync;
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
    reverb.set_time(fRevTime);
    reverb.set_lp(fReverbLPF);
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
    MK_FLT_PAR_ABS_NOCV(fDelayReverbSend, fx1_fx_send, 4095.f, maxFXSendLevelRev)
    fDelayReverbSend *= fDelayReverbSend;
    MK_FLT_PAR_ABS_NOCV(fFeedback, fx1_feedback, 4095.f, 1.5f)
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
    MK_FLT_PAR_ABS_PAN_NOCV(fCompMix, c_mix, 4095.f, 1.f)
    MK_FLT_PAR_ABS_NOCV(fCompDlyLevel, c_dly_level, 4095.f, 2.f)
    fCompDlyLevel *= fCompDlyLevel;
    MK_FLT_PAR_ABS_NOCV(fCompRevLevel, c_rev_level, 4095.f, 2.f)
    fCompRevLevel *= fCompRevLevel;

    // overall mix
    MK_FLT_PAR_ABS_NOCV(fMixLevel, sum_lev, 4095.f, 3.f)
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

    // reverb
    reverb.Process(rev_buf_l, rev_buf_r, bufSz);

    // add fx to sum
    fRevAmount *= fRevAmount;
    fDelayAmount *= fDelayAmount;
    for (int i = 0; i < bufSz; i++) {
        data.buf[i * 2] += rev_buf_l[i] * fRevAmount + dly_buf_l[i] * fDelayAmount;
        data.buf[i * 2 + 1] += rev_buf_r[i] * fRevAmount + dly_buf_r[i] * fDelayAmount;
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

        ch15_smp.track_length = ch15.track_length;
        ch15_smp.Process(idata);
        if (ch15_smp.enabled) {
            mixRenderOutputMono(ch15_smp.s1_out, ch15.level, ch15.pan, ch15.send1, ch15.send2);
        }
    }

    // T2 = esp_timer_get_time();
    // ch15_render_time = T2 - T;

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
    // ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "Registering param %s, key %d for CC %d+%d",
    //     fullId.c_str(), key, initdata->cc_base, cc);
    pMapParCC.emplace(key, PsramVector<function<void(const int)>>());
    pMapParCC[key].push_back(setter);
}

void ctagSoundProcessorGrooveBoxRack::handleMidiControlChange(const uint8_t channel, const uint8_t control, const uint8_t value) {
    int cv_value = ((int)value * 4096) / 128;
    int key = CC_TO_MAP_KEY(channel, control);

    auto it = pMapParCC.find(key);
    if (it != pMapParCC.end()) {
        // ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "MIDI: CC %d, %d, %d (cv %d) (Set)", channel, control, value, cv_value);
        // TODO: Write directly to devices?.
        for(auto& listener : it->second){
            listener(cv_value);
            // if (dev->handlesCC(channel, control)){
            //     dev->setParameterForCC(control, cv_value);
            // }
        }
    } else {
        // ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "MIDI: CC %d, %d, %d (Unhandled)", channel, control, value);
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

    // init compressor
    sumCompressor.setSampleRate(44100.f);
    sumCompressor.initRuntime();

    ESP_LOGI("ctagSoundProcessorGrooveBoxRack", "After Init()");
    dumpMemoryUsage();
}

ctagSoundProcessorGrooveBoxRack::~ctagSoundProcessorGrooveBoxRack(){
}

#ifdef TBD_SIM
// ===================== SIMULATOR-ONLY OVERRIDE ============================================
// On the device the macro/preset (RP2350) layer overrides FX1 / FX2 / Master for the loaded
// kit; the sim has no such layer, so the values come straight from mp-GrooveBoxRack.json,
// tuned for the hardware gain staging (raw rack peak ~20×, tanh-clipped). Re-apply clean
// defaults AFTER every LoadPreset(): unity-ish master + bypassed compressor + audible FX
// returns (so a track's FX Send 1/2 in the WebUI actually produces delay/reverb). Excluded
// from the device build entirely by the #ifdef TBD_SIM guards (here and in the .hpp).
void ctagSoundProcessorGrooveBoxRack::loadPresetInternal() {
    ctagSoundProcessor::loadPresetInternal();        // apply mp-GrooveBoxRack.json normally
    sum_mute = 0; sum_lev = 1500;                    // master ≈ unity for a single voice
    c_mix = 0; c_gain = 0; c_thres = 4095; c_ratio = 0; c_lpf = 0; c_dly_level = 0; c_rev_level = 0;  // bypass comp
    fx1_amount = 1500; fx1_fx_send = 0; fx1_feedback = 1000;
    fx1_time_ms = 256;                                // = a quarter note at 120 BPM
    fx1_sync = 0; fx1_freeze = 0; fx1_tape_digital = 0;
    fx1_st_width = 2048; fx1_base = 1024; fx1_width = 2048;
    fx2_amount = 2000; fx2_time = 2048; fx2_lp = 2048;
}
// ===================== END SIMULATOR-ONLY ================================================
#endif

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

    DEFINE_GLOBAL_PARAM("c_thres", 13, 60, c_thres);
    DEFINE_GLOBAL_PARAM("c_ratio", 13, 61, c_ratio);
    DEFINE_GLOBAL_PARAM("c_atk", 13, 62, c_atk);
    DEFINE_GLOBAL_PARAM("c_rel", 13, 63, c_rel);
    DEFINE_GLOBAL_PARAM("c_lpf", 13, 64, c_lpf);
    DEFINE_GLOBAL_PARAM("c_gain", 13, 65, c_gain);
    DEFINE_GLOBAL_PARAM("c_mix", 13, 66, c_mix);
    DEFINE_GLOBAL_PARAM("c_dly_level", 13, 67, c_dly_level);
    DEFINE_GLOBAL_PARAM("c_rev_level", 13, 68, c_rev_level);

    DEFINE_GLOBAL_PARAM("sum_mute", 13, 80, sum_mute);
    DEFINE_GLOBAL_PARAM("sum_lev", 13, 81, sum_lev);

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
                if (b2 == 0) handleMidiNoteOff(channel, b1, 0); // note-on, velocity 0 == note-off
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

    if (trackIndex == 0) {
        ch1.enabled = !machineId.empty();
        ch1.volumeMultiplier = volumeMultiplier;
        ch1_db.enabled = machineId == "db";
        ch1_ab.enabled = machineId == "ab";
        ch1_smp.enabled = machineId == "ro";
        // printf("  ch1=%d, ch1_db=%d, ch1_ab=%d, ch1_ro=%d\n", ch1.enabled, ch1_db.enabled, ch1_ab.enabled, ch1_smp.enabled);
    }
    else if (trackIndex == 1) {
        ch2.enabled = !machineId.empty();
        ch2.volumeMultiplier = volumeMultiplier;
        ch2_fmb1.enabled = machineId == "fmb";
        ch2_smp.enabled = machineId == "ro";
        // printf("  ch2=%d, ch2_fmb1=%d, ch2_ro=%d\n", ch2.enabled, ch2_fmb1.enabled, ch2_smp.enabled);
    }
    else if (trackIndex == 2) {
        ch3.enabled = !machineId.empty();
        ch3.volumeMultiplier = volumeMultiplier;
        ch3_ds.enabled = machineId == "ds";
        ch3_as.enabled = machineId == "as";
        ch3_smp.enabled = machineId == "ro";
        // printf("  ch3=%d, ch3_ds=%d, ch3_as=%d, ch3_ro=%d\n", ch3.enabled, ch3_ds.enabled, ch3_as.enabled, ch3_smp.enabled);
    }
    else if (trackIndex == 3) {
        ch4.enabled = !machineId.empty();
        ch4.volumeMultiplier = volumeMultiplier;
        ch4_hh1.enabled = machineId == "hh1";
        ch4_hh2.enabled = machineId == "hh2";
        ch4_smp.enabled = machineId == "ro";
        // printf("  ch4=%d, ch4_hh1=%d, ch4_hh2=%d, ch4_ro=%d\n", ch4.enabled, ch4_hh1.enabled, ch4_hh2.enabled, ch4_smp.enabled);
    }
    else if (trackIndex == 4) {
        ch5.enabled = !machineId.empty();
        ch5.volumeMultiplier = volumeMultiplier;
        ch5_rs.enabled = machineId == "rs";
        ch5_smp.enabled = machineId == "ro";
        // printf("  ch5=%d, ch5_rs=%d, ch5_ro=%d\n", ch5.enabled, ch5_rs.enabled, ch5_smp.enabled);
    }
    else if (trackIndex == 5) {
        ch6.enabled = !machineId.empty();
        ch6.volumeMultiplier = volumeMultiplier;
        ch6_cl.enabled = machineId == "cl";
        ch6_smp.enabled = machineId == "ro";
        // printf("  ch6=%d, ch6_cl=%d\n", ch6.enabled, ch6_cl.enabled);
    }
    else if (trackIndex == 6) {
        ch7.enabled = !machineId.empty();
        ch7.volumeMultiplier = volumeMultiplier;
        ch7_smp.enabled = machineId == "ro";
        // printf("  ch7=%d, ch7_ro=%d\n", ch7.enabled, ch7_smp.enabled);
    }
    else if (trackIndex == 7) {
        ch8.enabled = !machineId.empty();
        ch8.volumeMultiplier = volumeMultiplier;
        ch8_smp.enabled = machineId == "ro";
        // printf("  ch8=%d, ch8_ro=%d\n", ch8.enabled, ch8_smp.enabled);
    }
    else if (trackIndex == 8) {
        ch9.enabled = !machineId.empty();
        ch9.volumeMultiplier = volumeMultiplier;
        ch9_td3.enabled = machineId == "td3";
        ch9_smp.enabled = machineId == "ro";
        // printf("  ch9=%d, ch9_td3=%d, ch9_ro=%d\n", ch9.enabled, ch9_td3.enabled, ch9_smp.enabled);
    }
    else if (trackIndex == 9) {
        ch10.enabled = !machineId.empty();
        ch10.volumeMultiplier = volumeMultiplier;
        ch10_td3.enabled = machineId == "td3";
        ch10_smp.enabled = machineId == "ro";
        // printf("  ch10=%d, ch10_td3=%d, ch10_smp=%d\n", ch10.enabled, ch10_td3.enabled, ch10_smp.enabled);
    }
    else if (trackIndex == 10) {
        ch11.enabled = !machineId.empty();
        ch11.volumeMultiplier = volumeMultiplier;
        ch11_mo.enabled = machineId == "mo";
        ch11_smp.enabled = machineId == "ro";
        // printf("  ch11=%d, ch11_mo=%d, ch11_ro=%d\n", ch11.enabled, ch11_mo.enabled, ch11_smp.enabled);
    }
    else if (trackIndex == 11) {
        ch12.enabled = !machineId.empty();
        ch12.volumeMultiplier = volumeMultiplier;
        ch12_wtosc.enabled = machineId == "wtosc";
        ch12_mo.enabled = machineId == "mo";
        ch12_smp.enabled = machineId == "ro";
        // printf("  ch12=%d, ch12_wtosc=%d, ch12_mo=%d, ch12_ro=%d\n", ch12.enabled, ch12_wtosc.enabled, ch12_mo.enabled, ch12_smp.enabled);
    }
    else if (trackIndex == 12) {
        ch13.enabled = !machineId.empty();
        ch13.volumeMultiplier = volumeMultiplier;
        ch13_smp.enabled = machineId == "ro";
        // printf("  ch13=%d, ch13_ro=%d\n", ch13.enabled, ch13_smp.enabled);
    }
    else if (trackIndex == 13) {
        ch14.enabled = !machineId.empty();
        ch14.volumeMultiplier = volumeMultiplier;
        ch14_smp.enabled = machineId == "ro";
        // printf("  ch14=%d, ch14_ro=%d\n", ch14.enabled, ch14_smp.enabled);
    }
    else if (trackIndex == 14) {
        ch15.enabled = !machineId.empty();
        ch15.volumeMultiplier = volumeMultiplier;
        ch15_pp.enabled = machineId == "pp";
        ch15_smp.enabled = machineId == "ro";
        // printf("  ch15=%d, ch15_pp=%d, ch15_ro=%d\n", ch15.enabled, ch15_pp.enabled, ch15_smp.enabled);
    }
    else if (trackIndex == 15) {
        ch16.enabled = !machineId.empty();
        ch16.volumeMultiplier = volumeMultiplier;
        ch16_in.enabled = (machineId == "in");
        // printf("  ch16=%d, ch16_in=%d\n", ch16.enabled, ch16_in.enabled);
    }
}

// The "chN_device" parameter (0..4095 int) is the channel's machine selector. The WebUI's
// machine dropdown buckets the option index over the full 0..4095 range — so for an N-machine
// track, the i-th option sends round(i/(N-1) * 4095), and we recover the index the same way.
// The factory preset has chN_device = 0, i.e. the first machine. On the device the macro layer
// (MacroTranslator) calls setTrackMachine() directly afterwards and is authoritative; this
// just keeps the WebUI machine dropdown working and gives a sane default.
void ctagSoundProcessorGrooveBoxRack::setTrackMachineByDeviceValue(const uint8_t trackIndex, const int deviceValue) {
    auto pick = [deviceValue](std::initializer_list<const char*> ms) -> const char* {
        const std::size_t n = ms.size();
        if (n == 0) return nullptr;
        if (n == 1) return *ms.begin();
        const int v = (deviceValue < 0) ? 0 : (deviceValue > 4095 ? 4095 : deviceValue);
        // 0 → 0, 4095 → n-1 (matches the WebUI's round(i/(n-1) * 4095))
        std::size_t idx = static_cast<std::size_t>((static_cast<long>(v) * static_cast<long>(n - 1) + 2047) / 4095);
        if (idx >= n) idx = n - 1;
        return *(ms.begin() + idx);
    };
    const char* m = nullptr;
    switch (trackIndex) {
        case 0:  m = pick({"db",    "ab",  "ro"});       break;  // Kick
        case 1:  m = pick({"fmb",   "ro"});              break;  // Kick2
        case 2:  m = pick({"ds",    "as",  "ro"});       break;  // Snare
        case 3:  m = pick({"hh1",   "hh2", "ro"});       break;  // Hat
        case 4:  m = pick({"rs",    "ro"});              break;  // Rimshot
        case 5:  m = pick({"cl",    "ro"});              break;  // Clap
        case 6:  m = "ro";                               break;  // sampler-only
        case 7:  m = "ro";                               break;  // sampler-only
        case 8:  m = pick({"td3",   "ro"});              break;  // ch9
        case 9:  m = pick({"td3",   "ro"});              break;  // ch10
        case 10: m = pick({"mo",    "ro"});              break;  // ch11
        case 11: m = pick({"wtosc", "mo",  "ro"});       break;  // ch12
        case 12: m = "ro";                               break;  // sampler-only
        case 13: m = "ro";                               break;  // sampler-only
        case 14: m = pick({"pp",    "ro"});              break;  // ch15
        default: return;                                          // ch16 = audio input — no machine here
    }
    setTrackMachine(trackIndex, m, 1.f);
}

void ctagSoundProcessorGrooveBoxRack::setTrackBank(const uint8_t trackIndex, const uint16_t bankIndex) {
    printf("GrooveBoxRack: setTrackBank(%d, %d)\n", trackIndex, bankIndex);

    if (trackIndex == 0) {
        ch1_smp.bank_index = bankIndex;
    }
    else if (trackIndex == 1) {
        ch2_smp.bank_index = bankIndex;
    }
    else if (trackIndex == 2) {
        ch3_smp.bank_index = bankIndex;
    }
    else if (trackIndex == 3) {
        ch4_smp.bank_index = bankIndex;
    }
    else if (trackIndex == 4) {
        ch5_smp.bank_index = bankIndex;
    }
    else if (trackIndex == 5) {
        ch6_smp.bank_index = bankIndex;
    }
    else if (trackIndex == 6) {
        ch7_smp.bank_index = bankIndex;
    }
    else if (trackIndex == 7) {
        ch8_smp.bank_index = bankIndex;
    }
    else if (trackIndex == 8) {
        ch9_smp.bank_index = bankIndex;
    }
    else if (trackIndex == 9) {
        ch10_smp.bank_index = bankIndex;
    }
    else if (trackIndex == 10) {
        ch11_smp.bank_index = bankIndex;
    }
    else if (trackIndex == 11) {
        ch12_smp.bank_index = bankIndex;
    }
    else if (trackIndex == 12) {
        ch13_smp.bank_index = bankIndex;
    }
    else if (trackIndex == 13) {
        ch14_smp.bank_index = bankIndex;
    }
    else if (trackIndex == 14) {
        ch15_smp.bank_index = bankIndex;
    }
    else if (trackIndex == 15) {
        // ch16_smp.bank_index = bankIndex;
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
//   When you add a new rack machine, drop a branch in here for its track's channel
//   (drum: `if (chN_x.enabled && velocity>0) chN_x.trigger();`,
//    synth: `if (chN_x.enabled) chN_x.noteOn(note, velocity);`).
//   rackgen.js prints the exact snippet for you.
// =====================================================================================

void ctagSoundProcessorGrooveBoxRack::handleMidiNoteOn(const uint8_t channel, uint8_t note, uint8_t velocity) {
    // printf("GrooveBoxRack: handleMidiNoteOn(channel=%d, note=%d, velocity=%d)\n", channel, note, velocity);
    if (channel == 9) {
        if (note == 36) {
            if (ch1_ab.enabled) {
                // printf("ch1_ab triggered by note %d, velocity %d\n", note, velocity);
                if (velocity > 0) {
                    ch1_ab.trigger();
                }
            }
            if (ch1_db.enabled) {
                // printf("ch1_db triggered by note %d, velocity %d\n", note, velocity);
                if (velocity > 0) {
                    ch1_db.trigger();
                }
            }
            if (ch1_smp.enabled) {
                // printf("ch1_ro triggered by note %d, velocity %d\n", note, velocity);
                if (velocity > 0) {
                    ch1_smp.noteOn(36, velocity);
                } else {
                    ch1_smp.noteOff(36, 0);
                }
            }
        }
        else if (note == 37) {
            if (ch2_fmb1.enabled) {
                // printf("ch2_fmb1 triggered by note %d, velocity %d\n", note, velocity);
                if (velocity > 0) {
                    ch2_fmb1.trigger();
                }
            }
            if (ch2_smp.enabled) {
                // printf("ch2_ro triggered by note %d, velocity %d\n", note, velocity);
                if (velocity > 0) {
                    ch2_smp.noteOn(36, velocity);
                } else {
                    ch2_smp.noteOff(36, 0);
                }
            }
        }
        else if (note == 38) {
            if (ch3_as.enabled) {
                // printf("ch3_as triggered by note %d, velocity %d\n", note, velocity);
                if (velocity > 0) {
                    ch3_as.trigger();
                }
            }
            if (ch3_ds.enabled) {
                // printf("ch3_ds triggered by note %d, velocity %d\n", note, velocity);
                if (velocity > 0) {
                    ch3_ds.trigger();
                }
            }
            if (ch3_smp.enabled) {
                // printf("ch3_ro triggered by note %d, velocity %d\n", note, velocity);
                if (velocity > 0) {
                    ch3_smp.noteOn(36, velocity);
                } else {
                    ch3_smp.noteOff(36, 0);
                }
            }
        }
    } else if (channel == 10) {
        if (note == 36) {
            if (ch4_hh1.enabled) {
                // printf("ch4_hh1 triggered by note %d, velocity %d\n", note, velocity);
                if (velocity > 0) {
                    ch4_hh1.trigger();
                }
            }
            if (ch4_hh2.enabled) {
                // printf("ch4_hh2 triggered by note %d, velocity %d\n", note, velocity);
                if (velocity > 0) {
                    ch4_hh2.trigger();
                }
            }
            if (ch4_smp.enabled) {
                    // printf("ch4_ro triggered by note %d, velocity %d\n", note, velocity);
                if (velocity > 0) {
                    ch4_smp.noteOn(36, velocity);
                } else {
                    ch4_smp.noteOff(36, 0);
                }
            }
        }
        else if (note == 37) {
            if (ch5_rs.enabled) {
                // printf("ch5_rs triggered by note %d, velocity %d\n", note, velocity);
                if (velocity > 0) {
                    ch5_rs.trigger();
                }
            }
            if (ch5_smp.enabled) {
                // printf("ch5_ro triggered by note %d, velocity %d\n", note, velocity);
                if (velocity > 0) {
                    ch5_smp.noteOn(36, velocity);
                } else {
                    ch5_smp.noteOff(36, 0);
                }
            }
        }
        else if (note == 38) {
            if (ch6_cl.enabled) {
                if (velocity > 0) {
                    // printf("ch6_cl triggered by note %d, velocity %d\n", note, velocity);
                    ch6_cl.trigger();
                }
            }
            if (ch6_smp.enabled) {
                // printf("ch6_ro triggered by note %d, velocity %d\n", note, velocity);
                if (velocity > 0) {
                    ch6_smp.noteOn(36, velocity);
                } else {
                    ch6_smp.noteOff(36, 0);
                }
            }
        }
    } else if (channel == 11) {
        if (note == 36) {
            if (ch7_smp.enabled) {
                // printf("ch7_ro triggered by note %d, velocity %d\n", note, velocity);
                if (velocity > 0) {
                    ch7_smp.noteOn(36, velocity);
                } else {
                    ch7_smp.noteOff(36, 0);
                }
            }
        }
        else if (note == 37) {
            if (ch8_smp.enabled) {
                // printf("ch8_ro triggered by note %d, velocity %d\n", note, velocity);
                if (velocity > 0) {
                    ch8_smp.noteOn(36, velocity);
                } else {
                    ch8_smp.noteOff(36, 0);
                }
            }
        }
    }
    else if (channel == 0) {
        if (ch9_td3.enabled) {
            //  printf("ch9    _td3 triggered by note %d, velocity %d\n", note, velocity);
            if (velocity > 0) {
                ch9_td3.noteOn(note, velocity);
            } else {
                ch9_td3.noteOff(note, 0);
            }
        }
        if (ch9_smp.enabled) {
            // printf("ch9_ro triggered by note %d, velocity %d\n", note, velocity);
            if (velocity > 0) {
                ch9_smp.noteOn(note, velocity);
            } else {
                ch9_smp.noteOff(note, 0);
            }
        }
    }
    else if (channel == 1) {
        if (ch10_td3.enabled) {
            // printf("ch10_td3 triggered by note %d, velocity %d\n", note, velocity);
            if (velocity > 0) {
                ch10_td3.noteOn(note, velocity);
            } else {
                ch10_td3.noteOff(note, 0);
            }
        }
        if (ch10_smp.enabled) {
            // printf("ch10_smp triggered by note %d, velocity %d\n", note, velocity);
            if (velocity > 0) {
                ch10_smp.noteOn(note, velocity);
            } else {
                ch10_smp.noteOff(note, 0);
            }
        }
    }
    else if (channel == 2) {
        if (ch11_mo.enabled) {
            // printf("ch11_mo triggered by note %d, velocity %d\n", note, velocity);
            if (velocity > 0) {
                ch11_mo.noteOn(note, velocity);
            } else {
                ch11_mo.noteOff(note, 0);
            }
        }
        if (ch11_smp.enabled) {
            // printf("ch11_ro triggered by note %d, velocity %d\n", note, velocity);
            if (velocity > 0) {
                ch11_smp.noteOn(note, velocity);
            } else {
                ch11_smp.noteOff(note, 0);
            }
        }
    }
    else if (channel == 3) {
        if (ch12_wtosc.enabled) {
            if (velocity > 0) {
                ch12_wtosc.noteOn(note, velocity);
            } else {
                ch12_wtosc.noteOff(note, 0);
            }
        }
        if (ch12_mo.enabled) {
            // printf("ch12_mo triggered by note %d, velocity %d\n", note, velocity);
            if (velocity > 0) {
                ch12_mo.noteOn(note, velocity);
            } else {
                ch12_mo.noteOff(note, 0);
            }
        }
        if (ch12_smp.enabled) {
            // printf("ch12_ro triggered by note %d, velocity %d\n", note, velocity);
            if (velocity > 0) {
                ch12_smp.noteOn(note, velocity);
            } else {
                ch12_smp.noteOff(note, 0);
            }
        }
    }
    else if (channel == 4) {
        if (ch13_smp.enabled) {
            // printf("ch13_ro triggered by note %d, velocity %d\n", note, velocity);
            if (velocity > 0) {
                ch13_smp.noteOn(note, velocity);
            } else {
                ch13_smp.noteOff(note, 0);
            }
        }
    }
    else if (channel == 5) {
        if (ch14_smp.enabled) {
            // printf("ch14_ro triggered by note %d, velocity %d\n", note, velocity);
            if (velocity > 0) {
                ch14_smp.noteOn(note, velocity);
            } else {
                ch14_smp.noteOff(note, 0);
            }
        }
    }
    else if (channel == 6) {
        if (ch15_pp.enabled) {
            // printf("ch15_pp triggered by note %d, velocity %d\n", note, velocity);
            if (velocity > 0) {
                ch15_pp.noteOn(note, velocity);
            } else {
                ch15_pp.noteOff(note, 0);
            }
        }
        if (ch15_smp.enabled) {
            // printf("ch15_ro triggered by note %d, velocity %d\n", note, velocity);
            if (velocity > 0) {
                ch15_smp.noteOn(note, velocity);
            } else {
                ch15_smp.noteOff(note, 0);
            }
        }
    }
}

void ctagSoundProcessorGrooveBoxRack::handleMidiNoteOff(const uint8_t channel, uint8_t note, uint8_t velocity) {
    // printf("GrooveBoxRack: handleMidiNoteOff(channel=%d, note=%d, velocity=%d)\n", channel, note, velocity);
    if (channel == 9) {
        if (note == 36) {
            if (ch1_smp.enabled) {
                ch1_smp.noteOff(36, 0);
            }
        }
        if (note == 37) {
            if (ch2_smp.enabled) {
                ch2_smp.noteOff(36, 0);
            }
        }
        if (note == 38) {
            if (ch3_smp.enabled) {
                ch3_smp.noteOff(36, 0);
            }
        }
    } else if (channel == 10) {
        if (note == 36) {
            if (ch4_smp.enabled) {
                ch4_smp.noteOff(36, 0);
            }
        }
        if (note == 37) {
            if (ch5_smp.enabled) {
                ch5_smp.noteOff(36, 0);
            }
        }
        if (note == 38) {
            if (ch6_smp.enabled) {
                ch6_smp.noteOff(36, 0);
            }
        }
    } else if (channel == 11) {
        if (note == 36) {
            if (ch7_smp.enabled) {
                ch7_smp.noteOff(36, 0);
            }
        }
        if (note == 37) {
            if (ch8_smp.enabled) {
                ch8_smp.noteOff(36, 0);
            }
        }
    }
    else if (channel == 0) {
        if (ch9_td3.enabled) {
            ch9_td3.noteOff(note, 0);
        }
        if (ch9_smp.enabled) {
            ch9_smp.noteOff(note, 0);
        }
    }
    else if (channel == 1) {
        if (ch10_td3.enabled) {
            ch10_td3.noteOff(note, 0);
        }
        if (ch10_smp.enabled) {
            ch10_smp.noteOff(note, 0);
        }
    }
    else if (channel == 2) {
        if (ch11_mo.enabled) {
            ch11_mo.noteOff(note, 0);
        }
        if (ch11_smp.enabled) {
            ch11_smp.noteOff(note, 0);
        }
    }
    else if (channel == 3) {
        if (ch12_wtosc.enabled) {
            ch12_wtosc.noteOff(note, 0);
        }
        if (ch12_mo.enabled) {
            ch12_mo.noteOff(note, 0);
        }
        if (ch12_smp.enabled) {
            ch12_smp.noteOff(note, 0);
        }
    }
    else if (channel == 4) {
        if (ch13_smp.enabled) {
            ch13_smp.noteOff(note, 0);
        }
    }
    else if (channel == 5) {
        if (ch14_smp.enabled) {
            ch14_smp.noteOff(note, 0);
        }
    }
    else if (channel == 6) {
        if (ch15_pp.enabled) {
            ch15_pp.noteOff(note, 0);
        }
        if (ch15_smp.enabled) {
            ch15_smp.noteOff(note, 0);
        }
    }
}

void ctagSoundProcessorGrooveBoxRack::handleMidiControlChangePair(const uint8_t channel, uint8_t firstcontrol, uint16_t value) {
}

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
#include "RackWTOsc.hpp"
#include "../ctagSoundProcessorGrooveBoxRack.hpp"
#include "helpers/ctagNumUtil.hpp"
#include "plaits/dsp/engine/engine.h"
#include "esp_heap_caps.h"
#include <algorithm>

using namespace CTAG::SP;

namespace {

int decodeCcExpandedEnum(const int raw, const int maxValue) {
    int decoded = (raw * 128 + 2048) / 4096;
    if (decoded < 0) {
        return 0;
    }
    if (decoded > maxValue) {
        return maxValue;
    }
    return decoded;
}

int decodeDirectOrNormalizedRange(const int raw, const int directMax) {
    if (raw <= directMax) {
        return raw < 0 ? 0 : raw;
    }
    int decoded = (raw * directMax + 2047) / 4095;
    if (decoded < 0) {
        return 0;
    }
    if (decoded > directMax) {
        return directMax;
    }
    return decoded;
}

float decodeLfoRateHz(const int raw) {
    int v = raw;
    if (v < 0) {
        v = 0;
    } else if (v > 4095) {
        v = 4095;
    }

    if (v <= 2048) {
        return 0.01f + (static_cast<float>(v) / 2048.f) * 0.99f;
    }
    return 1.f + (static_cast<float>(v - 2048) / 2047.f) * 19.f;
}

} // namespace

void RackWTOsc::Init(const GrooveBoxRackInitData *initdata) {
    // Allocate ONE bank's worth of PSRAM (~33 KB) plus a 2 KB float
    // scratch for the integration math. Bank change in Process triggers
    // a one-shot prepareBank(currentBank) — single audio-block glitch on
    // change is acceptable for non-real-time bank selection.
    bankBuffer = static_cast<int16_t*>(heap_caps_malloc(kBankBytes, MALLOC_CAP_SPIRAM));
    memset(bankBuffer, 0, kBankBytes);
    fbufScratch = static_cast<float*>(heap_caps_malloc(512 * sizeof(float), MALLOC_CAP_SPIRAM));

    // Preload bank 0 so the first noteOn doesn't have to do the prep.
    prepareBank(currentBank, initdata->sampleRom);
    lastBank = currentBank;

    for (auto &voice : voices) {
        voice.lfo.SetSampleRate(44100.f / BUF_SZ);
        voice.lfo.SetFrequency(1.f);
        voice.oscillator.Init();
        voice.svf.Init();
        voice.adsr.SetModeExp();
        voice.adsr.SetSampleRate(44100.f / BUF_SZ);
        voice.adsr.Reset();
    }
    this->enabled = false;

    initdata->rack->registerParamAndCC(initdata, "wavebank", 8, [&](const int val){ wavebank = val;});
    initdata->rack->registerParamAndCC(initdata, "wave", 9, [&](const int val){ wave = val;});
    initdata->rack->registerParamAndCC(initdata, "tune", 10, [&](const int val){ tune = val;});

    initdata->rack->registerParamAndCC(initdata, "fmode", 11, [&](const int val){ fmode = val;});
    initdata->rack->registerParamAndCC(initdata, "fcut", 12, [&](const int val){ fcut = val;});
    initdata->rack->registerParamAndCC(initdata, "freso", 13, [&](const int val){ freso = val;});
    initdata->rack->registerParamAndCC(initdata, "attack", 15, [&](const int val){ attack = val;});
    initdata->rack->registerParamAndCC(initdata, "decay", 16, [&](const int val){ decay = val;});
    initdata->rack->registerParamAndCC(initdata, "sustain", 17, [&](const int val){ sustain = val;});
    initdata->rack->registerParamAndCC(initdata, "release", 18, [&](const int val){ release = val;});

    initdata->rack->registerParamAndCC(initdata, "eg2wave", 19, [&](const int val){ eg2wave = val;});
    initdata->rack->registerParamAndCC(initdata, "eg2fm", 20, [&](const int val){ eg2fm = val;});
    initdata->rack->registerParamAndCC(initdata, "eg2filtfm", 21, [&](const int val){ eg2filtfm = val;});

    initdata->rack->registerParamAndCC(initdata, "lfospeed", 22, [&](const int val){ lfospeed = val;});
    initdata->rack->registerParamAndCC(initdata, "lfophase", 23,
        [&](const int val){ lfophase = (decodeDirectOrNormalizedRange(val, 180) * 4095 + 90) / 180; },
        [&](const int val){ lfophase = val; });

    // lfo2* ctrl numbers are 24..27 to satisfy the macro idx+8=ctrl
    // invariant (LFO Mod page = macro idx 16..19). ctrl=24 was previously
    // reserved for egfasl (Slow EG toggle) which is currently disabled
    // in DSP, so it's free to reuse.
    initdata->rack->registerParamAndCC(initdata, "lfo2wave",   24, [&](const int val){ lfo2wave = val;});
    initdata->rack->registerParamAndCC(initdata, "lfo2am",     25, [&](const int val){ lfo2am = val;});
    initdata->rack->registerParamAndCC(initdata, "lfo2fm",     26, [&](const int val){ lfo2fm = val;});
    initdata->rack->registerParamAndCC(initdata, "lfo2filtfm", 27, [&](const int val){ lfo2filtfm = val;});

    initdata->rack->registerParamAndCC(initdata, "mode", 14,
        [&](const int val){ mode = std::clamp(val, 0, 36); },
        [&](const int val){ mode = decodeCcExpandedEnum(val, 36); });
}

void RackWTOsc::prepareBank(int b, HELPERS::ctagSampleRom *sampleRom) {
    // Bank b occupies sub-slices [b*64 .. b*64+63] in the sample-rom
    // PSRAM region (each sub-slice = 256 raw int16 samples). If those
    // slices aren't loaded (active wt-bank descriptor has fewer entries),
    // mark the wavetable bad and bail — Process will skip rendering.
    if (!sampleRom->HasSliceGroup(b * 64, b * 64 + 63)) {
        isWaveTableGood = false;
        return;
    }
    if (sampleRom->GetSliceGroupSize(b * 64, b * 64 + 63) != 256 * 64) {
        isWaveTableGood = false;
        return;
    }

    int16_t *buf = bankBuffer;
    const int bufferOffset = 4 * 64;  // 256

    // ReadSlice clips at sliceSize=256, so the legacy upstream pattern
    // ReadSlice(buf, slice, 0, 256*64) returns only ONE wave's worth.
    // Walk the 64 sub-slices explicitly.
    for (int j = 0; j < 64; j++) {
        sampleRom->ReadSlice(&buf[bufferOffset + j * 256], b * 64 + j, 0, 256);
    }

    // Integrated-wavetable preprocessing
    // (https://www.dafx12.york.ac.uk/papers/dafx12_submission_69.pdf,
    // K=1, N=1). See the maths reference in the original ctagSound-
    // ProcessorWTOsc::prepareWavetables for the per-step rationale.
    int c = 0;
    for (int i = 0; i < 64; i++) {
        int startOffset = bufferOffset + i * 256;
        float sum4 = buf[startOffset] + buf[startOffset+1] + buf[startOffset+2] + buf[startOffset+3];
        for (int j = 0; j < 512; j++) {
            fbufScratch[j] = buf[startOffset + (j % 256)] + sum4;
        }
        removeMeanOfFloatArray(fbufScratch, 512);
        scaleFloatArrayToAbsMax(fbufScratch, 512);
        accumulateFloatArray(fbufScratch, 512);
        removeMeanOfFloatArray(fbufScratch, 512);
        for (int j = 512 - 256 - 4; j < 512; j++) {
            int16_t v = static_cast<int16_t>(roundf(fbufScratch[j] * 4.f * 32768.f / 256.f));
            buf[c++] = v;
        }
    }
    for (int i = 0; i < 64; i++) wavetables[i] = &buf[i * 260];
    isWaveTableGood = true;
}

void RackWTOsc::noteOn(uint8_t note, uint8_t vel) {
    const bool monoUnison = std::clamp<int>(mode.load(), 0, 36) > 0;
    const uint32_t serial = ++note_serial_counter;
    if (monoUnison) {
        for (auto &voice : voices) {
            voice.midi_note = note;
            voice.note_serial = serial;
            voice.gate = true;
            voice.note_held = true;
            voice.pending_velocity = vel ? vel : 100;
            voice.pending_retrigger = true;
            voice.silence_tail_blocks = 0;
        }
        return;
    }

    Voice *target = nullptr;

    for (auto &voice : voices) {
        if (voice.note_held && voice.midi_note == note) {
            target = &voice;
            break;
        }
    }
    if (target == nullptr) {
        for (auto &voice : voices) {
            if (!voice.note_held && voice.adsr.IsIdle()) {
                target = &voice;
                break;
            }
        }
    }
    if (target == nullptr) {
        for (auto &voice : voices) {
            if (!voice.note_held) {
                target = &voice;
                break;
            }
        }
    }
    if (target == nullptr) {
        // V1 duophony: keep held voices stable instead of stealing one.
        return;
    }

    target->midi_note = note;
    target->note_serial = serial;
    target->gate = true;
    target->note_held = true;
    target->pending_velocity = vel ? vel : 100;
    target->pending_retrigger = true;  // forces a clean re-attack
    target->silence_tail_blocks = 0;
}

void RackWTOsc::noteOff(uint8_t note, uint8_t vel) {
    (void)vel;
    for (auto &voice : voices) {
        if (voice.note_held && voice.midi_note == note) {
            voice.gate = false;
            voice.note_held = false;
        }
    }
}

void RackWTOsc::Process(const GrooveBoxRackProcessData &data) {
    if (!this->enabled) {
        return;
    }

    std::fill_n(out, BUF_SZ * 2, 0.f);

    // wave select — wavebank is normalized to 0..4096 by registerParamAndCC.
    // Map across all 32 banks.
    currentBank = (wavebank * kMaxBanks) / 4096;
    if (currentBank < 0) currentBank = 0;
    if (currentBank >= kMaxBanks) currentBank = kMaxBanks - 1;

    if (lastBank != currentBank) {
        // One-shot heavy preprocessing on bank change. ~10 ms blocking
        // work; produces an audible audio glitch at the moment of
        // change. Acceptable since bank changes are non-real-time.
        prepareBank(currentBank, data.sampleRom);
        lastBank = currentBank;
    }

    const float fWave = wave / 4095.f;

    MK_FLT_PAR_ABS_NOCV(fAttack, attack, 4095.f, 10.f)
    MK_FLT_PAR_ABS_NOCV(fDecay, decay, 4095.f, 10.f)
    MK_FLT_PAR_ABS_NOCV(fSustain, sustain, 4095.f, 1.f)
    MK_FLT_PAR_ABS_NOCV(fRelease, release, 4095.f, 10.f)

    // EG mod amounts are SEMANTICALLY BIPOLAR (knob mid = no mod,
    // knob full-down = max negative mod, knob full-up = max positive
    // mod). The MK_FLT_PAR_ABS_SFT_NOCV macro in the rack header is
    // actually NON-shifting (despite the _SFT_ name) — the original
    // CV-mode _SFT_ macro shifted, but the rack _NOCV_ variant in
    // ctagSoundProcessorGrooveBoxRack.hpp is identical to ABS_NOCV.
    // Compute the bipolar shift manually here so knob mid (cv=2048)
    // → 0, knob ends (cv=0 / cv=4095) → ±scale.
    const float fEGFM     = (eg2fm     - 2048.f) / 2048.f * 12.f;
    const float fEGFMFilt = (eg2filtfm - 2048.f) / 2048.f * 1.f;
    const float fEGWave   = (eg2wave   - 2048.f) / 2048.f * 1.f;

    // modulation LFO: two-zone control curve matching the sequencer UI.
    // 0..2048 => 0.01..1 Hz, 2049..4095 => 1..20 Hz.
    const float fLFOSpeed = decodeLfoRateHz(lfospeed);
    constexpr float kPi = 3.14159265358979323846f;
    const float lfoPhaseRad = static_cast<float>(lfophase.load()) / 4095.f * kPi;

    MK_FLT_PAR_ABS_NOCV(fLFOAM, lfo2am, 4095.f, 1.f)
    MK_FLT_PAR_ABS_NOCV(fLFOFM, lfo2fm, 4095.f, 12.f)
    MK_FLT_PAR_ABS_NOCV(fLFOFMFilt, lfo2filtfm, 4095.f, 1.f);
    MK_FLT_PAR_ABS_NOCV(fLFOWave, lfo2wave, 4095.f, 1.f)

    MK_FLT_PAR_ABS_SFT_NOCV(fTune, tune, 2048.f, 1.f)

    // Shared filter params. Keep this path aligned with RackPolyPad:
    // cutoff/resonance drive the Braids SVF's 14-bit integer domain and
    // Type is the compact LP/BP/HP enum.
    int32_t baseCut = fcut * 4;
    CONSTRAIN(baseCut, 1750, 16384)
    int32_t resonance = freso * 8;
    CONSTRAIN(resonance, 0, 32767)
    const int32_t filterType = decodeDirectOrNormalizedRange(fmode, 2);
    const braids::SvfMode filterMode = static_cast<braids::SvfMode>(filterType);
    const int wtMode = std::clamp<int>(mode.load(), 0, 36);
    const bool monoUnison = wtMode > 0;
    const float monoDetuneSemitones = static_cast<float>(wtMode - 1) * 0.01f;

    if (monoUnison) {
        int sourceVoice = -1;
        uint32_t newestSerial = 0;
        for (int i = 0; i < kNumVoices; i++) {
            if (voices[i].note_held && (sourceVoice < 0 || voices[i].note_serial >= newestSerial)) {
                sourceVoice = i;
                newestSerial = voices[i].note_serial;
            }
        }
        if (sourceVoice >= 0) {
            const int monoNote = voices[sourceVoice].midi_note;
            const uint8_t monoVelocity = voices[sourceVoice].pending_velocity;
            for (auto &voice : voices) {
                if (!voice.note_held || voice.midi_note != monoNote) {
                    voice.midi_note = monoNote;
                    voice.note_serial = newestSerial;
                    voice.gate = true;
                    voice.note_held = true;
                    voice.pending_velocity = monoVelocity;
                    voice.pending_retrigger = true;
                    voice.silence_tail_blocks = 0;
                }
            }
        }
    }

    for (int voiceIndex = 0; voiceIndex < kNumVoices; voiceIndex++) {
        auto &voice = voices[voiceIndex];
        const bool any_activity = voice.note_held || voice.pending_retrigger || !voice.adsr.IsIdle();
        if (any_activity) {
            voice.silence_tail_blocks = 0;
        } else {
            voice.silence_tail_blocks++;
            if (voice.silence_tail_blocks > kSilenceTailBlocks) {
                continue;
            }
        }

        // Force-retrigger pattern: ctagADSREnv::Gate(true) is a no-op when
        // currently in env_decay or env_sustain, so reset on every noteOn.
        const bool retrigger = voice.pending_retrigger;
        if (retrigger) {
            voice.pending_retrigger = false;
            voice.adsr.Reset();
        }
        voice.adsr.Gate(voice.note_held);
        voice.adsr.SetAttack(fAttack);
        voice.adsr.SetDecay(fDecay);
        voice.adsr.SetSustain(fSustain);
        voice.adsr.SetRelease(fRelease);
        voice.valADSR = voice.adsr.Process();

        const bool trigger = retrigger || (voice.preGate != voice.gate && voice.gate);
        if (trigger) {
            voice.lfo.SetFrequencyPhase(fLFOSpeed, voiceIndex == 0 ? 0.f : lfoPhaseRad);
        } else {
            voice.lfo.SetFrequency(fLFOSpeed);
        }
        voice.preGate = voice.gate;
        voice.valLFO = voice.lfo.Process();

        int32_t ipitch = static_cast<int32_t>(voice.midi_note * 128.0f);
        float fPitch = static_cast<float>(ipitch) / 128.f;
        const float unisonDetune = monoUnison
            ? ((voiceIndex == 0) ? -0.5f : 0.5f) * monoDetuneSemitones
            : 0.f;
        const float f0 = plaits::NoteToFrequency(
            fPitch + fTune * 12.f + unisonDetune + fLFOFM * voice.valLFO + fEGFM * voice.valADSR) * 0.998f;

        float voiceCut = static_cast<float>(baseCut);
        voiceCut += fEGFMFilt * voice.valADSR * 16383.f;
        voiceCut += fLFOFMFilt * voice.valLFO * 16383.f;
        CONSTRAIN(voiceCut, 0.f, 16383.f)
        voice.svf.set_frequency(static_cast<int16_t>(voiceCut));
        voice.svf.set_resonance(static_cast<int16_t>(resonance));
        voice.svf.set_mode(filterMode);

        const float fVel = voice.pending_velocity / 127.f;
        float fAM = voice.valADSR * fVel;
        fAM *= (1.f - (voice.valLFO + 1.f) * 0.5f * fLFOAM);
        CONSTRAIN(fAM, 0.f, 1.f)

        float fWt = fWave + voice.valADSR * fEGWave + voice.valLFO * fLFOWave * 2.f;
        CONSTRAIN(fWt, 0.f, 1.f)

        float voiceOut[BUF_SZ] = {0.f};
        if (isWaveTableGood) {
            voice.oscillator.Render(trigger, f0, fAM, fWt, wavetables, voiceOut, BUF_SZ);

            for (int i = 0; i < BUF_SZ; i++) {
                const int32_t filtered = voice.svf.Process(static_cast<int32_t>(voiceOut[i] * 32767.f));
                voiceOut[i] = static_cast<float>(filtered) / 32767.f;
            }
            for (int i = 0; i < BUF_SZ; i++) {
                if (monoUnison) {
                    out[i * 2 + voiceIndex] += voiceOut[i];
                } else {
                    out[i * 2] += voiceOut[i];
                    out[i * 2 + 1] += voiceOut[i];
                }
            }
        }
    }
}

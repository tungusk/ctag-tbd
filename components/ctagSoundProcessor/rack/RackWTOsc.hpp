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

#pragma once

#include "RackSynth.hpp"
#include "braids/analog_oscillator.h"
#include "braids/signature_waveshaper.h"
#include "braids/macro_oscillator.h"
#include "braids/settings.h"
#include "braids/quantizer.h"
#include "synthesis/RomplerVoiceMinimal.hpp"
#include "helpers/ctagSampleRom.hpp"
#include "helpers/ctagSineSource.hpp"
#include "helpers/ctagADSREnv.hpp"
#include "plaits/dsp/oscillator/wavetable_oscillator.h"

using namespace CTAG::SP;

class RackWTOsc {
public:
    void Process(const GrooveBoxRackProcessData &data);
    void Init(const GrooveBoxRackInitData *initdata);
	bool enabled;
    float out[BUF_SZ];
	void noteOn(uint8_t note, uint8_t vel);
	void noteOff(uint8_t note, uint8_t vel);

private:
	void prepareWavetables(HELPERS::ctagSampleRom *samplerom);
	plaits::WavetableOscillator<256, 64> oscillator;
	ctagSineSource lfo;
	ctagADSREnv adsr;
	stmlib::Svf svf;
	int16_t *buffer = NULL;
	float *fbuffer = NULL;
	float fWave = 0.f;
	const int16_t *wavetables[64];
	int currentBank = 0;
	int lastBank = -1;
	bool isWaveTableGood = false;
	float valADSR = 0.f, valLFO = 0.f;
	bool preGate = false;
	braids::Quantizer pitchQuantizer;
	float pre_fWt = 0.f;
	float midi_freq {0.0f};
	int midi_note {0};
	bool midi_trig {false};

	atomic<int32_t> gain;
	atomic<int32_t> pitch;
	atomic<int32_t> q_scale;
	atomic<int32_t> tune;
	atomic<int32_t> wavebank;
	atomic<int32_t> wave;
	atomic<int32_t> fmode;
	atomic<int32_t> fcut;
	atomic<int32_t> freso;
	atomic<int32_t> lfo2wave;
	atomic<int32_t> lfo2am;
	atomic<int32_t> lfo2fm;
	atomic<int32_t> lfo2filtfm;
	atomic<int32_t> eg2wave;
	atomic<int32_t> eg2am;
	atomic<int32_t> eg2fm;
	atomic<int32_t> eg2filtfm;
	atomic<int32_t> lfospeed;
	atomic<int32_t> lfosync;
	atomic<int32_t> egfasl;
	atomic<int32_t> attack;
	atomic<int32_t> decay;
	atomic<int32_t> sustain;
	atomic<int32_t> release;
};

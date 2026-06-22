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

#pragma once

#include "RackSynth.hpp"
#include "braids/analog_oscillator.h"
#include "braids/signature_waveshaper.h"
#include "braids/macro_oscillator.h"
#include "braids/settings.h"
#include "helpers/ctagSampleRom.hpp"
#include "helpers/ctagADEnv.hpp"

using namespace CTAG::SP;

class RackMO {
public:
    void Process(const GrooveBoxRackProcessData &data);
    void Init(const GrooveBoxRackInitData *initdata);
	bool enabled;
    float mo_out[32];
	void noteOn(uint8_t note, uint8_t vel);
	void noteOff(uint8_t note, uint8_t vel);

private:
	braids::MacroOscillatorShape mo_last_shape;
	braids::MacroOscillator mo_osc;
	braids::SignatureWaveshaper mo_ws;
	CTAG::SP::HELPERS::ctagADEnv mo_envelope;
	const uint8_t mo_sync[32] = {0};
	bool mo_prevTrigger = false;
	float mo_decimation_phase {0.0f};
	int16_t mo_decimated_sample {0};
	float mo_decimated_smooth_sample {0.0f};
	float midi_freq {0.0f};
	int midi_note {0};
	bool midi_trig {false};

	atomic<int16_t> mo_shape;
	atomic<int16_t> mo_pitch;
	atomic<int16_t> mo_decimation;
	atomic<int16_t> mo_param_0;
	atomic<int16_t> mo_param_1;
	atomic<int16_t> mo_waveshaping;
	atomic<int16_t> mo_fm_amt;
	atomic<int16_t> mo_p0_amt;
	atomic<int16_t> mo_p1_amt;
	atomic<int16_t> mo_loopEG;
	atomic<int16_t> mo_attack;
	atomic<int16_t> mo_decay;
	atomic<int16_t> mo_envMode;
};

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
#include "stmlib/dsp/filter.h"
#include "stmlib/utils/random.h"
#include "plaits/dsp/drums/synthetic_bass_drum.h"

using namespace CTAG::SP;

class RackDBD {
public:
    void Process(const GrooveBoxRackProcessData &data);
    void Init(const GrooveBoxRackInitData *initdata);
	bool enabled;
	float out[BUF_SZ];
	void trigger();

private:
	plaits::SyntheticBassDrum dbd;
	bool trig_prev {false};
	bool midi_trig {false};
	atomic<int16_t> accent;
	atomic<int16_t> f0;
	atomic<int16_t> tone;
	atomic<int16_t> decay;
	atomic<int16_t> dirty;
	atomic<int16_t> fm_env;
	atomic<int16_t> fm_dcy;
};

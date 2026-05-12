/***************
TBD-16 — Macro/Preset System & GrooveBoxRack

(c) 2025-2026 Per-Olov Jernberg (possan). https://possan.codes

Licensed under the GNU General Public License (GPL 3.0):
https://www.gnu.org/licenses/gpl-3.0.txt

A commercial licence is available — contact https://dadamachines.com/contact/

Provided "as is" without any express or implied warranties.
See LICENSE in the repository root for full terms.

SPDX-License-Identifier: GPL-3.0-only
***************/

#pragma once

#include "RackSynth.hpp"
#include "plaits/dsp/drums/hi_hat.h"

using namespace CTAG::SP;

class RackHH1 {
public:
    void Process(const GrooveBoxRackProcessData &data);
    void Init(const PickSeqRackInitData *initdata);
	bool enabled;
	float out[BUF_SZ];
	void trigger();

private:
	plaits::HiHat<plaits::SquareNoise, plaits::SwingVCA, true, false> hh1;
	bool trig_prev {false};
	bool midi_trig {false};
	float temp1[BUF_SZ];
	float temp2[BUF_SZ];
	atomic<int16_t> accent;
	atomic<int16_t> f0;
	atomic<int16_t> tone;
	atomic<int16_t> decay;
	atomic<int16_t> noise;
};

/***************
TBD-16 — Macro/Preset System & PicoSeqRack

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
#include "synthesis/FmKick.hpp"

using namespace CTAG::SP;

class RackFMB {
public:
    void Process(const PicoSeqRackProcessData &data);
    void Init(const PickSeqRackInitData *initdata);
	bool enabled;
	float out[BUF_SZ];
	void trigger();

private:
	CTAG::SYNTHESIS::FmKick fmb;
	bool trig_prev {false};
	bool midi_trig {false};
	atomic<int16_t> use_ratio_mode;
	atomic<int16_t> mod_env_sync;
	atomic<int16_t> f_b;
	atomic<int16_t> d_b;
	atomic<int16_t> f_m;
	atomic<int16_t> I;
	atomic<int16_t> d_m;
	atomic<int16_t> b_m;
	atomic<int16_t> A_f;
	atomic<int16_t> d_f;
};

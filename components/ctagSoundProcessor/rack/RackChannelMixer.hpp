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

using namespace CTAG::SP;

class RackChannelMixer {
public:
	void PreProcess(const GrooveBoxRackProcessData &data);
	void Init(const PickSeqRackInitData *initdata);
	bool enabled;
	float level;
	float pan;
	float send1;
	float send2;
	int cc_base;
	int track_length;
	float volumeMultiplier;

private:
	atomic<int16_t> mix_lev;
	atomic<int16_t> mix_device;
	atomic<int16_t> mix_pan;
	atomic<int16_t> mix_fx1;
	atomic<int16_t> mix_fx2;
	atomic<int16_t> mix_track_length;
};

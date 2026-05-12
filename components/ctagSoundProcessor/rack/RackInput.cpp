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

#include "RackSynth.hpp"
#include "RackInput.hpp"
#include "../ctagSoundProcessorGrooveBoxRack.hpp"

using namespace CTAG::SP;

void RackInput::Init(const PickSeqRackInitData *initdata) {
    this->enabled = false;
}

void RackInput::Process(const GrooveBoxRackProcessData &data) {
    if (!this->enabled) {
        return;
    }

    // std::fill_n(out, BUF_SZ, 0.f);
    memcpy(out, data.inputbuffer, sizeof(float) * 32 * 2);
}

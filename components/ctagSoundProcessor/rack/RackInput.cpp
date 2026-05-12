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

#include "RackSynth.hpp"
#include "RackInput.hpp"
#include "../ctagSoundProcessorPicoSeqRack.hpp"

using namespace CTAG::SP;

void RackInput::Init(const PickSeqRackInitData *initdata) {
    this->enabled = false;
}

void RackInput::Process(const PicoSeqRackProcessData &data) {
    if (!this->enabled) {
        return;
    }

    // std::fill_n(out, BUF_SZ, 0.f);
    memcpy(out, data.inputbuffer, sizeof(float) * 32 * 2);
}

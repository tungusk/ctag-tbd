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

#include <stdint.h>
#include <string>
#include <memory>
#include <map>
#include <functional>
#include <atomic>

#include "../ctagSoundProcessor.hpp"
#include "helpers/ctagSampleRom.hpp"

using namespace std;

#define BUF_SZ 32

namespace CTAG {
    namespace SP {
        class ctagSoundProcessorGrooveBoxRack;

        struct GrooveBoxRackProcessData {
            uint32_t firstNonWtSlice;
            HELPERS::ctagSampleRom *sampleRom;
            uint32_t msPerBeat;
            uint32_t tempo; // BPM * 100
            uint8_t quantum;
            float *inputbuffer;
        };

        struct PickSeqRackInitData {
            int track_index;
            const char *prefix;
            int midi_channel;
            int cc_base;
            ctagSoundProcessorGrooveBoxRack *rack;
        };
    }
}
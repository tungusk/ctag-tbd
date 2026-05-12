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

#include <string>
#include <vector>
#include "rapidjson/document.h"

namespace CTAG {
    namespace MACROPRESETS {
        const int MaxSynthDefinitionParameters = 24;

        enum SynthParameterType : uint8_t {
            SynthParameterType_None = 0,
            SynthParameterType_CC = 1,
            SynthParameterType_NRPM = 2,
        };

        enum SynthType : uint8_t {
            SynthType_None = 0,
            SynthType_Synth = 1,
            SynthType_Drum = 2,
        };

        struct SynthParameter {
            char id[16];
            char name[32];
            enum SynthParameterType type;
            uint16_t defaultValue;
            uint8_t cc;
        };

        struct SynthDefinition {
            char id[16];
            char name[32];
            enum SynthType type;
            struct SynthParameter parameters[MaxSynthDefinitionParameters];
        };

        class SynthDefinitionUtils final {
        public:
            SynthDefinitionUtils() = delete;

            static void SynthDefinition_Reset(struct SynthDefinition *def);
            static bool SynthDefinition_DeserializeJSON(struct SynthDefinition *def, const rapidjson::Value &jsonelement);
        };
    }
}
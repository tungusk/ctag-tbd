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
        const int MaxOutputMappingSources = 8;
        const int MaxOutputMappings = 16;

        // Response curve types for parameter mapping
        enum class MacroCurveType : uint8_t {
            Linear = 0,   // Default: straight 1:1 mapping
            Log    = 1,   // Logarithmic: for frequency/cutoff (pitch is logarithmic)
            Exp    = 2,   // Exponential: for decay/envelope times (resolution for short times)
        };

        struct MacroDeviceOutputMappingSource {
            uint8_t parameterIndex;
            int32_t multiplier;
            int32_t divider;
            MacroCurveType curve;
        };

        struct MacroDeviceOutputMapping {
            uint8_t ctrl;
            int16_t startValue;
            struct MacroDeviceOutputMappingSource sources[MaxOutputMappingSources];
        };

        struct MacroDeviceDefinition {
            char id[16];
            char name[32];
            char synthId[16];
            float volumeMultiplier;
            struct MacroDeviceOutputMapping outputMappings[MaxOutputMappings];
        };

        class MacroDeviceDefinitionUtils final {
            public:
            MacroDeviceDefinitionUtils() = delete;

            static void MacroDeviceDefinition_CopyInto(const struct MacroDeviceDefinition *source, struct MacroDeviceDefinition *target);
            static void MacroDeviceDefinition_Reset(struct MacroDeviceDefinition *def);
            static bool MacroDeviceDefinition_DeserializeJSON(struct MacroDeviceDefinition *def, const rapidjson::Value &jsonelement);
        };
    }
}
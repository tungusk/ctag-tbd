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

#include <string>
#include <vector>
#include "rapidjson/document.h"

namespace CTAG {
    namespace MACROPRESETS {
        const int MaxTrackDefinitionMachineIds = 8;

        struct TrackDefinition {
            int index;
            char name[16];
            int midiChannel;
            int drumNote;
            int baseCC;
            char macroMachineIds[MaxTrackDefinitionMachineIds][16];
        };

        class TrackDefinitionUtils final {
        public:
            TrackDefinitionUtils() = delete;

            static void TrackDefinition_Reset(struct TrackDefinition *def);
            static bool TrackDefinition_DeserializeJSON(struct TrackDefinition *def, const rapidjson::Value &jsonelement);
        };
    }
}
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

#include <vector>
#include <set>
#include <string>
#include <stdint.h>
#include <iostream>
#include <memory>
#include "rapidjson/document.h"

namespace CTAG {
    namespace MACROPRESETS {
        const int MaxMacroSoundPresetParameters = 24;
        const int MaxPresetsPerGroup = 64;

        struct MacroSoundPreset {
            char id[32];
            char displayName[32];
            char groupName[32];
            char macroDeviceId[32];
            uint32_t validTracksBitmask;
            int32_t parameterValues[MaxMacroSoundPresetParameters];
        };

        struct MacroSoundPresetGroup {
            char id[32];
            char displayName[32];
            uint32_t validTracksBitmask;
            uint8_t numFileIds;
            char fileIds[MaxPresetsPerGroup][32];
        };

        class MacroSoundPresetUtils final {
            public:
            MacroSoundPresetUtils() = delete;

            static bool MacroSoundPreset_DeserializeJSON(struct MacroSoundPreset *preset, const rapidjson::Value &jsonelement);
            static void MacroSoundPreset_Reset(struct MacroSoundPreset *preset);

            static void MacroSoundPresetGroup_Reset(struct MacroSoundPresetGroup *group);
        };
    }
}
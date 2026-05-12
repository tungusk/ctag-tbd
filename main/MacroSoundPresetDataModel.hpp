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
#include <string>
#include "ctagDataModelBase.hpp"


namespace CTAG {
    namespace MACROPRESETS {
        const int MaxSoundPresets = 256;
        const int MaxSoundPresetGroups = 64;

        class MacroSoundPresetDataModel final : public CTAG::SP::ctagDataModelBase{
            private:
                struct MacroSoundPreset* presets;
                int presetsUsed; 
                struct MacroSoundPresetGroup* groups;
                int groupsUsed;
            public:
                void Init();
                void ReloadSoundPresets();
                int GetNumberOfSoundPresetGroups();
                void GetMacroSoundPresetGroupId(int index, std::string *idOutput);
                int GetNumberOfSoundPresets();
                void GetPresetIndexJson(int trackIndex, std::string *output);
                void SerializeListJSON(std::string *output);
                void SerializeItemJSON(const std::string &id, std::string *output);
                void LoadMacroSoundPreset(MacroSoundPreset *target, const std::string id);
                bool UpdatePreset(const std::string &jsonString);
                void DeleteItem(const std::string &id);
                void SerializeListInto(int trackIndex, rapidjson::Document &doc);
                bool PutSamplePresetJSON(const string &presetJSON);
                static MacroSoundPresetDataModel &instance();
        };
    }
}
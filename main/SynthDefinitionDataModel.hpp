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

#include <string>
#include <vector>
#include "ctagDataModelBase.hpp"
#include "SynthDefinition.hpp"
#include "TrackDefinition.hpp"


#define MAX_TRACKS 20
#define MAX_SYNTHS 24

namespace CTAG {
    namespace MACROPRESETS {

        class SynthDefinitionDataModel final : public CTAG::SP::ctagDataModelBase {
            private:
                class SynthDefinition *synths;
                class TrackDefinition *tracks;

            public:
                // SynthDefinitionDataModel(const SynthDefinitionDataModel&) = delete;

                void Init();
                void ReloadSynthDefinitions();
                int GetNumberOfSynthDefinitions();
                void GetSynthDeviceDefinitionId(int index, std::string *idOutput);
                void GetSynthDefinitionsJSON(const std::string *output);
                SynthDefinition *GetSynthDefinition(const std::string id);
                TrackDefinition *GetTrackDefinition(int index);
                bool DeserializeJSON(const rapidjson::Value &jsonelement);
                void SerializeJSON(std::string *output);
                void SerializeListJSON(std::string *output);

                static SynthDefinitionDataModel *instance();
        };
    }
}
/***************
TBD-16 — Macro/Preset System & GrooveBoxRack

(c) 2024-2026 Per-Olov Jernberg (possan). https://possan.codes
(c) 2024-2026 Johannes Elias Lohbihler for dadamachines.
Based in part on the CTAG TBD DrumRack / engine by Robert Manzke (CTAG Kiel).

Licensed under the GNU General Public License (GPL 3.0):
https://www.gnu.org/licenses/gpl-3.0.txt

A commercial licence is available — contact https://dadamachines.com/contact/

Provided "as is" without any express or implied warranties.
See LICENSE in the repository root for full terms.

SPDX-License-Identifier: GPL-3.0-only
***************/

#include "TrackDefinition.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"


using namespace CTAG::MACROPRESETS;
using namespace rapidjson;


void CTAG::MACROPRESETS::TrackDefinitionUtils::TrackDefinition_Reset(struct TrackDefinition *def) {
    def->index = 0;
    memset(def->name, 0, sizeof(def->name));
    def->midiChannel = 0;
    def->drumNote = 0;
    def->baseCC = 0;
    memset(def->macroMachineIds, 0, sizeof(def->macroMachineIds));
}

bool CTAG::MACROPRESETS::TrackDefinitionUtils::TrackDefinition_DeserializeJSON(struct TrackDefinition *def, const Value &jsonelement) {
    if (!jsonelement.HasMember("index")) return false;
    if (!jsonelement.HasMember("name")) return false;
    if (!jsonelement.HasMember("midichannel")) return false;
    if (!jsonelement.HasMember("drumnote")) return false;
    if (!jsonelement.HasMember("basecc")) return false;

    def->index = jsonelement["index"].GetInt();
    strncpy(def->name, jsonelement["name"].GetString(), sizeof(def->name) - 1);
    def->name[sizeof(def->name) - 1] = '\0';
    def->midiChannel = jsonelement["midichannel"].GetInt();
    def->drumNote = jsonelement["drumnote"].GetInt();
    def->baseCC = jsonelement["basecc"].GetInt();

    memset(def->macroMachineIds, 0, sizeof(def->macroMachineIds));
    if (jsonelement.HasMember("machines") && jsonelement["machines"].IsArray()) {
        int i = 0;
        for (auto &v : jsonelement["machines"].GetArray()) {
            if (i < MaxTrackDefinitionMachineIds) {
                strncpy(def->macroMachineIds[i], v.GetString(), sizeof(def->macroMachineIds[i]) - 1);
                def->macroMachineIds[i][sizeof(def->macroMachineIds[i]) - 1] = '\0';
                i++;
            }
        }
    }

    return true;
}

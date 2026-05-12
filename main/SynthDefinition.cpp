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

#include "SynthDefinition.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"


using namespace CTAG::MACROPRESETS;
using namespace rapidjson;


void SynthParameter_Reset(struct SynthParameter *param) {
    memset(param->id, 0, sizeof(param->id));
    memset(param->name, 0, sizeof(param->name));
    param->type = SynthParameterType_None;
    param->defaultValue = 0;
    param->cc = 0;
}


void CTAG::MACROPRESETS::SynthDefinitionUtils::SynthDefinition_Reset(struct SynthDefinition *def) {
    memset(def->id, 0, sizeof(def->id));
    memset(def->name, 0, sizeof(def->name));
    def->type = SynthType_None;
    for(int pi=0; pi<MaxSynthDefinitionParameters; pi++) {
        SynthParameter_Reset(&def->parameters[pi]);
    }
}

bool CTAG::MACROPRESETS::SynthDefinitionUtils::SynthDefinition_DeserializeJSON(struct SynthDefinition *def, const Value &jsonelement) {
    if (!jsonelement.HasMember("id")) return false;
    if (!jsonelement["id"].IsString()) return false;
    strncpy(def->id, jsonelement["id"].GetString(), sizeof(def->id) - 1);

    if (!jsonelement.HasMember("name")) return false;
    if (!jsonelement["name"].IsString()) return false;
    strncpy(def->name, jsonelement["name"].GetString(), sizeof(def->name) - 1);

    if (!jsonelement.HasMember("type")) return false;
    if (!jsonelement["type"].IsString()) return false;

    std::string typestring = jsonelement["type"].GetString();
    def->type = SynthType_None;
    if (typestring == "synth") {
        def->type = SynthType_Synth;
    }
    if (typestring == "drum") {
        def->type = SynthType_Drum;
    }

    int index = 0;
    if (jsonelement.HasMember("parameters") && jsonelement["parameters"].IsArray()) {
        for (auto &v : jsonelement["parameters"].GetArray()) {
            if (!v.HasMember("id")) return false;
            if (!v.HasMember("name")) return false;
            if (!v.HasMember("type")) return false;
            if (!v.HasMember("def")) return false;
            if (!v.HasMember("ctrl")) return false;

            SynthParameter *p = &def->parameters[index];
            strncpy(p->id, v["id"].GetString(), sizeof(p->id) - 1);
            strncpy(p->name, v["name"].GetString(), sizeof(p->name) - 1);
            std::string typestring = v["type"].GetString();
            if (typestring == "cc") {
                p->type = SynthParameterType_CC;
            } else if (typestring == "nrpm") {
                p->type = SynthParameterType_NRPM;
            } else {
                p->type = SynthParameterType_None;
            }
            p->defaultValue = v["def"].GetUint();
            p->cc = v["ctrl"].GetUint();
            index ++;
        }
    }

    return true;
}

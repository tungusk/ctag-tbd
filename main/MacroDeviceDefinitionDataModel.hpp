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

#pragma once

#include <string>
#include <vector>
#include "MacroDeviceDefinition.hpp"
#include "ctagDataModelBase.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace CTAG {
    namespace MACROPRESETS {
        const int MaxMacroDeviceDefinitions = 64;

        class MacroDeviceDefinitionDataModel final : public CTAG::SP::ctagDataModelBase{
            private:
                struct MacroDeviceDefinition *definitions;
                SemaphoreHandle_t arrayMutex = nullptr;
            public:
                void Init();
                void ReloadMachineDefinitions();
                bool ReloadSingleDefinition(const std::string &id);
                int GetNumberOfDefinitions();
                struct MacroDeviceDefinition *GetMacroDeviceDefinition(const char *id);
                void SerializeListJSON(std::string *output);
                void SerializeItemJSON(const std::string &id, std::string *output);
                bool UpdateDefinition(const std::string &jsonString);
                void DeleteItem(const std::string &id);
                static MacroDeviceDefinitionDataModel &instance();
        };
    }
}
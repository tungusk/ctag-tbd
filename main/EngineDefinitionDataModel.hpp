/***************
TBD-16 — Macro/Preset System & PicoSeqRack

(c) 2025-2026 Per-Olov Jernberg (possan). https://possan.codes

Licensed under the GNU Lesser General Public License (LGPL 3.0).
https://www.gnu.org/licenses/lgpl-3.0.txt

dadamachines has a commercial license to use this code in the TBD-16 product.
Other commercial use requires a separate license agreement.

Provided "as is" without any express or implied warranties.

License and copyright details for specific submodules are included in their
respective component folders / files if different from this license.
***************/

#pragma once

#include <string>
#include "EngineDefinition.hpp"
#include "TrackDefinition.hpp"
#include "rapidjson/document.h"


namespace CTAG {
    namespace MACROPRESETS {

        class EngineDefinitionDataModel final {
            public:
                int GetNumberOfSynthDefinitions();
                const EngineDef    *GetSynthDefinition(const std::string &id);
                const TrackDef     *GetTrackDefinition(int index);
                bool SerializeIntoJSON(rapidjson::Document &doc);

                void WriteListResponse(struct GetEngineDefinitionIdListResponse *response);
                void WriteEngineDefinitionPageResponse(const struct GetEngineDefinitionsPageRequest *request, struct GetEngineDefinitionsPageResponse *response);

                static EngineDefinitionDataModel *instance();
        };
    }
}
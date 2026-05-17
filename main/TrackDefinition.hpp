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

#include <stdint.h>

const int MaxTrackDefinitionEngineIds = 8;
const int MaxTrackDefinitionEngineIdLength = 16;

enum TrackType : uint8_t {
    TRACK_TYPE_UNKNOWN = 0,
    TRACK_TYPE_DRUM = 1,
    TRACK_TYPE_SYNTH = 2,
    TRACK_TYPE_FX = 3,
    TRACK_TYPE_AUDIOINPUT = 4,
};

struct TrackDefinition {
    uint8_t index;
    enum TrackType type;
    int8_t midiChannel;
    int8_t drumNote;
    int8_t baseCC;
    char engineIdStr[MaxTrackDefinitionEngineIds][MaxTrackDefinitionEngineIdLength];
    char name[16];
};

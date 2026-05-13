/***************
TBD-16 — GrooveBoxRack drum machine template (used by rackgen.js).

Replace RackTemplateDrum with your class name and fill in the DSP in Process().
The rackgen markers below are placeholders for the atomic param members the
generator emits — leave them where they are.

SPDX-License-Identifier: GPL-3.0-only
***************/

#pragma once

#include <atomic>
#include "RackSynth.hpp"

using namespace CTAG::SP;
using namespace std;

class RackTemplateDrum {
public:
    void Init(const GrooveBoxRackInitData *initdata);
    void Process(const GrooveBoxRackProcessData &data);
    void trigger();

    bool  enabled;
    float out[BUF_SZ];

private:
    bool midi_trig;
    bool trig_prev;
    // rackgen: atomic param members
    // rackgen:hppMembers
    // rackgen:hppMembers
};

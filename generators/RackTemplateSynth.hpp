/***************
TBD-16 — GrooveBoxRack synth machine template (used by rackgen.js).

Replace RackTemplateSynth with your class name and fill in the DSP in Process().
SPDX-License-Identifier: GPL-3.0-only
***************/

#pragma once

#include <atomic>
#include "RackSynth.hpp"

using namespace CTAG::SP;
using namespace std;

class RackTemplateSynth {
public:
    void Init(const GrooveBoxRackInitData *initdata);
    void Process(const GrooveBoxRackProcessData &data);
    void noteOn(uint8_t note, uint8_t velocity);
    void noteOff(uint8_t note, uint8_t velocity);

    bool  enabled;
    float out[BUF_SZ];

private:
    bool    gate;
    uint8_t cur_note;
    uint8_t cur_vel;
    // rackgen: atomic param members
    // rackgen:hppMembers
    // rackgen:hppMembers
};

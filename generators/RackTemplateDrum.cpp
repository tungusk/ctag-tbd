/***************
TBD-16 — GrooveBoxRack drum machine template (used by rackgen.js).
SPDX-License-Identifier: GPL-3.0-only
***************/

#include <algorithm>
#include "RackSynth.hpp"
#include "RackTemplateDrum.hpp"
#include "../ctagSoundProcessorGrooveBoxRack.hpp"

using namespace CTAG::SP;

void RackTemplateDrum::Init(const GrooveBoxRackInitData *initdata) {
    // rackgen: parameter registrations
    // rackgen:cppRegs
    // rackgen:cppRegs

    this->enabled   = false;
    this->midi_trig = false;
    this->trig_prev = false;

    // TODO: initialise your DSP here (clear buffers, set up filters, etc.)
}

void RackTemplateDrum::trigger() {
    midi_trig = true;
}

void RackTemplateDrum::Process(const GrooveBoxRackProcessData &data) {
    bool _trig = false;
    if (midi_trig) { _trig = true; midi_trig = false; }
    trig_prev = _trig;
    if (!this->enabled) return;

    std::fill_n(out, BUF_SZ, 0.f);

    // Scale your raw 0..4096 param values into useful ranges, e.g.:
    //   MK_FLT_PAR_ABS_NOCV(fDecay, decay, 4095.f, 1.f)              // -> 0..1
    //   MK_FLT_PAR_ABS_MIN_MAX_NOCV(fMs, decay, 4095.f, 5.f, 2000.f) // -> 5..2000
    // (these macros come from ctagSoundProcessorGrooveBoxRack.hpp.)

    // TODO: render BUF_SZ mono samples into out[].
    // for (int i = 0; i < BUF_SZ; i++) out[i] = /* your DSP */;
}

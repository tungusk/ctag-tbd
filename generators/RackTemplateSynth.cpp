/***************
TBD-16 — GrooveBoxRack synth machine template (used by rackgen.js).
SPDX-License-Identifier: GPL-3.0-only
***************/

#include <algorithm>
#include "RackSynth.hpp"
#include "RackTemplateSynth.hpp"
#include "../ctagSoundProcessorGrooveBoxRack.hpp"

using namespace CTAG::SP;

void RackTemplateSynth::Init(const GrooveBoxRackInitData *initdata) {
    // rackgen: parameter registrations
    // rackgen:cppRegs
    // rackgen:cppRegs

    this->enabled  = false;
    this->gate     = false;
    this->cur_note = 60;
    this->cur_vel  = 0;

    // TODO: initialise your DSP here.
}

void RackTemplateSynth::noteOn(uint8_t note, uint8_t velocity) {
    cur_note = note;
    cur_vel  = velocity;
    gate     = (velocity > 0);
}

void RackTemplateSynth::noteOff(uint8_t note, uint8_t velocity) {
    if (note == cur_note) gate = false;
}

void RackTemplateSynth::Process(const GrooveBoxRackProcessData &data) {
    if (!this->enabled) return;

    std::fill_n(out, BUF_SZ, 0.f);

    // Param scaling (see ctagSoundProcessorGrooveBoxRack.hpp for macros):
    //   MK_FLT_PAR_ABS_NOCV(fCutoff, cutoff, 4095.f, 1.f)
    //   MK_FLT_PAR_ABS_MIN_MAX_NOCV(fDecay, decay, 4095.f, 5.f, 2000.f)
    //
    // float freq = stmlib::SemitonesToRatio(cur_note - 69) * 440.f;  // cur_note → Hz

    // TODO: render BUF_SZ mono samples into out[] based on `gate` / `cur_note` / `cur_vel`.
    // for (int i = 0; i < BUF_SZ; i++) out[i] = /* your DSP */;
}

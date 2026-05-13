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

#include "RackSynth.hpp"
#include "RackClap.hpp"
#include "../ctagSoundProcessorGrooveBoxRack.hpp"

using namespace CTAG::SP;

void RackClap::Init(const GrooveBoxRackInitData *initdata) {
    cl.Init();

    initdata->rack->registerParamAndCC(initdata, "f0", 8, [&](const int val){ f0 = val;});
    initdata->rack->registerParamAndCC(initdata, "tone", 9, [&](const int val){ tone = val;});
    initdata->rack->registerParamAndCC(initdata, "decay", 10, [&](const int val){ decay = val;});
    initdata->rack->registerParamAndCC(initdata, "scale", 11, [&](const int val){ scale = val;});
    initdata->rack->registerParamAndCC(initdata, "transient", 12, [&](const int val){ transient = val;});

    this->enabled = false;
}

void RackClap::trigger() {
    midi_trig = true;
}

void RackClap::Process(const GrooveBoxRackProcessData &data) {
    if (!this->enabled) {
        return;
    }

    std::fill_n(out, BUF_SZ, 0.f);

    MK_FLT_PAR_ABS_MIN_MAX_NOCV(_pitch1_, f0, 4095.f, 350.f, 4000.f)
    MK_FLT_PAR_ABS_MIN_MAX_NOCV(_pitch2_, f0, 4095.f, 300.f, 3000.f)
    MK_FLT_PAR_ABS_MIN_MAX_NOCV(_reso1_, tone, 4095.f, 1.f, 2.5f)
    MK_FLT_PAR_ABS_MIN_MAX_NOCV(_reso2_, tone, 4095.f, 0.75f, 6.5f)
    MK_FLT_PAR_ABS_MIN_MAX_NOCV(_decay1_, decay, 4095.f, 0.05f, 0.3f)
    MK_FLT_PAR_ABS_MIN_MAX_NOCV(_decay2_, decay, 4095.f, 0.05f, 2.f)
    MK_FLT_PAR_ABS_MIN_MAX_NOCV(_scale_attack_, scale, 4095.f, 0.f, 0.1f)
    MK_FLT_PAR_ABS_MIN_MAX_NOCV(_scale_trans, scale, 4095.f, 1.f, 3.f)
    MK_INT_PAR_ABS_NOCV(_trans_, transient, 16)

    cl.params.pitch1 = _pitch1_ / 44100.f;
    cl.params.pitch2 = _pitch2_ / 44100.f;
    cl.params.reso1 = _reso1_;
    cl.params.reso2 = _reso2_;
    cl.params.decay1 = _decay1_;
    cl.params.decay2 = _decay2_;
    cl.params.attack = _scale_attack_;
    cl.params.scale = _scale_trans;
    cl.params.transient = _trans_ % 16;

    // MK_BOOL_PAR_NOCV(_trig, trigger)
    bool _trig = false;
    if (midi_trig) {
        _trig = true;
        midi_trig = false;
    }
    if (_trig != trig_prev){
        if (_trig) {
            // printf("CL\n");
            cl.Trigger();
        }
        trig_prev = _trig;
    }

    cl.Process(out, BUF_SZ);

    if (out[0] != out[0]) {
        // Audio-thread: no printf — see RackABD.cpp note. Recovery via Init below.
        // printf("DrumRackCL: NaN detected!\n");
        cl.Init();
    }
}

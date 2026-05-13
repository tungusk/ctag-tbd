/***************
TBD-16 — GrooveBoxRack: simulator-only overrides.

This whole file is a no-op on the device build (the contents are wrapped in
`#ifdef TBD_SIM`).  On the device, the macro/preset/RP2350 layer drives the
rack's FX1 / FX2 / Master settings as the user loads a kit; the simulator has
no such layer, so the values come straight from mp-GrooveBoxRack.json — which
is tuned for the hardware gain staging and would otherwise blow the rack's raw
output to ~20× (heavily clipped by the sim's tanh limiter).

We override `loadPresetInternal()` here so that, after every preset load (boot,
host re-load, user clicking a preset in the WebUI), the master and FX bus get
sim-friendly defaults — clean master, compressor bypassed, FX *returns* audible
so per-track FX-send knobs in the WebUI actually do something.  Every value
stays editable from the WebUI's master + FX strips at the bottom of the
GrooveBoxRack view; this is just the starting state.

SPDX-License-Identifier: GPL-3.0-only
(c) 2026 Johannes Elias Lohbihler for dadamachines.
***************/

#ifdef TBD_SIM

#include "ctagSoundProcessorGrooveBoxRack.hpp"

using namespace CTAG::SP;

void ctagSoundProcessorGrooveBoxRack::loadPresetInternal() {
    // 1) apply preset values normally (mp-GrooveBoxRack.json → pMapPar setters)
    ctagSoundProcessor::loadPresetInternal();

    // 2) override the FX1 / FX2 / Master globals with sim-friendly defaults
    //    (see file header for the why)
    sum_mute        = 0;
    sum_lev         = 1500;     // fMixLevel² ≈ 1.21 → unity-ish for a single voice
    // master compressor — bypassed (clean dry mix; turn it on from the WebUI when needed)
    c_mix           = 0;        // PAN-mapped: 0 → all dry, no compressor in path
    c_gain          = 0;        // 0 dB makeup
    c_thres         = 4095;     // threshold 0 dB → effectively no compression
    c_ratio         = 0;        // 1:1
    c_lpf           = 0;        // sidechain LPF off
    c_dly_level     = 0;
    c_rev_level     = 0;
    // FX1 (stereo delay) — bus return audible, musical default
    fx1_amount      = 1500;     // master delay return ≈ 37 % (audible when a track sends to it)
    fx1_fx_send     = 0;        // no global feed into the delay
    fx1_feedback    = 1000;     // ~25 % feedback — graceful decay
    fx1_time_ms     = 256;      // ≈ a quarter note at 120 BPM (unit: step × msPerBeat / 8)
    fx1_sync        = 0;
    fx1_freeze      = 0;
    fx1_tape_digital= 0;
    fx1_st_width    = 2048;     // 50 % L/R width
    fx1_base        = 1024;     // band-pass on the feedback loop, low-ish
    fx1_width       = 2048;
    // FX2 (reverb) — bus return audible, medium hall
    fx2_amount      = 2000;     // master reverb return ≈ 49 % (audible when a track sends to it)
    fx2_time        = 2048;     // medium decay
    fx2_lp          = 2048;     // medium LP on the reverb's input
}

#endif // TBD_SIM

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

#include "RackSynth.hpp"

using namespace CTAG::SP;

class RackInput {
public:
    void Process(const GrooveBoxRackProcessData &data);
    void Init(const GrooveBoxRackInitData *initdata);
    bool enabled;
    float out[BUF_SZ * 2];

    // Post-ADC input-processing params (wired as ctrls 8..15).
    // Values arrive in the 0..4095 normalised DSP range (the ESP32-P4
    // param callback handles CC 0..127 → 0..4064 and NRPM 0..16383 → 0..4095
    // scaling upstream). See RackInput.cpp Process() for the param-to-effect
    // math.
    int in_gain;     // ctrl 8  — pre-fader gain (0..4095 → -60..+24 dB, PT_GAIN_LEVEL)
    int in_mono;     // ctrl 9  — mono collapse (0/1)
    int in_hp;       // ctrl 10 — HP pre-filter cutoff (0..4095 → 0..500 Hz)
    int in_drive;    // ctrl 11 — soft-clip drive (0..4095 → 1× .. 4×)
    int in_ftype;    // ctrl 12 — filter mode (0..4095 → Off/LP/BP/HP, PT_FILTER_MODE)
    int in_fcutoff;  // ctrl 13 — SVF cutoff (0..4095 → 20 Hz..12 kHz log, PT_FILTER_CUTOFF)
    int in_freso;    // ctrl 14 — SVF resonance (0..4095 → 0..0.95, PT_FILTER_Q)
    int in_fenv;     // ctrl 15 — envelope-to-cutoff amount (0..4095 → 0..100%, PT_ENV_AMOUNT)

    // Per-channel one-pole HP pre-filter state (L, R).
    float hp_x[2];
    float hp_y[2];

    // Stereo SVF state (Cytomic/TPT topology, two-integrator) per channel.
    // See RackInput.cpp Process() for the per-sample recursion.
    float svf_ic1[2];
    float svf_ic2[2];

    // Mono envelope follower state driven by the post-drive signal magnitude.
    // Fed through the SVF cutoff modulator when in_fenv > 0.
    float env_level;
};

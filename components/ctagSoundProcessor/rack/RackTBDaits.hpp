/***************
TBD-16 — Macro/Preset System & GrooveBoxRack
RackTBDaits: Plaits (Mutable Instruments) extended-engine wrapper for the rack.

(c) 2024-2026 Johannes Elias Lohbihler for dadamachines.
Based in part on the CTAG TBD platform by Robert Manzke (CTAG Kiel).

Licensed under the GNU General Public License (GPL 3.0):
https://www.gnu.org/licenses/gpl-3.0.txt

A commercial licence is available — contact https://dadamachines.com/contact/

Provided "as is" without any express or implied warranties.
See LICENSE in the repository root for full terms.

SPDX-License-Identifier: GPL-3.0-only
***************/

#pragma once

#include "RackSynth.hpp"
#include "plaits/dsp/voice.h"

using namespace CTAG::SP;

class RackTBDaits {
public:
    void Process(const GrooveBoxRackProcessData &data);
    void Init(const GrooveBoxRackInitData *initdata);
    void noteOn(uint8_t note, uint8_t vel);
    void noteOff(uint8_t note, uint8_t vel);

    bool enabled {false};
    // Stereo output: out = main engine output, out+1 = aux (Plaits aux channel —
    // most engines mirror or provide a related secondary signal).
    float aits_out_stereo[BUF_SZ * 2] {};

private:
    // Plaits Voice + its 16 KB shared allocator workspace are PSRAM-allocated
    // in Init. The Voice has 22 engine instances inline (incl. 3 Six-Op DX7
    // voicings) — too large to keep as an inline member without overflowing
    // GrooveBoxRack's ctagSPAllocator block (300 KB).
    plaits::Voice *voice {nullptr};
    char *shared_buffer {nullptr};
    plaits::Patch patch {};
    plaits::Modulations modulations {};

    // Note state.
    int midi_note {60};
    bool note_held {false};
    uint8_t pending_velocity {100};
    // 2-block trigger pulse state machine. noteOn loads 2; Process emits one
    // block of trigger=0 then one block of trigger=1, guaranteeing a 0→1
    // edge for Plaits' internal trigger detector even on repeated same-note
    // sequencer triggers (where note_held stays true between calls).
    int trigger_pulse_state {0};

    // AHR envelope state for the wrapper-side amplitude / accent envelope.
    //   Attack: instant snap to (velocity/127) on noteOn.
    //   Hold:   stays at peak while note_held — keyboard sustain.
    //   Release: exponential decay per Decay knob (5 ms..8 s tau)
    //     when note_held drops to false.
    // Drives modulations.level (with level_patched=true) so:
    //   - LPG (ProcessLP) audibly follows the envelope → Decay shapes tail
    //   - DX7 patches see velocity-shaped accent → per-patch velocity-to-
    //     timbre routing preserved (the Plaits-with-envelope-patched feel)
    //   - Note length actually affects sustain (matters for keyboard play)
    float env_value {0.f};

    // Block counter for silence gate: counts up since the last trigger /
    // active note. After kSilenceTailBlocks of pure idle we mute the output
    // and skip Voice::Render entirely (CPU + correctness — drone engines
    // like Speech/Chord/Noise never naturally fall silent without LPG).
    int silence_tail_blocks {0};

    // ---- Plaits-mapped params (CCs 8..18) ----
    // Defaults pre-set to the cv-space values that match the preset's
    // post-load state, so the very first Process block produces musical
    // output even before the macro engine pushes preset values.
    //
    // Default model = 2 (Six-Op DX7 instance A) so the user immediately
    // hears DX7-style FM out of the box — which is the headline feature.
    atomic<int16_t> model_par {320};      // preset 2 (cc src) × mul 5 → wire 10 → cv 320 → engine 2 (DX7A) via wrapper / 160
    atomic<int16_t> freq_par {2048};      // preset 8192 → cv 2048 → no detune
    atomic<int16_t> harm_par {2048};      // preset 8192 → cv 2048 → 0.5
    atomic<int16_t> timbre_par {2048};    // preset 8192 → cv 2048 → 0.5
    atomic<int16_t> morph_par {2048};     // preset 8192 → cv 2048 → 0.5
    atomic<int16_t> decay_par {3250};     // preset 13000 → cv 3250 → ~0.79 LPG decay (audible sustain for lead patches, ~850 ms display)
    atomic<int16_t> color_par {2048};     // LPG tone/colour
    atomic<int16_t> mix_par {2048};       // OUT/AUX blend
    atomic<int16_t> fmod_par {0};         // preset 0 → no FM mod by default
    atomic<int16_t> tmod_par {0};         // preset 0 → no Timbre mod by default
    atomic<int16_t> mmod_par {0};         // preset 0 → no Morph mod by default
    atomic<int16_t> width_par {2048};     // stereo width for OUT/AUX or mono-ish engine widening

    float mix_smooth {0.5f};
    float width_smooth {0.5f};
    float decorrelator_buffer[128] {};
    int decorrelator_idx {0};
    float decorrelator_lfo_phase {0.f};
    int previous_engine {-1};
    int mute_after_sixop_switch_blocks {0};
};

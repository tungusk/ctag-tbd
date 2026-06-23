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

#include "RackSynth.hpp"
#include "RackTBDaits.hpp"
#include "../ctagSoundProcessorGrooveBoxRack.hpp"
#include "stmlib/dsp/dsp.h"
#include "stmlib/utils/buffer_allocator.h"
#include "esp_heap_caps.h"
#include <algorithm>
#include <cmath>
#include <new>

using namespace CTAG::SP;

// Plaits Voice::Init expects a 16 KB scratch workspace via stmlib::BufferAllocator.
// All 24 engines share this buffer (allocator->Free() between Init calls).
static constexpr size_t kPlaitsAllocatorBytes = 16384;
static constexpr float kTBDaitsFixedOutputGain = 2900.f / 4095.f;
static constexpr int kTBDaitsDecorrelatorSize = 128;
static constexpr int kTBDaitsDecorrelatorMask = kTBDaitsDecorrelatorSize - 1;
static constexpr float kTBDaitsDecorrelatorLfoInc = 0.23f / 44100.f;

// Silence-gate tail. Same reasoning as RackTBDings: ~5 s at 1378 Hz block rate
// covers the longest LPG decay tail before we hard-mute. Plaits' drone-style
// engines (Noise / Speech / Chord) never naturally fall silent without LPG, so
// this is the only thing keeping an idle-but-enabled track from humming.
static constexpr int kSilenceTailBlocks = 7000;

void RackTBDaits::Init(const GrooveBoxRackInitData *initdata) {
    // Init runs once at GrooveBoxRack construction, before the audio task starts.
    // heap_caps_malloc here is safe.
    if (!shared_buffer) {
        shared_buffer = (char*)heap_caps_malloc(kPlaitsAllocatorBytes, MALLOC_CAP_SPIRAM);
        assert(shared_buffer != nullptr);
    }
    if (!voice) {
        void *mem = heap_caps_malloc(sizeof(plaits::Voice), MALLOC_CAP_SPIRAM);
        assert(mem != nullptr);
        voice = new (mem) plaits::Voice();
    }

    {
        stmlib::BufferAllocator allocator(shared_buffer, kPlaitsAllocatorBytes);
        voice->Init(&allocator);
    }

    // Sensible patch defaults (mid-range Plaits, DX7A engine).
    patch.engine = 2;       // Six-Op DX7 instance A
    patch.note = 60.f;
    patch.harmonics = 0.5f;
    patch.timbre = 0.5f;
    patch.morph = 0.5f;
    patch.frequency_modulation_amount = 0.f;
    patch.timbre_modulation_amount = 0.f;
    patch.morph_modulation_amount = 0.f;
    patch.decay = 0.6f;
    patch.lpg_colour = 0.5f;

    // Modulation source patching:
    //   *_patched=false for FM/Timbre/Morph → Plaits' ApplyModulations falls
    //   back to its internal decay envelope as the modulation source, scaled
    //   by the *_amount knobs (musically expected envelope-driven mod).
    //   trigger_patched=true → we drive trigger explicitly per note.
    //   level_patched is set per-block in Process based on the active engine
    //   (DX7 instances get level_patched=true so the LEVEL knob acts as
    //   per-patch velocity per the Plaits manual; all other engines get
    //   level_patched=false so the trigger strikes the LPG and Decay knob
    //   audibly shapes the note tail).
    modulations.engine = 0.f;
    modulations.note = 0.f;
    modulations.frequency = 0.f;
    modulations.harmonics = 0.f;
    modulations.timbre = 0.f;
    modulations.morph = 0.f;
    modulations.trigger = 0.f;
    modulations.level = 0.f;
    modulations.frequency_patched = false;
    modulations.timbre_patched = false;
    modulations.morph_patched = false;
    modulations.trigger_patched = true;
    modulations.level_patched = false;    // overwritten per-block for DX7 engines

    // Prime the default Six-Op/DX7 engine before the first audible note. Plaits
    // loads DX7 patch data lazily on first Render(), and the SixOp engine has
    // two staggered internal voices. Exercising two silent trigger edges here
    // keeps first user note presses from paying that cold-start cost.
    {
        plaits::Voice::Frame warm_frames[BUF_SZ];
        const int saved_engine = patch.engine;
        const float saved_trigger = modulations.trigger;
        const float saved_level = modulations.level;
        const bool saved_level_patched = modulations.level_patched;

        patch.engine = 2;
        modulations.level = 0.f;
        modulations.level_patched = true;
        for (int pulse = 0; pulse < 2; ++pulse) {
            for (int i = 0; i < 6; ++i) {
                modulations.trigger = 0.f;
                voice->Render(patch, modulations, warm_frames, BUF_SZ);
            }
            for (int i = 0; i < 6; ++i) {
                modulations.trigger = 1.f;
                voice->Render(patch, modulations, warm_frames, BUF_SZ);
            }
        }
        for (int i = 0; i < 6; ++i) {
            modulations.trigger = 0.f;
            voice->Render(patch, modulations, warm_frames, BUF_SZ);
        }

        patch.engine = saved_engine;
        modulations.trigger = saved_trigger;
        modulations.level = saved_level;
        modulations.level_patched = saved_level_patched;
    }

    // ---- CC registrations (8..19) ----
    // Sequential ctrls; idx == ctrl - 8 invariant holds across all 12 params.
    initdata->rack->registerParamAndCC(initdata, "model",  8,  [&](int v){ model_par = v; });
    initdata->rack->registerParamAndCC(initdata, "freq",   9,  [&](int v){ freq_par = v; });
    initdata->rack->registerParamAndCC(initdata, "harm",   10, [&](int v){ harm_par = v; });
    initdata->rack->registerParamAndCC(initdata, "timbre", 11, [&](int v){ timbre_par = v; });
    initdata->rack->registerParamAndCC(initdata, "morph",  12, [&](int v){ morph_par = v; });
    initdata->rack->registerParamAndCC(initdata, "decay",  13, [&](int v){ decay_par = v; });
    initdata->rack->registerParamAndCC(initdata, "color",  14, [&](int v){ color_par = v; });
    initdata->rack->registerParamAndCC(initdata, "mix",    15, [&](int v){ mix_par = v; });
    initdata->rack->registerParamAndCC(initdata, "fmod",   16, [&](int v){ fmod_par = v; });
    initdata->rack->registerParamAndCC(initdata, "tmod",   17, [&](int v){ tmod_par = v; });
    initdata->rack->registerParamAndCC(initdata, "mmod",   18, [&](int v){ mmod_par = v; });
    initdata->rack->registerParamAndCC(initdata, "width",  19, [&](int v){ width_par = v; });

    enabled = false;
}

void RackTBDaits::noteOn(uint8_t note, uint8_t vel) {
    midi_note = note;
    pending_velocity = vel ? vel : 100;
    note_held = true;
    // Force a 0→1 trigger edge over 2 blocks. Even if note_held was already
    // true (repeated same-note triggers from sequencer), the 1-block low
    // floor guarantees Plaits' internal trigger detector sees a fresh edge.
    trigger_pulse_state = 2;
    // Re-attack the AHR envelope to the new velocity unconditionally.
    // Without this snap-to-velocity, a forte note followed by a piano note
    // (held key, sequencer step, or repeated keyboard strike) would
    // continue at the higher amplitude — masking the user's velocity
    // intent. Critical for expressive keyboard play and per-step velocity
    // dynamics in the sequencer.
    env_value = pending_velocity / 127.f;
}

void RackTBDaits::noteOff(uint8_t note, uint8_t vel) {
    (void)note;
    (void)vel;
    note_held = false;
}

void RackTBDaits::Process(const GrooveBoxRackProcessData &data) {
    (void)data;
    if (!enabled) return;

    // Snapshot atomics once per block.
    const int i_model  = model_par;
    const int i_freq   = freq_par;
    const int i_harm   = harm_par;
    const int i_timbre = timbre_par;
    const int i_morph  = morph_par;
    const int i_decay  = decay_par;
    const int i_color  = color_par;
    const int i_mix    = mix_par;
    const int i_fmod   = fmod_par;
    const int i_tmod   = tmod_par;
    const int i_mmod   = mmod_par;
    const int i_width  = width_par;

    // ---- SILENCE GATE -----------------------------------------------------
    // Active iff: note held, pulse pending, or AHR envelope still ringing
    // above -60 dB. The env_value > 0.001 check keeps audio flowing during
    // the natural release tail (sequencer-fit and keyboard release feel)
    // AND keeps drone mode (Decay at max → release_factor=1) sustaining
    // indefinitely until the user releases the key + the env actually
    // decays. Without it, drone-style engines (Noise / Chord / Speech /
    // Modal at high decay) keep humming and the silence gate could prematurely
    // mute audible release tails.
    const bool any_activity = note_held
                           || (trigger_pulse_state != 0)
                           || (env_value > 0.001f);
    if (any_activity) {
        silence_tail_blocks = 0;
    } else {
        silence_tail_blocks++;
        if (silence_tail_blocks > kSilenceTailBlocks) {
            std::fill_n(aits_out_stereo, BUF_SZ * 2, 0.f);
            std::fill_n(decorrelator_buffer, kTBDaitsDecorrelatorSize, 0.f);
            decorrelator_idx = 0;
            decorrelator_lfo_phase = 0.f;
            return;
        }
    }
    // ----------------------------------------------------------------------

    // Engine selection — 24 engines mapped 1:1 to encoder positions 0..23.
    // The macro layer fans encoder pos via mul=5 → wire 0..115 → cv 0..3680
    // (not full 0..4095). The standard (cv*N)/4096 bucketing would lose the
    // top two engines because cv never reaches 4064. Recover the encoder
    // position directly: cv = encoder_pos * 32 (macro res scaling) * 5
    // (macro mul) = encoder_pos * 160. Divide by 160 to invert.
    //   ⚠ This divisor is paired with the macro JSON's mul=5. If you change
    //   one, change the other. See tbdait-params.json mapping ctrl 8.
    int active_engine;
    {
        int e = i_model / 160;
        if (e < 0) e = 0;
        if (e >= plaits::kMaxEngines) e = plaits::kMaxEngines - 1;
        patch.engine = e;
        active_engine = e;
    }
    if (active_engine != previous_engine) {
        previous_engine = active_engine;
        if (active_engine >= 2 && active_engine <= 4) {
            mute_after_sixop_switch_blocks = 1;
        }
    }

    // AHR-envelope-driven modulations.level (level_patched=true).
    // Plaits hardware default = "LEVEL CV unpatched" → ProcessPing → trigger
    // strikes LPG → Decay shapes tail. That model assumes an external CV
    // shapes the note when LEVEL IS patched, and treats the Level knob as
    // a static post-Render multiplier when not.
    //
    // Our environment is different from Plaits hardware:
    //   - No CV inputs — Level is a static knob.
    //   - Users include both step-sequencer and live-keyboard players.
    //   - Keyboard players expect held notes to sustain and release on
    //     key-up (ProcessPing alone ignores note length).
    //   - DX7 patches use accent (= compressed_level when level_patched=true)
    //     for per-patch velocity-to-timbre routing — a real expressive
    //     feature for keyboard play that's lost without ProcessLP.
    //
    // Solution: synthesize a wrapper-side AHR envelope (instant attack,
    // hold while note_held, exponential release per Decay knob) and feed
    // it into modulations.level. Then ProcessLP follows it → LPG audibly
    // shapes the tail per Decay, AND the engine accent sees envelope-
    // shaped velocity → DX7 patches respond expressively. Single code path
    // works for all 24 engines.
    (void)active_engine;
    modulations.level_patched = true;
    {
        // Target: peak (velocity-scaled) while held, zero on release.
        const float target = note_held ? (pending_velocity / 127.f) : 0.f;
        if (env_value < target) {
            // Instant attack — sequencer / keyboard expect immediate response.
            env_value = target;
        } else {
            // Exponential release. tau (seconds) maps from Decay knob via
            // the same Plaits curve as PT_PLAITS_DECAY: 5 ms..8 s.
            //   tau = 0.005 * 1600^(decay_norm)
            //   per-block decay factor = exp(-block_time / tau)
            //   block_time = BUF_SZ / sample_rate = 32 / 44100 ≈ 0.000726 s
            // Drone mode: at the very top of the Decay knob (top ~0.5 %),
            // release_factor is forced to 1.0 → infinite sustain. Lets the
            // user explicitly opt into drone / pad textures without limiting
            // the released-note tail elsewhere on the knob. The silence
            // gate's env_value > threshold check keeps audio flowing in
            // drone mode (handled in the gate block above-block).
            float release_factor;
            if (i_decay >= 4080) {
                release_factor = 1.f;
            } else {
                const float decay_norm = i_decay / 4095.f;
                const float tau_s = 0.005f * powf(1600.f, decay_norm);
                release_factor = expf(-0.000726f / tau_s);
            }
            env_value = target + (env_value - target) * release_factor;
        }
        if (env_value < 0.f) env_value = 0.f;
        if (env_value > 1.f) env_value = 1.f;
    }
    modulations.level = env_value;

    // Note pitch — bias ±12 semitones (1 octave) around the played MIDI note.
    // Mid-scale (cv ≈ 2048) = 0 detune. The Plaits manual documents an
    // 8-octave default range; ±12 is the sequencer-friendly middle ground —
    // wide enough for octave transposition / creative repitching, fine
    // enough that hi-res NRPM (16384 steps) gives ~0.0015 semi precision.
    {
        float bias_semis = (i_freq / 4095.f - 0.5f) * 24.f;
        patch.note = (float)midi_note + bias_semis;
        CONSTRAIN(patch.note, 0.f, 96.f);
    }

    // Macro tone params — direct 0..1 mapping.
    patch.harmonics = i_harm / 4095.f;
    patch.timbre    = i_timbre / 4095.f;
    patch.morph     = i_morph / 4095.f;
    patch.decay     = i_decay / 4095.f;
    patch.lpg_colour = i_color / 4095.f;
    CONSTRAIN(patch.harmonics, 0.f, 1.f);
    CONSTRAIN(patch.timbre,    0.f, 1.f);
    CONSTRAIN(patch.morph,     0.f, 1.f);
    CONSTRAIN(patch.decay,     0.f, 1.f);
    CONSTRAIN(patch.lpg_colour, 0.f, 1.f);

    // Modulation amounts — Plaits expects -1..+1 (signed). Map our 0..1 cv
    // to 0..1 positive depth, leaving negative depths inaccessible (they
    // sound like inverted versions of the positive direction; one knob
    // direction is enough for sequencer-driven use).
    patch.frequency_modulation_amount = i_fmod / 4095.f;
    patch.timbre_modulation_amount    = i_tmod / 4095.f;
    patch.morph_modulation_amount     = i_mmod / 4095.f;
    CONSTRAIN(patch.frequency_modulation_amount, -1.f, 1.f);
    CONSTRAIN(patch.timbre_modulation_amount,    -1.f, 1.f);
    CONSTRAIN(patch.morph_modulation_amount,     -1.f, 1.f);

    // Trigger pulse state machine — see noteOn for rationale.
    //   2 → emit trigger=0 this block, decrement to 1
    //   1 → emit trigger=1 this block, decrement to 0
    //   0 → steady state: trigger = note_held ? 1 : 0
    float trig_value;
    if (trigger_pulse_state == 2) {
        trig_value = 0.f;
        trigger_pulse_state = 1;
    } else if (trigger_pulse_state == 1) {
        trig_value = 1.f;
        trigger_pulse_state = 0;
    } else {
        trig_value = note_held ? 1.f : 0.f;
    }
    modulations.trigger = trig_value;

    const float mix_target = i_mix / 4095.f;
    const float width_target = i_width / 4095.f;
    mix_smooth += (mix_target - mix_smooth) * 0.15f;
    width_smooth += (width_target - width_smooth) * 0.15f;
    CONSTRAIN(mix_smooth, 0.f, 1.f);
    CONSTRAIN(width_smooth, 0.f, 1.f);

    // Fixed post-Render master gain. Velocity is NOT applied here — it's
    // already inside env_value (which feeds modulations.level → LPG).
    // Applying velocity twice would square the dynamic range.
    const bool sixop_engine = active_engine >= 2 && active_engine <= 4;
    const float master_gain = kTBDaitsFixedOutputGain * (sixop_engine ? env_value : 1.f);

    // Render one block.
    plaits::Voice::Frame frames[BUF_SZ];
    voice->Render(patch, modulations, frames, BUF_SZ);
    if (mute_after_sixop_switch_blocks > 0) {
        mute_after_sixop_switch_blocks--;
        std::fill_n(aits_out_stereo, BUF_SZ * 2, 0.f);
        std::fill_n(decorrelator_buffer, kTBDaitsDecorrelatorSize, 0.f);
        decorrelator_idx = 0;
        decorrelator_lfo_phase = 0.f;
        return;
    }

    // Detect engines whose OUT/AUX are identical (DX7/SixOp does this).
    // Width normally uses native OUT/AUX difference, but that disappears at
    // Mix extremes. Fade in a tiny symmetric side decorrelator there too, so
    // dual-output engines still react to Width when Mix is near OUT or AUX.
    float diff_sum = 0.f;
    float signal_sum = 0.f;
    for (int i = 0; i < BUF_SZ; i++) {
        const float main = frames[i].out / 32768.f;
        const float aux = frames[i].aux / 32768.f;
        diff_sum += fabsf(main - aux);
        signal_sum += fabsf(main) + fabsf(aux);
    }
    const bool monoish = diff_sum < (signal_sum * 0.001f);

    // Convert int16 frames to float stereo, apply fixed gain, soft-clip,
    // NaN/Inf guard. Frame::out / Frame::aux are int16 in [-32767, +32767].
    for (int i = 0; i < BUF_SZ; i++) {
        const float main = frames[i].out / 32768.f;
        const float aux = frames[i].aux / 32768.f;
        const float mono = main + (aux - main) * mix_smooth;
        const float native_side_amount = 2.f * std::min(mix_smooth, 1.f - mix_smooth);
        float side = (main - aux) * 0.5f * native_side_amount;
        const float decorrelator_amount = monoish ? 1.f : (1.f - native_side_amount);
        if (decorrelator_amount > 0.001f && width_smooth > 0.001f) {
            // Symmetric side-only micro-chorus. Two mirrored fractional taps
            // cancel static left/right bias: the added signal exists only as
            // mid/side width and mono still collapses cleanly.
            const float tri = decorrelator_lfo_phase < 0.5f
                            ? decorrelator_lfo_phase * 4.f - 1.f
                            : 3.f - decorrelator_lfo_phase * 4.f;
            const float d_a = 43.f + tri * 5.f;
            const float d_b = 67.f - tri * 5.f;

            auto read_delay = [&](float delay_samples) {
                float read_pos = static_cast<float>(decorrelator_idx) - delay_samples;
                while (read_pos < 0.f) read_pos += kTBDaitsDecorrelatorSize;
                const int i0 = static_cast<int>(read_pos) & kTBDaitsDecorrelatorMask;
                const int i1 = (i0 + 1) & kTBDaitsDecorrelatorMask;
                const float frac = read_pos - static_cast<float>(static_cast<int>(read_pos));
                return decorrelator_buffer[i0] + (decorrelator_buffer[i1] - decorrelator_buffer[i0]) * frac;
            };

            const float tap_a = read_delay(d_a);
            const float tap_b = read_delay(d_b);
            side += (tap_a - tap_b) * (0.10f * decorrelator_amount);
        }
        decorrelator_buffer[decorrelator_idx] = mono;
        decorrelator_idx = (decorrelator_idx + 1) & kTBDaitsDecorrelatorMask;
        decorrelator_lfo_phase += kTBDaitsDecorrelatorLfoInc;
        if (decorrelator_lfo_phase >= 1.f) decorrelator_lfo_phase -= 1.f;

        float l = (mono + side * width_smooth) * master_gain;
        float r = (mono - side * width_smooth) * master_gain;
        if (!std::isfinite(l)) l = 0.f;
        if (!std::isfinite(r)) r = 0.f;
        aits_out_stereo[i * 2 + 0] = stmlib::SoftClip(l);
        aits_out_stereo[i * 2 + 1] = stmlib::SoftClip(r);
    }
}

// ═══════════════════════════════════════════════════════════════
// TBD-16 WebUI — Machine-specific display hint overlay
// Vanilla JS · No dependencies
//
// This module is an *optional additive overlay* on top of
// display-hints.js.  It carries per-machine semantic hints for the
// 17 factory machines on the TBD-16's GrooveBoxRack, lifted from the
// pico-seq3 sequencer's PARAMTYPE enum (PT_LEVEL, PT_FILTER_TYPE,
// PT_FILTER_CUTOFF, PT_FREQ, PT_MACRO_SHAPE …):
//
//   • Per-machine frequency ranges that respect the actual DSP scaling
//     (kick = 20..500 Hz, not the generic 20..20 kHz).
//   • Named-enum filter types (LP / BP / HP / "Karlson" / "Pirkle Boost"
//     …) rendered as labels instead of raw 0..N integers.
//   • Macro-shape name lookup for Braids-derived voices.
//
// LAYERING — strictly additive:
//
//   display-hints.js exposes window.DH (resolveHint / formatDisplayValue
//   / buildConvExpr).  It checks for window.MH (this module) and lets
//   it have first dibs on the lookup; if MH is absent or returns null,
//   the existing generic suffix/name/keyword tables take over.
//
//   A third-party rack with unknown machine ids therefore never gains a
//   hint from this file — the generic table handles it.  Hardcoding is
//   intentional for now; the longer-term plan is to drive these hints
//   from a JSON sibling of mui-GrooveBoxRack.json (or even the firmware
//   itself), but the WebUI doesn't need that to render well today.
//
// Keys are `<machine_id>_<param_suffix>` — the rack's voice id (e.g.
// "db", "wtosc", "tbd", "aits") joined with the param's CC suffix.
// The lookup strips the `chN_` track prefix off the paramId, so the
// same entry applies to every track that can host the machine (e.g.
// `tbd_bright` covers both `ch12_tbd_bright` and `ch15_tbd_bright`).
//
// (c) 2026 Johannes Elias Lohbihler for dadamachines.
// Licensed under the GNU Lesser General Public License (LGPL 3.0).
// ═══════════════════════════════════════════════════════════════
'use strict';

(function() {

  // ─── Macro-shape name table (lifted from pico-seq3) ──────
  // For Braids-style macro-oscillator "shape" knobs.  Indices 0..47 map
  // 1:1 to the encoder positions on the OLED.  Source:
  // tbd-pico-seq3/lib/sequencerui/screens/commonrender.cpp:226.  The
  // pipe ("|") two-line OLED separator has been collapsed to a space
  // since the WebUI has one line of room per value readout.
  var MACRO_SHAPE_NAMES = [
    'CSAW', 'MORPH', 'SAW SQR', 'SINE TRI', 'BUZZ', 'SQUARE SUB', 'SAW SUB',
    'SQUARE SYNC', 'SAW SYNC', '3xSAW', '3xSQU', '3xTRI', '3xSINE', '3xRING MOD',
    'SAW SWARM', 'SAW COMB', 'TOY', 'FiltLP', 'FiltPK', 'FiltBP', 'FiltHP',
    'VOSIM', 'VOWEL', 'VOWEL FOF', 'HARM ONICS', 'FM', 'FEEDBK FM',
    'CHAOTIC FB FM', 'PLUCKED', 'BOWED', 'BLOWN', 'FLUTED', 'STRUCK BELL',
    'STRUCK DRUM', 'KICK', 'CYMBAL', 'SNARE', 'WAVET ABLES', 'WAVE MAP',
    'WAVE LINE', 'WAVE PARAPH.', 'FILTR. NOISE', 'TWINPKS NOISE',
    'CLOCKED NOISE', 'GRAN. CLOUD', 'PARTIC. NOISE', 'DIGITAL MOD',
    'QUEST. MARK',
  ];

  // Named enum tables — referenced from a hint via `enumTable: 'macroShape'`.
  // Add more tables here as new machines need them.
  var ENUM_TABLES = {
    macroShape: MACRO_SHAPE_NAMES,
  };

  // ─── Per-machine GrooveBoxRack hint table ────────────────
  var MACHINE_HINTS = {
    // ── Plaits analog bass drum (db) ──────────────────────
    'db_f0':     { unit: 'Hz', scale: 'log', physMin: 20,  physMax: 500,  label: 'Frequency' },
    'db_accent': { unit: '%',  scale: 'lin', physMin: 0,   physMax: 100,  format: 'percent', label: 'Accent' },
    'db_tone':   { unit: '%',  scale: 'lin', physMin: 0,   physMax: 100,  format: 'percent', label: 'Tone' },
    'db_decay':  { unit: 'ms', scale: 'log', physMin: 5,   physMax: 2000, label: 'Decay' },
    'db_dirty':  { unit: '%',  scale: 'lin', physMin: 0,   physMax: 100,  format: 'percent', label: 'Dirtiness' },
    'db_fm_env': { unit: '%',  scale: 'lin', physMin: 0,   physMax: 100,  format: 'percent', label: 'FM Envelope' },
    'db_fm_dcy': { unit: 'ms', scale: 'log', physMin: 1,   physMax: 1000, label: 'FM Decay' },

    // ── Plaits synthetic analog bass drum (ab) ────────────
    'ab_f0':     { unit: 'Hz', scale: 'log', physMin: 20,  physMax: 500,  label: 'Frequency' },
    'ab_accent': { unit: '%',  scale: 'lin', physMin: 0,   physMax: 100,  format: 'percent', label: 'Accent' },
    'ab_tone':   { unit: '%',  scale: 'lin', physMin: 0,   physMax: 100,  format: 'percent', label: 'Tone' },
    'ab_decay':  { unit: 'ms', scale: 'log', physMin: 5,   physMax: 2000, label: 'Decay' },
    'ab_a_fm':   { unit: '%',  scale: 'lin', physMin: 0,   physMax: 100,  format: 'percent', label: 'Attack FM' },
    'ab_s_fm':   { unit: '%',  scale: 'lin', physMin: 0,   physMax: 100,  format: 'percent', label: 'Self FM' },

    // ── FM bass drum (fmb) — Larsson 4.1 model ────────────
    'fmb_f_b':   { unit: 'Hz', scale: 'log', physMin: 30,  physMax: 500,  label: 'Base Frequency' },
    'fmb_d_b':   { unit: 'ms', scale: 'log', physMin: 5,   physMax: 2000, label: 'Carrier Decay' },
    'fmb_f_m':   { unit: 'Hz', scale: 'log', physMin: 20,  physMax: 2000, label: 'Modulator Frequency' },
    'fmb_d_m':   { unit: 'ms', scale: 'log', physMin: 1,   physMax: 1000, label: 'Modulator Decay' },
    'fmb_b_m':   { unit: '%',  scale: 'lin', physMin: 0,   physMax: 100,  format: 'percent', label: 'Modulator Feedback' },
    'fmb_d_f':   { unit: 'ms', scale: 'log', physMin: 1,   physMax: 1000, label: 'Freq Env Decay' },
    'fmb_use_ratio_mode': { unit: '', enum: ['Off', 'On'], label: 'Use Ratio Mode' },
    'fmb_mod_env_sync':   { unit: '', enum: ['Off', 'On'], label: 'Mod Env Sync' },

    // ── Snares / hats / rim / clap ────────────────────────
    'ds_f0':     { unit: 'Hz', scale: 'log', physMin: 60,  physMax: 1200, label: 'Frequency' },
    'ds_decay':  { unit: 'ms', scale: 'log', physMin: 5,   physMax: 2000, label: 'Decay' },
    'ds_fm_amt': { unit: '%',  scale: 'lin', physMin: 0,   physMax: 100,  format: 'percent', label: 'FM Amount' },
    'ds_spy':    { unit: '%',  scale: 'lin', physMin: 0,   physMax: 100,  format: 'percent', label: 'Snappy' },
    'as_f0':     { unit: 'Hz', scale: 'log', physMin: 60,  physMax: 1200, label: 'Frequency' },
    'as_tone':   { unit: '%',  scale: 'lin', physMin: 0,   physMax: 100,  format: 'percent', label: 'Tone' },
    'as_decay':  { unit: 'ms', scale: 'log', physMin: 5,   physMax: 2000, label: 'Decay' },
    'as_a_spy':  { unit: '%',  scale: 'lin', physMin: 0,   physMax: 100,  format: 'percent', label: 'Snappy' },
    'hh1_f0':    { unit: 'Hz', scale: 'log', physMin: 200, physMax: 8000, label: 'Frequency' },
    'hh1_decay': { unit: 'ms', scale: 'log', physMin: 5,   physMax: 2000, label: 'Decay' },
    'hh2_f0':    { unit: 'Hz', scale: 'log', physMin: 200, physMax: 8000, label: 'Frequency' },
    'hh2_decay': { unit: 'ms', scale: 'log', physMin: 5,   physMax: 2000, label: 'Decay' },
    'rs_f0':     { unit: 'Hz', scale: 'log', physMin: 200, physMax: 4000, label: 'Frequency' },
    'rs_decay':  { unit: 'ms', scale: 'log', physMin: 5,   physMax: 1000, label: 'Decay' },
    'cl_f0':     { unit: 'Hz', scale: 'log', physMin: 200, physMax: 4000, label: 'Frequency' },
    'cl_decay':  { unit: 'ms', scale: 'log', physMin: 5,   physMax: 1000, label: 'Decay' },

    // ── TBD03 (TB-303 emulation) ──────────────────────────
    'tbd03_cutoff':     { unit: 'Hz', scale: 'log', physMin: 20, physMax: 22000, label: 'Cutoff' },
    'tbd03_resonance':  { unit: '%',  scale: 'lin', physMin: 0,  physMax: 100, format: 'percent', label: 'Resonance' },
    'tbd03_decay_vca':  { unit: 'ms', scale: 'log', physMin: 5,  physMax: 2000, label: 'Decay (VCA)' },
    'tbd03_decay_vcf':  { unit: 'ms', scale: 'log', physMin: 5,  physMax: 2000, label: 'Decay (VCF)' },
    'tbd03_envelope':   { unit: '%',  scale: 'lin', physMin: 0,  physMax: 100, format: 'percent', label: 'Env Mod' },
    'tbd03_saturation': { unit: '%',  scale: 'lin', physMin: 0,  physMax: 100, format: 'percent', label: 'Saturation' },
    'tbd03_drive':      { unit: '%',  scale: 'lin', physMin: 0,  physMax: 100, format: 'percent', label: 'Drive' },
    'tbd03_filter_type': { unit: '', enum: ['Pirkle Boost', 'Karlson', 'Blaukraut', 'Pirkle ZDF', 'Zavalishin'], label: 'Filter' },
    'tbd03_accent_level': { unit: '%', scale: 'lin', physMin: 0, physMax: 100, format: 'percent', label: 'Accent' },
    'tbd03_slide_level':  { unit: '%', scale: 'lin', physMin: 0, physMax: 100, format: 'percent', label: 'Slide' },

    // ── Macro Oscillator (mo) — Braids ────────────────────
    'mo_shape':         { enumTable: 'macroShape', physMin: 0, physMax: 47, label: 'Shape' },
    'mo_decimation':    { unit: '%',  scale: 'lin', physMin: 0,  physMax: 100, format: 'percent', label: 'Decimation' },
    'mo_bit_reduction': { unit: '%',  scale: 'lin', physMin: 0,  physMax: 100, format: 'percent', label: 'Bit Reduction' },
    'mo_waveshaping':   { unit: '%',  scale: 'lin', physMin: 0,  physMax: 100, format: 'percent', label: 'Wave Shaping' },
    'mo_pitch':         { unit: 'st', scale: 'lin', physMin: -24, physMax: 24, format: 'semitones', label: 'Pitch' },
    'mo_attack':        { unit: 'ms', scale: 'log', physMin: 0.5, physMax: 5000, label: 'Attack' },
    'mo_decay':         { unit: 'ms', scale: 'log', physMin: 1,   physMax: 5000, label: 'Decay' },

    // ── Wavetable Oscillator (wtosc) ──────────────────────
    'wtosc_wavebank':   { unit: '/32', scale: 'lin', physMin: 0, physMax: 31, label: 'Wave Bank' },
    'wtosc_wave':       { unit: '/64', scale: 'lin', physMin: 0, physMax: 63, label: 'Wave' },
    'wtosc_tune':       { unit: 'st', scale: 'lin', physMin: -12, physMax: 12, format: 'semitones', label: 'Tune' },
    'wtosc_fmode':      { unit: '', enum: ['Off', 'LP', 'BP', 'HP'], label: 'Filter Mode' },
    'wtosc_fcut':       { unit: 'Hz', scale: 'log', physMin: 20, physMax: 22000, label: 'Filter Cutoff' },
    'wtosc_freso':      { unit: '%',  scale: 'lin', physMin: 0, physMax: 100, format: 'percent', label: 'Filter Resonance' },
    'wtosc_attack':     { unit: 'ms', scale: 'log', physMin: 0.5, physMax: 5000, label: 'Attack' },
    'wtosc_decay':      { unit: 'ms', scale: 'log', physMin: 1, physMax: 5000, label: 'Decay' },
    'wtosc_sustain':    { unit: '%',  scale: 'lin', physMin: 0, physMax: 100, format: 'percent', label: 'Sustain' },
    'wtosc_release':    { unit: 'ms', scale: 'log', physMin: 1, physMax: 10000, label: 'Release' },
    'wtosc_gain':       { unit: 'dB', scale: 'lin', physMin: -60, physMax: 6, format: 'db', label: 'Gain' },
    'wtosc_lfospeed':   { unit: 'Hz', scale: 'log', physMin: 0.01, physMax: 20, label: 'LFO Speed' },

    // ── TBDings (tbd) — Mutable Rings ─────────────────────
    'tbd_model':    { unit: '', enum: ['Modal', 'Sympathetic', 'String'], label: 'Model' },
    'tbd_freq':     { unit: 'st', scale: 'lin', physMin: -24, physMax: 24, format: 'semitones', label: 'Pitch' },
    'tbd_struc':    { unit: '%',  scale: 'lin', physMin: 0, physMax: 100, format: 'percent', label: 'Structure' },
    'tbd_pos':      { unit: '%',  scale: 'lin', physMin: 0, physMax: 100, format: 'percent', label: 'Position' },
    'tbd_bright':   { unit: '%',  scale: 'lin', physMin: 0, physMax: 100, format: 'percent', label: 'Brightness' },
    'tbd_damp':     { unit: '%',  scale: 'lin', physMin: 0, physMax: 100, format: 'percent', label: 'Damping' },
    'tbd_poly':     { unit: '', enum: ['1 voice', '2 voices', '4 voices'], label: 'Polyphony' },
    'tbd_air':      { unit: '%',  scale: 'lin', physMin: 0, physMax: 100, format: 'percent', label: 'Air' },
    'tbd_vela':     { unit: '%',  scale: 'lin', physMin: 0, physMax: 100, format: 'percent', label: 'Vel Amount' },
    'tbd_inversion':{ unit: '', enum: ['Root', '1st', '2nd', '3rd'], label: 'Inversion' },

    // ── TBDaits (aits) — Plaits 24-engine macro voice ─────
    // Encoder position 0..23 maps 1:1 to Plaits engines (model_par / 160).
    'aits_model':  { unit: '', enum: [
        'Virtual Analog', 'Waveshaping', 'Six-Op DX7-A', 'Six-Op DX7-B', 'Six-Op DX7-C',
        'Wave Terrain', 'String Machine', 'Chiptune', 'FM', 'Granular Formant',
        'Harmonic', 'Wavetable', 'Chord', 'Speech', 'Swarm', 'Noise',
        'Particle', 'String', 'Modal', 'Bass Drum', 'Snare', 'Hi-Hat',
        'Reserved-22', 'Reserved-23',
      ], physMax: 23, label: 'Engine' },
    'aits_freq':   { unit: 'st', scale: 'lin', physMin: -12, physMax: 12, format: 'semitones', label: 'Pitch' },
    'aits_harm':   { unit: '%',  scale: 'lin', physMin: 0, physMax: 100, format: 'percent', label: 'Harmonics' },
    'aits_timbre': { unit: '%',  scale: 'lin', physMin: 0, physMax: 100, format: 'percent', label: 'Timbre' },
    'aits_morph':  { unit: '%',  scale: 'lin', physMin: 0, physMax: 100, format: 'percent', label: 'Morph' },
    'aits_decay':  { unit: 'ms', scale: 'log', physMin: 5, physMax: 8000, label: 'Decay' },
    'aits_color':  { unit: '%',  scale: 'lin', physMin: 0, physMax: 100, format: 'percent', label: 'LPG Colour' },
    'aits_level':  { unit: 'dB', scale: 'lin', physMin: -60, physMax: 6, format: 'db', label: 'Level' },
    'aits_fmod':   { unit: '%',  scale: 'lin', physMin: 0, physMax: 100, format: 'percent', label: 'FM Mod' },
    'aits_tmod':   { unit: '%',  scale: 'lin', physMin: 0, physMax: 100, format: 'percent', label: 'Timbre Mod' },
    'aits_mmod':   { unit: '%',  scale: 'lin', physMin: 0, physMax: 100, format: 'percent', label: 'Morph Mod' },

    // ── PolyPad (pp) ──────────────────────────────────────
    'pp_filter_type':{ unit: '', enum: ['LP', 'BP', 'HP'], label: 'Filter' },
    'pp_cutoff':     { unit: 'Hz', scale: 'log', physMin: 20, physMax: 22000, label: 'Cutoff' },
    'pp_resonance':  { unit: '%',  scale: 'lin', physMin: 0, physMax: 100, format: 'percent', label: 'Resonance' },
    'pp_pitch':      { unit: 'st', scale: 'lin', physMin: -24, physMax: 24, format: 'semitones', label: 'Pitch' },
    'pp_detune':     { unit: 'ct', scale: 'lin', physMin: 0, physMax: 100, label: 'Detune' },
    'pp_attack':     { unit: 'ms', scale: 'log', physMin: 0.5, physMax: 5000, label: 'Attack' },
    'pp_decay':      { unit: 'ms', scale: 'log', physMin: 1, physMax: 5000, label: 'Decay' },
    'pp_sustain':    { unit: '%',  scale: 'lin', physMin: 0, physMax: 100, format: 'percent', label: 'Sustain' },
    'pp_release':    { unit: 'ms', scale: 'log', physMin: 1, physMax: 10000, label: 'Release' },
    'pp_lfo1_freq':  { unit: 'Hz', scale: 'log', physMin: 0.01, physMax: 20, label: 'LFO 1 Freq' },
    'pp_lfo2_freq':  { unit: 'Hz', scale: 'log', physMin: 0.01, physMax: 20, label: 'LFO 2 Freq' },
    'pp_inversion':  { unit: '', enum: ['Root', '1st', '2nd', '3rd'], label: 'Inversion' },

    // ── Rompler / sampler (smp prefix in mui) ─────────────
    'smp_bank':   { unit: '/16', scale: 'lin', physMin: 0, physMax: 15, label: 'Bank' },
    'smp_slice':  { unit: '/16', scale: 'lin', physMin: 0, physMax: 15, label: 'Slice' },
    'smp_pitch':  { unit: 'st', scale: 'lin', physMin: -24, physMax: 24, format: 'semitones', label: 'Pitch' },
    'smp_fc':     { unit: 'Hz', scale: 'log', physMin: 20, physMax: 22000, label: 'Filter Cutoff' },
    'smp_fq':     { unit: '%',  scale: 'lin', physMin: 0, physMax: 100, format: 'percent', label: 'Filter Q' },
    'smp_ft':     { unit: '', enum: ['Off', 'LP', 'BP', 'HP'], label: 'Filter Type' },
    'smp_atk':    { unit: 'ms', scale: 'log', physMin: 0.5, physMax: 5000, label: 'Attack' },
    'smp_dcy':    { unit: 'ms', scale: 'log', physMin: 1, physMax: 5000, label: 'Decay' },
  };

  // ─── Public API ──────────────────────────────────────────
  // display-hints.js calls MH.lookup() first when resolving a hint.

  /**
   * Look up a per-machine hint for a paramId.
   * Strips the `chN_` track prefix and probes MACHINE_HINTS by suffix.
   * @param {string} paramId   e.g. 'ch12_wtosc_fmode'
   * @param {string} paramName display name from the schema (overrides table label)
   * @returns {object|null}    hint or null if not in the table
   */
  function lookup(paramId, paramName) {
    if (!paramId) return null;
    var m = paramId.match(/^ch\d+_(.+)$/);
    if (!m) return null;
    var key = m[1];
    // Tolerate machine instance suffixes like `fmb1` — first probe the raw
    // key, then collapse trailing digits in the first underscore segment.
    var hit = MACHINE_HINTS[key];
    if (!hit) {
      var parts = key.split('_');
      if (parts.length >= 2) {
        var stripped = parts[0].replace(/\d+$/, '') + '_' + parts.slice(1).join('_');
        if (stripped !== key) hit = MACHINE_HINTS[stripped];
      }
    }
    if (!hit) return null;
    var out = Object.assign({}, hit);
    if (paramName) out.label = paramName;
    return out;
  }

  /**
   * Resolve a `hint.enumTable` name to its label array.
   * @param {string} name table name (e.g. 'macroShape')
   * @returns {string[]|null}
   */
  function enumTable(name) {
    return ENUM_TABLES[name] || null;
  }

  window.MH = {
    lookup: lookup,
    enumTable: enumTable,
    MACHINE_HINTS: MACHINE_HINTS,
    ENUM_TABLES: ENUM_TABLES,
  };

})();

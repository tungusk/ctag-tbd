****************
TBDaits (Stereo)
****************

Description
~~~~~~~~~~~

CTAG TBD port of Mutable Instruments **Plaits** — a macro oscillator that hosts
24 synthesis engines (virtual analog, waveshaping, FM, granular formant, wavetable,
chord, speech, modal, string, Karplus-Strong, snare, kick, hi-hat, …) under one
common interface (``patch.engine``, ``patch.harmonics``, ``patch.timbre``,
``patch.morph``, ``patch.decay``, ``patch.lpg_colour``).

  https://mutable-instruments.net/modules/plaits/

The TBD plugin variant wraps Plaits' raw ``Voice::Render`` in an AHR (attack–hold–
release) envelope so keyboard-style playing and per-step velocity work naturally
without needing an external CV. The same wrapper is what the GrooveBoxRack's
``tbdait`` machine uses on the TBD-16; see :doc:`Machines <machines>` for the
rack-internal id, the track it lives on (CH12 Lead2), and the source.

Engines (encoder position 0–23)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``0`` Virtual analog · ``1`` Waveshaping · ``2`` Six-Op DX7-A · ``3`` Six-Op DX7-B ·
``4`` Six-Op DX7-C · ``5`` Wave terrain · ``6`` String machine · ``7`` Chiptune ·
``8`` FM · ``9`` Granular formant · ``10`` Harmonic · ``11`` Wavetable ·
``12`` Chord · ``13`` Speech · ``14`` Swarm · ``15`` Noise ·
``16`` Particle · ``17`` String · ``18`` Modal · ``19`` Bass drum ·
``20`` Snare drum · ``21`` Hi-hat · ``22`` (Plaits 1.2 reserved) ·
``23`` (Plaits 1.2 reserved).

Parameters
~~~~~~~~~~

================  ===========================================================
Param             What it does
================  ===========================================================
``model``         Engine index 0–23. CC scale uses macro ``mul=5``.
``freq``          Pitch bias around the MIDI note, ±12 semitones.
``harm``          Engine-specific "harmonics" axis (0..1).
``timbre``        Engine-specific "timbre" axis (0..1).
``morph``         Engine-specific "morph" axis (0..1).
``decay``         AHR release / LPG decay. Top ~0.5 % of travel = drone.
``color``         LPG colour (filter character) — Mix / Bright / Dark zones.
``level``         Output gain (post-Render). Velocity is applied
                  separately via ``modulations.level``.
``fmod``          Envelope → frequency-mod depth (0..1, positive only).
``tmod``          Envelope → timbre-mod depth (0..1, positive only).
``mmod``          Envelope → morph-mod depth (0..1, positive only).
================  ===========================================================

Use it standalone via the WebUI plugin list, or hosted on the GrooveBoxRack —
the source is the same (``components/ctagSoundProcessor/ctagSoundProcessorTBDaits.*``
for the standalone, ``rack/RackTBDaits.*`` for the rack-machine variant).

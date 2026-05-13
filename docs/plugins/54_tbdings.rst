****************
TBDings (Stereo)
****************

Description
~~~~~~~~~~~

CTAG TBD port of Mutable Instruments **Rings** — a polyphonic resonator that
turns any excitation (pluck, noise burst, audio in) into modal / sympathetic /
inharmonic-string tones. Three resonator models, each with frequency, structure,
brightness, damping and a "position" pickup axis.

  https://mutable-instruments.net/modules/rings/

The TBD plugin variant adds:

* **Polyphony hold** — Rings' internal poly voicer is exposed (1 / 2 / 4 voices),
  with optional voice hold so chords ring out beyond the trigger.
* **Air blend** — continuous low-level noise mixed into the exciter input, for
  bowed / breathy textures that don't need an external excitation signal.
* **Chord / inversion / detune** — for polyphonic mode, a chord-shape table
  with detune control (the same shape the rack's PolyPad uses).
* **Velocity → exciter amplitude** mapping so per-step / per-key velocity is
  expressive without an external CV.

The same engine is what the GrooveBoxRack's ``tbd`` machine uses on CH12 (Lead2)
and CH15 (Chordo). See :doc:`Machines <machines>` for the rack-internal id and
source pointers.

Parameters (subset)
~~~~~~~~~~~~~~~~~~~

================  ===========================================================
Param             What it does
================  ===========================================================
``model``         Resonator model: 0 = Modal, 1 = Sympathetic strings,
                  2 = Inharmonic string.
``freq``          Note frequency / bias.
``struc``         Structure — odd/even partial ratio (model-dependent).
``pos``           Excitation pickup position along the resonator.
``bright``        High-frequency content of the partials.
``damp``          Resonator damping (release time).
``poly``          Polyphony: 1 / 2 / 4 voices.
``chord``         Chord index for polyphonic mode (PolyPad chord table).
``inversion``     Chord inversion 0..3.
``detune``        Per-voice detune amount (cents) — adds chorus / depth.
``air``           Air noise blend into the exciter input.
``vela``          Velocity → exciter amplitude depth.
``envsh``         Exciter envelope shape (impulse vs. sustained).
``pluck``         Manual pluck trigger (rising edge).
================  ===========================================================

Use it standalone via the WebUI plugin list, or hosted on the GrooveBoxRack —
``components/ctagSoundProcessor/ctagSoundProcessorTBDings.*`` for the standalone,
``rack/RackTBDings.*`` for the rack-machine variant.

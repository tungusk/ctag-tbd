*********************************
Writing a GrooveBoxRack Machine
*********************************

.. note::

   This is for **rack plugins** — voices that live *inside* the TBD-16's ``GrooveBoxRack``
   instrument (the macro/groovebox engine). If you want to write a normal, standalone
   ctag-tbd plugin (a Eurorack-style synth/effect/oscillator), see
   :doc:`Creating a Plugin <step-by-step>` instead. The two are different things.

   The macro/preset/rack layer is © Per-Olov Jernberg (possan) & Johannes Elias Lohbihler,
   building on the CTAG TBD ``DrumRack`` / engine by Robert Manzke (CTAG Kiel). It is
   GPL-3.0; a commercial licence is available — see ``LICENSE`` and ``CONTRIBUTING.md`` in
   the repo root.


What a "rack machine" is
========================

The **TBD-16** is a MIDI-driven groovebox (not a Eurorack module). Its ``GrooveBoxRack``
sound processor hosts up to **16 tracks**; each track can run one **machine** at a time —
a small DSP voice (a drum, a synth, a sampler, …). A machine:

- renders **one block** (``BUF_SZ`` = 32 samples) of **mono** audio per ``Process()`` call
  into its own public output buffer;
- is **triggered by MIDI** — drum machines by a fixed note on a fixed channel, synth
  machines by pitched notes on a per-track channel;
- exposes parameters that the rack registers as both **preset/WebUI parameters** and
  **MIDI CCs**;
- has its level / pan / FX-sends handled for it by the track's ``RackChannelMixer`` — you
  just produce a clean mono signal.

GrooveBoxRack mixes all the active tracks' outputs, runs the bus FX (delay, reverb, master
compressor) and writes the stereo result. The hardware's RP2350 step-sequencer (and/or USB
MIDI) feeds it MIDI; in the :doc:`desktop simulator <simulator>` you play it from the
``/ctrl`` page's *GrooveBoxRack (MIDI)* tab.


The track / machine map: ``synthdefinitions.json``
==================================================

``sdcard_image/data/synthdefinitions.json`` is the canonical description of the rack's
tracks. For each track it gives the ``index``, ``type`` (``drum`` / ``synth`` / ``fx``),
display ``name``, ``midichannel`` (**0-based**), ``drumnote`` (drum tracks only), ``basecc``
(the CC offset for that track's params), and the ordered ``machines`` list. There is also a
``machines`` array describing each machine's CC ``parameters`` (``id``, ``ctrl``, ``def``).

The track ↔ MIDI mapping is **not** a General-MIDI drum layout — it's:

.. list-table::
   :header-rows: 1
   :widths: 14 30 18 38

   * - WebUI strip
     - track (synthdefinitions)
     - MIDI ch · note
     - machines with a param panel
   * - CH01 / CH02 / CH03
     - 0 Kick / 1 Kick2 / 2 Snare
     - **ch 10** · note 36 / 37 / 38
     - ``db,ab,ro`` / ``fmb,ro`` / ``ds,as,ro``
   * - CH04 / CH05 / CH06
     - 3 Hat / 4 Rimshot / 5 Clap
     - **ch 11** · note 36 / 37 / 38
     - ``hh1,hh2,ro`` / ``rs,ro`` / ``cl,ro``
   * - CH07 / CH08
     - 6 Rompler / 7 Rompler
     - **ch 12** · note 36 / 37
     - ``ro``
   * - CH09…CH15
     - 8…14 Bass / Bass2 / Lead / Lead2 / Rompler / Rompler / Chordo
     - **ch 1…7** · pitched notes
     - ``td3`` / ``td3`` / ``mo`` / ``wtosc,mo,ro`` / ``ro`` / ``ro`` / ``pp,ro``
   * - CH16
     - 15 Input
     - **ch 8** · no notes
     - ``inp`` (audio passthrough)
   * - bus FX
     - 16/17/18 FX1 / FX2 / Master
     - **ch 12** · basecc 0 / 20 / 40
     - ``fxdelay`` / ``fxreverb`` / ``fxmaster``

So: drum tracks share MIDI channels 10/11/12 and the **note** picks which of the (up to 3)
tracks on that channel fires; synth tracks get one MIDI channel each (1–7). ``nodrum`` /
``nosynth`` / ``extdrum`` / ``extsynth`` are entries with no DSP/param panel (empty slot,
or "forward to an external MIDI device"). ``ro`` is the sampler (the *Rompler* voice — it
reads from the sample-rom, so it needs ``--srom`` in the simulator).


Anatomy of a machine class
==========================

Each machine is a plain C++ class in ``components/ctagSoundProcessor/rack/RackXxx.{hpp,cpp}``.
The shared structs live in ``rack/RackSynth.hpp``; FM drum building blocks (operators,
envelopes) in ``rack/FmDrumPrimitives.hpp``.

.. code-block:: cpp

   // rack/RackMyVoice.hpp
   #pragma once
   #include "RackSynth.hpp"
   using namespace CTAG::SP;

   class RackMyVoice {
   public:
       void Init(const GrooveBoxRackInitData *initdata);     // register params, init DSP
       void Process(const GrooveBoxRackProcessData &data); // render BUF_SZ mono samples
       void noteOn(uint8_t note, uint8_t velocity);        // synth: a note arrived  (drums: use trigger())
       void noteOff(uint8_t note, uint8_t velocity);
       bool  enabled;                                      // set by GrooveBoxRack::setTrackMachine()
       float out[BUF_SZ];                                  // your output buffer (name it `out`)
   private:
       atomic<int16_t> p_cutoff, p_decay /* , … one per registered param */;
       // … your DSP state …
   };

``Init(const GrooveBoxRackInitData *initdata)`` — register your parameters and initialise DSP:

.. code-block:: cpp

   void RackMyVoice::Init(const GrooveBoxRackInitData *initdata) {
       // CC numbers come from this machine's `parameters[].ctrl` in synthdefinitions.json
       // (drum/synth machine params conventionally start at cc 8; the mixer strip uses 1–7).
       initdata->rack->registerParamAndCC(initdata, "cutoff", 8, [&](const int v){ p_cutoff = v; });
       initdata->rack->registerParamAndCC(initdata, "decay",  9, [&](const int v){ p_decay  = v; });
       // … one registerParamAndCC() per parameter …
       this->enabled = false;
       // … init your DSP (clear buffers, set up filters, etc.) …
   }

``registerParamAndCC(initdata, "<suffix>", <cc>, <setter>)`` registers the parameter under
the id ``"<prefix><suffix>"`` (the prefix is set by GrooveBoxRack per track/machine, e.g.
``ch11_mo_``) in **both** maps:

- ``pMapPar`` — so ``LoadPreset()`` and the WebUI's "set parameter" path reach the setter;
- ``pMapParCC`` at ``cc_base + <cc>`` on the track's MIDI channel — so a MIDI CC reaches it.

``Process(const GrooveBoxRackProcessData &data)`` — render one block:

.. code-block:: cpp

   void RackMyVoice::Process(const GrooveBoxRackProcessData &data) {
       bool _trig = false;
       if (midi_trig) { _trig = true; midi_trig = false; }   // drum-style trigger (set by trigger())
       if (!this->enabled) return;                            // not the active machine — do nothing

       std::fill_n(out, BUF_SZ, 0.f);
       // scale raw param values (0..4096, or -4095..4095 for bipolar) to useful ranges:
       MK_FLT_PAR_ABS_NOCV(fCutoff, p_cutoff, 4095.f, 1.f)    // → 0..1
       MK_FLT_PAR_ABS_MIN_MAX_NOCV(fDecay, p_decay, 4095.f, 5.f, 2000.f)  // → 5..2000 ms
       // … render BUF_SZ samples into out[] …
       for (int i = 0; i < BUF_SZ; i++) out[i] = /* your DSP */;
   }

``data`` (a ``GrooveBoxRackProcessData``) gives you ``tempo`` (BPM × 100), ``quantum``,
``msPerBeat``, ``sampleRom`` (a ``ctagSampleRom*`` — for sampler voices), ``firstNonWtSlice``
and ``inputbuffer`` (the stereo audio input, used by the input track / FX). The host audio
buffer / CV / triggers (``ProcessData.buf/cv/trig``) are **not** exposed to machines — that's
intentional, GrooveBoxRack is MIDI-driven.

Triggering:

- **Drum machines** implement ``void trigger()`` — it just sets a ``midi_trig`` flag that the
  next ``Process()`` consumes (see ``RackDBD``). GrooveBoxRack's ``handleMidiNoteOn()`` calls
  ``trigger()`` when the track's drum note arrives.
- **Synth machines** implement ``void noteOn(uint8_t note, uint8_t velocity)`` /
  ``void noteOff(uint8_t note, uint8_t velocity)`` — ``handleMidiNoteOn()`` / ``handleMidiNoteOff()``
  call these for the track's MIDI channel; ``note`` is the pitch (semitones).

The track's ``RackChannelMixer`` applies level/pan/FX-sends and a ``volumeMultiplier`` trim
on top of your ``out[]`` — so produce a clean, roughly unity-level mono signal and don't
worry about panning or mixing.


Scaffold with ``rackgen.js`` (recommended)
==========================================

The fast path is to use ``generators/rackgen.js`` — the analog of ``generator.js`` (the legacy
sound-processor scaffolder) for rack machines. It takes a small descriptor JSON, generates the
class boilerplate, patches the three GrooveBoxRack data files, and prints the lines you still
need to paste into ``ctagSoundProcessorGrooveBoxRack.{hpp,cpp}`` to wire the voice in.

1. Copy ``generators/rack-template.json`` (e.g. to ``generators/rack-mybd.json``), edit the
   fields — ``id`` / ``className`` / ``name`` / ``type`` (``drum`` or ``synth``) / ``track``
   (0-based: 0..7 = drum tracks CH01..CH08; 8..14 = synth tracks CH09..CH15) and the ``params``
   list (each with a MIDI ``ctrl`` number and a 0..127 ``def``).
2. **Dry-run** to preview everything (writes ``<className>.{hpp,cpp}`` to ``cwd``, prints the
   JSON patches and the C++ integration snippets):

   .. code-block:: bash

      cd generators
      node rackgen.js rack-mybd.json

3. When the printed snippets look right, run **``-i``** — that writes the class into
   ``components/ctagSoundProcessor/rack/`` and patches ``synthdefinitions.json``,
   ``mui-GrooveBoxRack.json``, ``mp-GrooveBoxRack.json`` in place (leaving ``.bak`` files):

   .. code-block:: bash

      node rackgen.js rack-mybd.json -i

4. Paste the ~6 printed wiring lines into ``ctagSoundProcessorGrooveBoxRack.{hpp,cpp}``
   (member, ``Init``, ``Process``+``mixRenderOutputMono``, ``setTrackMachine``,
   ``handleMidiNoteOn``/``Off``). The tool prints them with the right ``ch<N>``/``ch<N>_<id>``
   names already filled in.
5. Fill in the DSP in your new ``RackMyVoice::Process()`` (the template leaves a TODO + a few
   ``MK_FLT_PAR_*`` scaling examples). Rebuild the simulator and play it from
   ``http://localhost:8080/ctrl`` → *GrooveBoxRack (MIDI)*.

The descriptor is cross-checked against ``synthdefinitions.json``: id collisions, type/track
mismatches (drum on a synth track), duplicate CC numbers and reserved member names are caught
up front. The class templates live at ``generators/RackTemplateDrum.{hpp,cpp}`` /
``generators/RackTemplateSynth.{hpp,cpp}`` and use the same ``// rackgen:…`` marker scheme
``generator.js`` uses for legacy plugins, so you can re-run the generator later when you add /
remove parameters.


Wiring a new machine into GrooveBoxRack — by hand
=================================================

If you'd rather not use ``rackgen``, the manual recipe is below. (``rackgen`` does steps 1–3
automatically and prints the snippets for step 4.)

1. **Write the class** — ``rack/RackMyVoice.{hpp,cpp}``. Start from the closest existing voice:

   - drums: ``RackDBD`` (Plaits analog bass drum), ``RackABD`` (synthetic bass drum),
     ``RackFMB`` (FM bass drum, uses ``FmDrumPrimitives.hpp``), ``RackDSD``/``RackASD``
     (snares), ``RackHH1``/``RackHH2`` (hihats), ``RackRimshot``, ``RackClap``;
   - synths: ``RackTBD03`` (a 303), ``RackMO`` (Braids macro-oscillator), ``RackWTOsc``
     (wavetable), ``RackPolyPad`` (polyphonic pad);
   - sampler: ``RackRompler`` (reads ``data.sampleRom``); audio in: ``RackInput``;
   - bus FX: ``RackFxDelay`` / ``RackFxReverb`` / ``RackFxMaster``; the per-track strip is
     ``RackChannelMixer``.

   ``rack/*.cpp`` are picked up automatically by the build (they're globbed when
   ``CONFIG_TBD_USE_SD_CARD`` is set — which it is on the TBD-16 and in the simulator).

2. **Describe it in** ``sdcard_image/data/synthdefinitions.json`` — add the machine's id
   (e.g. ``"myv"``) to the relevant track's ``machines`` list, and add a ``machines`` entry
   with its CC ``parameters`` (each ``{ "id": "...", "name": "...", "type": "cc", "ctrl": N,
   "def": D }``).

3. **Add the WebUI knobs** — add a parameter group for the new machine to
   ``sdcard_image/data/sp/mui-GrooveBoxRack.json`` (so the WebUI's GrooveBoxRack view shows
   a tab + sliders for it), and the default values to ``sdcard_image/data/sp/mp-GrooveBoxRack.json``.

4. **Hook it into the rack** — in ``components/ctagSoundProcessor/ctagSoundProcessorGrooveBoxRack.{hpp,cpp}``:

   - add a member: ``RackMyVoice chN_myv;``
   - in ``Init()``: ``dri.prefix = "chN_myv_"; chN_myv.Init(&dri);``
   - in ``Process()``, inside the track's ``if (chN.enabled) { … }`` block:
     ``chN_myv.Process(idata); if (chN_myv.enabled) mixRenderOutputMono(chN_myv.out, chN.level, chN.pan, chN.send1, chN.send2);``
   - in ``setTrackMachine()``: ``chN_myv.enabled = (machineId == "myv");``
   - in ``setTrackMachineByDeviceValue()``: add it to the track's value→id table
     (the WebUI's machine tabs send ``chN_device`` = 0 for the first tab, 4095 for any later one);
   - in ``handleMidiNoteOn()`` / ``handleMidiNoteOff()`` for that track's MIDI channel: call
     ``chN_myv.trigger()`` (drum) or ``chN_myv.noteOn(note, velocity)`` / ``noteOff(note, 0)`` (synth).

5. **Build & test** — rebuild the simulator (``cd simulator/build && cmake . && make``) and,
   when stable, the firmware (``idf.py build``). In the simulator: load ``GrooveBoxRack``,
   open ``http://localhost:8080/ctrl`` → *GrooveBoxRack (MIDI)*, and play the track from the
   drum pads / step sequencer (drum tracks) or the keyboard set to that track's MIDI channel
   (synth tracks). Switch to your machine via its tab in the main WebUI's GrooveBoxRack view.
   A headless smoke test (``simulator/build/load-test GrooveBoxRack``) constructs the rack,
   loads the preset, injects a few notes and checks the output isn't silent.


See also
========

- :doc:`Desktop Simulator <simulator>` — how to run, the ``/ctrl`` page, ``--srom`` for samplers.
- :doc:`Plugin Architecture <architecture>` — the ``ctagSoundProcessor`` factory, the SP memory
  allocator, the parameter system that GrooveBoxRack and the rack machines build on.
- :doc:`Creating a Plugin <step-by-step>` — for standalone (non-rack) ctag-tbd plugins.

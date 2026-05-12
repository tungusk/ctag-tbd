*********
Simulator
*********

The TBD Simulator runs the full DSP engine on your computer --- no hardware required.
It uses ~99% the same code as the device firmware, so you can develop, test and debug
plugins on your laptop before flashing them to real hardware.

The simulator opens a local web server that serves the same WebUI as the hardware module;
you drive it from your browser, just like a connected TBD-16. It makes the TBD platform
accessible to **everyone**, even without owning the hardware --- great for exploring the
plugin library, developing your own plugins, or teaching DSP.

.. important::

   The simulator exposes **two pages**, and for most plugins you need **both**:

   - ``http://localhost:8080/`` --- the main WebUI: pick a plugin per channel, edit its
     parameters, manage presets/samples.
   - ``http://localhost:8080/ctrl`` --- the **control / modulation** page: virtual knobs,
     CV sliders and **trigger / note buttons**. Most synth plugins are *silent until you
     send them a note or trigger* from here (just like the hardware doesn't make a sound
     until a sequencer / MIDI note arrives). If you load a plugin and hear nothing, open
     ``/ctrl`` and hit a trigger.


What the Simulator Does
=======================

- Runs the CTAG TBD audio engine natively on your host
- Serves the WebUI at ``http://localhost:8080/`` and the control page at ``http://localhost:8080/ctrl``
- Plays real-time audio out of your computer's sound card (44100 Hz / 32-bit float)
- Optionally takes a ``.wav`` file as audio *input* (for testing effect plugins)
- Optionally uses an exported sample-rom (``--srom``) for rompler / wavetable plugins


Prerequisites
=============

macOS
-----

.. code-block:: bash

   brew install cmake boost

Debian / Ubuntu
---------------

.. code-block:: bash

   sudo apt-get install libboost-filesystem-dev libboost-thread-dev libboost-program-options-dev libasound2-dev

Arch Linux
----------

.. code-block:: bash

   sudo pacman -S boost

Windows (MSYS2, 64-bit)
------------------------

Install `MSYS2 <https://www.msys2.org>`_ and launch the **MinGW 64-bit** shell (not the default MSYS shell):

.. code-block:: bash

   pacman -Syu
   # Restart the shell, then:
   pacman -Su git mingw-w64-x86_64-make mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-libtool mingw-w64-x86_64-jq mingw-w64-x86_64-boost


Building
========

1. Make sure the git submodules are present (the simulator pulls in RtAudio,
   Simple-Web-Server, esp-dsp, and the Mutable ``eurorack`` sources as submodules):

   .. code-block:: bash

      git submodule update --init --recursive

2. Navigate to the simulator directory and create a build folder:

   .. code-block:: bash

      cd simulator
      mkdir -p build && cd build

3. Run CMake and compile:

   .. code-block:: bash

      cmake .. && make

   On Windows (MSYS2), use:

   .. code-block:: bash

      cmake -G "MinGW Makefiles" ..
      mingw32-make


Running
=======

From the build directory (it expects to be run from ``simulator/build/`` --- the default
``--srom`` path is relative to it):

.. code-block:: bash

   cd simulator/build
   ./tbd-sim

Then open **both** ``http://localhost:8080/`` and ``http://localhost:8080/ctrl`` in Chrome /
Edge / Opera. The simulator runs until you press **Enter** in the terminal.

.. tip::

   Paste **one command per line** into the terminal --- don't paste lines with ``#`` comments;
   in interactive ``zsh`` the ``#`` isn't stripped and a word like *"if"* in the comment text
   can leave the shell stuck at an ``if>`` prompt (press **Ctrl-C** to recover). If you see
   ``zsh: no such file or directory: ./tbd-sim`` you're not in ``simulator/build/``.


Choosing the Audio Device
=========================

The simulator wants a sound card that supports **44100 Hz, 32-bit float**. By default it uses
device ``0``; if that device is full-duplex it runs input+output, otherwise it falls back to
output-only.

- **List the available devices and their IDs / capabilities:**

  .. code-block:: bash

     ./tbd-sim --list

- **Use a specific device** (e.g. device 3 --- a USB audio interface):

  .. code-block:: bash

     ./tbd-sim --device 3

- **Force output-only** (no audio input --- fine for synth plugins):

  .. code-block:: bash

     ./tbd-sim --output

If the simulator exits right after ``Trying to open device id: N`` it couldn't open that
device --- run ``--list`` and try another ``--device``. On a Mac, a dedicated USB audio
interface (e.g. a Focusrite Scarlett) generally gives the cleanest result; the built-in
output works too but is more prone to occasional crackle (see *Troubleshooting* below).

Step by step --- start the simulator on a chosen interface
----------------------------------------------------------

1. Go to the build directory and list the audio devices:

   .. code-block:: bash

      cd ~/Documents/GitHub/dadamachines-ctag-tbd-dev/simulator/build
      ./tbd-sim --list

   You'll get lines like::

      device = 0 PHL: PHL 272P7V: maximum duplex channels = 0
      device = 1 PHL: 27E1N1800A: maximum duplex channels = 0
      device = 3 Focusrite: Scarlett 18i8 USB: maximum duplex channels = 8
      device = 5 Apple Inc.: MacBook Air Microphone: maximum duplex channels = 0

   Note the ``device = N`` number of the interface you want to use. A ``maximum duplex
   channels`` of ``2`` or more means it can do input+output (good for effect plugins);
   ``0`` means output-only.

2. Start the simulator on that device --- e.g. the Focusrite at id 3:

   .. code-block:: bash

      ./tbd-sim --device 3

   ...or the built-in output (typically id 0), output-only:

   .. code-block:: bash

      ./tbd-sim --device 0 --output

   ...or with a WAV file as input (so effect plugins have something to process):

   .. code-block:: bash

      ./tbd-sim --device 3 --wav ~/path/to/loop.wav

3. Wait for ``Server listening on port 8080``, then open **both**
   ``http://localhost:8080/`` and ``http://localhost:8080/ctrl`` in Chrome.

One-liner (clone path + the ``Void`` workaround for the ``GrooveBoxRack`` crash + chosen device):

.. code-block:: bash

   cd ~/Documents/GitHub/dadamachines-ctag-tbd-dev && sed -i.bak 's/GrooveBoxRack/Void/g' sdcard_image/data/spm-config.json && cd simulator/build && ./tbd-sim --device 3

To restore ``spm-config.json`` afterwards:

.. code-block:: bash

   mv ~/Documents/GitHub/dadamachines-ctag-tbd-dev/sdcard_image/data/spm-config.json.bak ~/Documents/GitHub/dadamachines-ctag-tbd-dev/sdcard_image/data/spm-config.json

(Copy each command on its own line --- don't paste the ``#``-commented examples; see the
*Running* tip above.)


Command-Line Options
--------------------

.. code-block:: text

   -h [ --help ]      Show help message
   -s [ --srom ]      Sample-ROM file (default: ../../sample_rom/sample-rom.tbd — see note below)
   -l [ --list ]      List sound cards
   -d [ --device ]    Sound card device ID (default: 0)
   -o [ --output ]    Output only (no audio input)
   -w [ --wav ]       Read audio in from a WAV file (stereo float32, loops indefinitely)


Using WAV Input (for effect plugins)
====================================

Effect plugins (reverbs, delays, chorus, filters, …) process an *incoming* audio signal ---
in output-only mode there's no input, so they produce silence. Feed them a stereo, 32-bit
float WAV file instead:

.. code-block:: bash

   ./tbd-sim --wav path/to/input.wav

The file loops indefinitely. (Or use a full-duplex audio device so live input goes through.)


Plugins to Try
==============

Most plugins fall into three groups; here's what works well in the simulator and what needs
extra setup:

**Synth plugins --- work out of the box** (load one on channel A, then open ``/ctrl`` and
hit a trigger / send a note):

- ``TBD03`` --- TB-303 emulation
- ``MacOsc`` / ``MacOscDuo`` --- Mutable Braids oscillator
- ``TBDaits`` --- Mutable Plaits port
- ``Subbotnik`` --- West-coast style voice
- ``SubSynth`` --- band-pass-cascade sub synth
- ``Karpuskl`` --- Karplus–Strong string
- ``Talkbox`` --- oscillator + talkbox effect
- ``Bjorklund`` --- Euclidean-rhythm pattern voice
- ``Antique``, ``PolyPad``, ``APCpp``, ``SineSrc``, ``Hihat1``, ``RecNPlay``

**Effect plugins --- need an audio input.** Reverbs (``MIVerb``, ``GDVerb``, ``GVerb``,
``FVerb``, …), delays (``MonoDelay``, ``CDelay``, ``TDelay``, ``StrampDly``, ``FBDlyLine``),
chorus/ensemble (``EChorus``, ``MIChorus``, ``MIEnsemble``), filters (``MoogFilt``, ``MISVF``),
``CStrip``/``CStripM``, ``BCSR``, ``SpaceFX``, ``Claude`` (Clouds), ``TBDings`` (Rings) — run
the simulator with ``--wav some.wav`` (or a full-duplex device) or you'll just hear silence.

**Sample / wavetable plugins --- need a sample-rom.** ``Rompler``, ``WTOsc``, ``WTOscDuo``,
``Freakwaves``, ``VctrSnt`` use sample/wavetable data that isn't shipped in the repo. Pass an
exported sample-rom with ``--srom path/to/sample-rom.tbd`` (export it from a TBD-16's WebUI),
otherwise they'll be silent.

.. warning::

   ``GrooveBoxRack`` (and ``DrumRack``) currently **crash the simulator** when loaded ---
   the sample/macro/rack layer relies on a sample-rom layout the simulator doesn't yet
   provide. Don't select them in the simulator for now. (The committed
   ``sdcard_image/data/spm-config.json`` boots ``GrooveBoxRack`` on channel A on the *device*;
   for the simulator, set ``activeProcessors`` there to ``["Void","Void"]`` so it starts
   cleanly, then load whatever plugin you want from the WebUI.)


Modulation Simulation (the ``/ctrl`` page)
==========================================

``http://localhost:8080/ctrl`` mirrors the TBD-16's modulation inputs: virtual knobs/sliders
for the CV and parameter inputs, and **trigger / note buttons**. Use it to play synth plugins
and to test parameter modulation without hardware. (You configure *which* control drives
*which* parameter from the main WebUI, the same way as on the device.)


Developing Plugins with the Simulator
=====================================

The simulator uses the same plugin code and directory structure as the firmware:

1. Create your plugin in ``components/ctagSoundProcessor/`` as in the
   :doc:`Plugin Tutorial </plugins/step-by-step>`.
2. ``cd simulator/build && cmake .. && make && ./tbd-sim`` --- test immediately, no flashing.
3. Once stable, build the ESP-IDF firmware (``idf.py build``) and flash to hardware.

Registering a New Plugin
------------------------

The simulator **re-scans** ``sdcard_image/data/sp/mui-*.json`` for the plugin list on every
startup, so a new (or renamed) plugin appears as soon as you restart ``tbd-sim`` --- no
manual step needed. (On the *device* the list is cached in ``spm-config.json``'s
``availableProcessors`` to keep boot fast; the simulator ignores that cache.)

If something still looks stale --- e.g. after pulling changes --- a ``git checkout
sdcard_image/data/spm-config.json`` resets that file (the simulator writes runtime state into
it, so it tends to get dirty), then restart.


Troubleshooting / Known Issues
==============================

**Occasional crackle / glitches in the audio.** The simulator runs the DSP in 32-sample
blocks --- the same as the device --- which is a very tight ~0.7 ms latency for a
non-realtime desktop OS, so a brief scheduling hiccup can cause an underrun (crackle). It
asks for a realtime-priority audio thread, but to minimise it: use a **dedicated USB audio
interface** rather than built-in/HDMI audio, pick that device with ``--device N``, plug your
laptop into power (not battery), and close CPU-heavy apps. (This is a desktop-only artifact;
the device itself runs the same DSP on a realtime scheduler.)

**The simulator exits right after ``Trying to open device id: N``.** It couldn't open that
audio device at 44100 Hz / 32-bit float. Run ``./tbd-sim --list`` and try another
``--device``; ``--output`` forces output-only.

**``GrooveBoxRack`` / ``DrumRack`` crash the simulator.** Known issue (see the warning
above) --- don't load them in the sim yet; keep ``spm-config.json`` defaulting to ``Void``.

**``Trying to open sample rom file ... sample-rom.tbd``** then it keeps going --- harmless;
that file isn't shipped, so sample/wavetable plugins won't have data. Pass one with ``--srom``
if you need them.

**The shell gets stuck at an ``if>`` prompt** when you paste commands --- you pasted a line
containing a ``#`` comment (interactive ``zsh`` doesn't treat ``#`` as a comment, and a word
like *"if"* in the comment text starts an ``if`` statement). Press **Ctrl-C**; paste only the
bare command. (Or ``setopt interactivecomments``.)


Cloud Builds
============

GitHub Actions can build simulator binaries for Windows and macOS automatically. Enable
Actions for your fork, then customize the workflows in ``.github/workflows/`` to suit.

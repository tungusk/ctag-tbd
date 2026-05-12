# Third-party components

Inventory of code in this repository that is **not** original to dadamachines /
Per-Olov Jernberg / CTAG Kiel, with its licence and whether it is safe to ship
under a **proprietary commercial licence** of the TBD platform (permissive = yes;
GPL = needs handling — replace, isolate, or obtain the author's permission).

> ⚠ Entries marked **VERIFY** were classified by inspection of the directory's
> own `LICENSE`/`readme`/source headers and still need a final check (lawyer
> audit). Per-file headers and the component's own `LICENSE` file are
> authoritative. PRs welcome to tighten this list.

## Vendored in `components/`

| Path | Origin | Licence | Commercial-safe? | Notes |
|---|---|---|---|---|
| `components/ctagSoundProcessor/airwindows/` | Airwindows (Chris Johnson) | **MIT** | ✅ yes | see `airwindows/LICENSE` |
| `components/ctagSoundProcessor/mutable/` | Mutable Instruments (Émilie Gillet) | **MIT** (VERIFY) | ✅ yes | Plaits/Braids/Rings/… derived DSP; Mutable's code is MIT |
| `components/ctagSoundProcessor/mifx/` | Mutable-Instruments-derived fx | **MIT** (VERIFY) | ✅ yes (if MIT) | "Mi…" = Mutable-derived |
| `components/ctagSoundProcessor/polypad/` | Mutable-derived (`ChordSynth`, `MiSuperSawOsc`) | **MIT** (VERIFY) | ✅ yes (if MIT) | |
| `components/ctagSoundProcessor/vult/` | Vult-generated DSP (Leonardo Laguna Ruiz) | **VERIFY** (Vult-generated code is typically MIT / permissive) | ⚠ verify | |
| `components/ctagSoundProcessor/tesselode/` | "tesselode" audio lib (port) | **VERIFY** — see `tesselode/license.md` | ⚠ verify | |
| `components/ctagSoundProcessor/SimpleComp/` | "Simple Source" dynamics (Bojan Markovic / ChunkWare-style) | **VERIFY** (often released "without restriction") | ⚠ verify | `SimpleComp`/`SimpleLimit`/`SimpleGate` |
| `components/ctagSoundProcessor/freeverb/` | Freeverb (Jezar, Dreampoint, 2000) | **Public domain** (VERIFY) | ✅ yes | see `freeverb/readme.txt` |
| `components/ctagSoundProcessor/freeverb3/` | Freeverb3 (Teru Kamogashira) | **VERIFY** — likely GPL or LGPL | ⚠ if GPL/LGPL → handle for a proprietary build | dadamachines has modified this dir |
| `components/ctagSoundProcessor/gverb/` | GVerb (Juhana Sadeharju / Steve Harris, LADSPA) | **GPL-2.0-or-later** (VERIFY) | ⚠ GPL → handle for a proprietary build | |
| `components/ctagSoundProcessor/memory/` | small memory-pool lib | **VERIFY** — see `memory/LICENSE` (likely zlib/MIT) | ⚠ verify | |
| `components/rapidjson/` | RapidJSON (Tencent / Milo Yip) | **MIT** | ✅ yes | |
| `components/ableton_link/link/` (submodule `Ableton/link`) | Ableton Link | **GPL-2.0-or-later** (open-source option; Ableton also offers a proprietary licence) | ⚠ GPL — compatible with our GPLv3 firmware; for a proprietary build, get Ableton's Link licence | nested submodule `link/modules/asio-standalone` = **Boost Software License 1.0** ✅ |
| `components/moog/MoogLadders/` (submodule `ctag-fh-kiel/MoogLadders`) | MoogLadders (ddiakopoulos et al.) | **Unlicense / public domain** (VERIFY — some individual models cite academic sources) | ✅ yes (if Unlicense) | see `MoogLadders/LICENSE` |
| `components/ctagSoundProcessor/{filters,fx,helpers,synthesis}/` | dadamachines / CTAG | — | — | not third-party (own code) |
| `components/ctagSoundProcessor/rack/`, `ctagSoundProcessorGrooveBoxRack.{cpp,hpp}` | **Per-Olov Jernberg (possan)** + Johannes Elias Lohbihler (dadamachines); based in part on the CTAG TBD DrumRack by Robert Manzke | GPL-3.0-only | — | team code, not third-party — see per-file headers + LICENSE |

## Submodules in `simulator/` (desktop simulator only — not in device firmware)

| Path | Origin | Licence | Commercial-safe? |
|---|---|---|---|
| `simulator/Simple-Web-Server` (`gitlab.com/eidheim/Simple-Web-Server`) | Ole Christian Eidheim | **MIT** | ✅ yes |
| `simulator/rtaudio` (`github.com/thestk/rtaudio`) | Gary P. Scavone | **MIT-style** (RtAudio licence — permissive, "modified files must carry a notice") | ✅ yes |
| `simulator/esp-dsp` (`github.com/ctag-fh-kiel/esp-dsp`, fork of `espressif/esp-dsp`) | Espressif | **Apache-2.0** | ✅ yes |

## Build / runtime dependencies (not vendored here)

| Component | Licence |
|---|---|
| ESP-IDF (Espressif IoT Development Framework) | Apache-2.0 |
| `@shoelace-style/shoelace` (WebUI, pulled via npm) | **MIT** |
| `webaudio-controls.js`, `Sortable.min.js` (WebUI, bundled) | webaudio-controls = MIT; Sortable = MIT |
| Shoelace theme CSS (`sdcard_image/www/shoelace/themes/*.css`) | MIT |
| Sphinx + theme + ablog (docs build) | BSD-2-Clause / MIT (varies by package) |

## Implications for the commercial licence

A "fully proprietary" commercial build of the **firmware** is undermined by any
**GPL-licensed** dependency above (currently: Ableton Link — GPL-2.0+ open-source
option; gverb — GPL; possibly freeverb3). Options for those: (1) obtain the
author's commercial/separate licence (Ableton sells a Link licence), (2) replace
the component, or (3) make the feature optional and ship it only in GPL builds.
The **WebUI** itself is proprietary (© dadamachines / Johannes Elias Lohbihler),
so its bundled MIT components (Shoelace, Sortable, webaudio-controls) are fine —
their notices just need to be preserved.

This file is the starting inventory for the lawyer audit referenced in
`DUAL-LICENSE-SETUP.md` §2.4 and `MASTER-PLAN.md`.

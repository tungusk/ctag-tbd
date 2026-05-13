# dadamachines TBD-16

The first standalone desktop audio DSP platform based on [CTAG TBD](https://github.com/ctag-fh-kiel/ctag-tbd), with standard MIDI connectivity — designed to bring open-source audio processing beyond Eurorack.

**TBD-16** combines 50+ high-quality generators and effects in a modular, extensible architecture. It is built for musicians, educators, and audio researchers who want hands-on DSP without proprietary lock-in.

## Documentation

**[dadamachines.github.io/ctag-tbd](https://dadamachines.github.io/ctag-tbd/)**

## What This Fork Does

This repository is a fork of [ctag-fh-kiel/ctag-tbd](https://github.com/ctag-fh-kiel/ctag-tbd) (branch `p4_main`), adapted for the **dadamachines TBD-16** hardware. Our focus:

- **UI/UX** — Redesigned web interface with musician-friendly interaction patterns
- **Documentation** — Clear guides, example workflows, and UX guidelines for plugin developers
- **Desktop Hardware** — Standalone form factor with standard MIDI, no Eurorack required

The DSP engine, plugin system, and core firmware are developed upstream by [Robert Manzke / CTAG](https://www.creative-technologies.de/).

## Getting Started

See the [documentation](https://dadamachines.github.io/ctag-tbd/) for setup guides, plugin reference, and flashing instructions.

## Hardware Configurations

The firmware supports four hardware configurations for different board
variants — from a minimal flash-only build to the full TBD-16 with SD card
and RP2350 sequencer bridge. See [HARDWARE_CONFIGURATIONS.md](HARDWARE_CONFIGURATIONS.md) for
the feature matrix, build instructions, and Kconfig flag reference.

## Project Structure

```
components/         DSP plugins and sound processors
docs/               Sphinx documentation source
main/               Firmware entry point and system management
sdcard_image/       SD card image (samples, kits, presets, web UI)
simulator/          Desktop simulator for plugin development
tools/              Build scripts and sample utilities
generators/         Plugin scaffolding templates
```

### Building Documentation Locally

To build and preview the documentation (including the blog) on your local machine:

1.  **Install requirements**:
    ```bash
    pip install -r docs/requirements.txt
    ```

2.  **Build HTML**:
    ```bash
    sphinx-build -b html -c docs/config docs build/docs
    ```
    
3.  **View**:
    Open `build/docs/index.html` in your browser.

## Community & Support

- [dadamachines Forum](https://forum.dadamachines.com) -- Ask questions, share patches, and connect with other TBD users and developers.
- [GitHub Issues](https://github.com/dadamachines/ctag-tbd/issues) -- Bug reports and feature requests.

## Contributing

Contributions are welcome — see [CONTRIBUTING.md](CONTRIBUTING.md) for the
full guide covering the branch model, CI pipelines, build instructions,
and artifact naming conventions.

## Acknowledgements

**CTAG TBD** was created by [Robert Manzke](https://github.com/ctag-fh-kiel/ctag-tbd) at the [Creative Technologies Arbeitsgruppe](https://www.creative-technologies.de/), Kiel University of Applied Sciences. 

The TBD-16 adaptation is led by [dadamachines](https://dadamachines.com).

UX and instrument design contributions by [Benjamin Weiss / instrument-design](https://instrument-design.com/work/).

## Funding

This project is partially funded through the [NGI0 Commons Fund](https://nlnet.nl/commonsfund), established by [NLnet](https://nlnet.nl/) with financial support from the European Commission's [Next Generation Internet](https://ngi.eu/) programme, under grant agreement No [101135429](https://cordis.europa.eu/project/id/101135429).

Not all work on TBD / TBD-16 is covered by NLnet funding.

[<img src="https://nlnet.nl/logo/banner-320x120.png" alt="NLnet" width="160">](https://nlnet.nl/project/TBD-DSP-Toolkit/)

## License

**Firmware & tooling** -- [GNU General Public License v3.0 (GPL-3.0-only)](https://www.gnu.org/licenses/gpl-3.0.txt). This covers the upstream CTAG DSP engine / sound processors / platform core **and** the dadamachines / Per-Olov Jernberg additions (REST API, macro/preset system, rack layer, drivers, build tools, simulator, tests). Modifications distributed must be released under GPL-3.0.

**WebUI** (`sdcard_image/www/` — dadamachines-authored HTML/JS/CSS) -- **proprietary: © 2014-2026 dadamachines / Johannes Elias Lohbihler. All rights reserved.** *Not* under the GPL. It's a separate program talking to the firmware over its REST API, not a derivative work of the firmware. Reuse requires written permission. (Vendored web components — Shoelace, webaudio-controls, Sortable — keep their own licences; per-file headers are authoritative.)

**Commercial license** -- Building a commercial product on TBD without GPL-3.0's source-disclosure obligations (closed-source firmware, ship without attribution, custom OEM terms, or use of the WebUI) -- a commercial licence is available. Contact dadamachines: https://dadamachines.com/contact/

**TBD-Core & TBD-16 Hardware** -- The dadamachines TBD-16 (desktop instrument) and TBD-Core (core DSP board for custom products) hardware designs are proprietary.

**Planned Open-Hardware Core Design** -- An open-hardware core design based on the same ESP32-P4 + RP2350 platform is planned for future publication in KiCad as an open-source reference.

**Original CTAG Hardware** (V1/V2) -- [Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)](https://creativecommons.org/licenses/by-nc-sa/4.0/).

**Third-party components** -- vendored libraries under `components/` and `sdcard_image/www/` retain their own per-file licence headers / `LICENSE` / `readme` files (Mutable Instruments-derived MIT DSP, Airwindows MIT, RapidJSON MIT, Ableton Link GPL-2.0+, Shoelace MIT, etc.).

Copyright (c) 2020-2026 Robert Manzke. All rights reserved. (CTAG TBD core / engine, original hardware research)

Copyright (c) 2024-2026 Per-Olov Jernberg (possan), https://possan.codes (macro/preset system, rack layer)

Copyright (c) 2024-2026 Johannes Elias Lohbihler for dadamachines. (TBD-16 adaptation, incl. REST API, WebUI, documentation & contributions to macro/preset system, rack layer and simulator)

See [LICENSE](LICENSE) for full details including trademark, commercial-use and contribution (CLA) terms. Contributions to the firmware are welcome under GPL-3.0 — see [CONTRIBUTING.md](CONTRIBUTING.md).

# License-header audit — every file with a wrong "LGPL 3.0" header

> **Status:** index + tooling. Generated 2026-05-12 against `dadamachines-ctag-tbd-dev` (`dada-tbd-master`).
> Re-generate the list anytime with `tools/fix-license-headers.py --list`.
> This feeds **`MASTER-PLAN.md` Phase 2** (the single "clean trunk" commit) and **Phase 2b** (the `PicoSeqRack`→`GrooveboxRack` rename).

**102 tracked files** currently contain "LGPL" / "Lesser General Public" (excluding `build/`, `managed_components/`, `*.gz`, `docs/*.md`). They split into four buckets:

| Bucket | Count | Target | Done by |
|---|---|---|---|
| **A — firmware / tooling source** (`*.cpp *.hpp *.h *.c`, `tests/webui/run-tests.js`) | **79** | GNU **GPL 3.0** header (`SPDX: GPL-3.0-only`); existing copyright line(s) preserved verbatim, incl. **Per-Olov Jernberg (possan)** where present | `tools/fix-license-headers.py` |
| **B — the WebUI** (`sdcard_image/www/` dadamachines-authored `.html .js .css`) | **14** | **proprietary** header: `(c) <years> dadamachines / Johannes Elias Lohbihler. All rights reserved.` — *not* under the GPL (separate REST-client program; German UrhG → name the author) | `tools/fix-license-headers.py` |
| **C — `LICENSE` / `README` / `CONTRIBUTING` + docs prose** | **7** | hand-rewritten as part of the `LICENSE` overhaul (GPLv3 + hardware/brand/commercial sections; CLA paragraph; etc.) | **by hand**, in the Phase-2 commit |
| **D — built WebUI bundles** (`sdcard_image/www/js/{app-bundle,macro-bundle}.js` + their `.gz`) | 2 (+gz) | not edited — **regenerate** with `sdcard_image/www/build-webui.sh` *after* the bucket-B source files are fixed | `build-webui.sh` |

(Vendored web assets — Shoelace, `webaudio-controls.js`, `Sortable.min.js`, the Shoelace theme CSS — carry no LGPL header and aren't touched; they keep their own licences and belong in `THIRD-PARTY.md`.)

---

## Bucket A → GPLv3 (79 files)

Two header *variants* exist in this bucket, both → the same GPLv3 header:
- the plain "Johannes / dadamachines" variant (title *"TBD-16 — dadamachines WebUI & REST API"* / *"Sample Manager REST API"*), and
- the **possan** variant (title *"TBD-16 — Macro/Preset System & PicoSeqRack"*, `(c) Per-Olov Jernberg (possan). https://possan.codes`, plus a *"dadamachines has a commercial license to use this code … other commercial use requires a separate license agreement"* note). The script keeps possan's copyright line; the licence body becomes GPLv3 and the bespoke commercial note is replaced by the standard *"a commercial licence is available — contact dadamachines"* line. **⚠ Confirm with possan** that he's fine with his files being plain GPLv3 in the firmware (his copyright preserved) — his current headers assert a separate arrangement. (`MASTER-PLAN.md` Phase 2b note.)

| Area | Files |
|---|---|
| `main/` — REST/API layer (Johannes variant) | `DeviceAPI.{cpp,hpp}`, `Favorites.{cpp,hpp}`, `FavoritesModel.{cpp,hpp}`, `MacroAPI.{cpp,hpp}`, `PluginAPI.{cpp,hpp}`, `SampleAPI.{cpp,hpp}` |
| `main/` — macro/preset/synth/track + SPI (possan variant) | `MacroDeviceDefinition.{cpp,hpp}`, `MacroDeviceDefinitionDataModel.{cpp,hpp}`, `MacroSoundPreset.{cpp,hpp}`, `MacroSoundPresetDataModel.{cpp,hpp}`, `MacroTranslator.{cpp,hpp}`, `SynthDefinition.{cpp,hpp}`, `SynthDefinitionDataModel.{cpp,hpp}`, `TrackDefinition.{cpp,hpp}`, `SpiProtocol.h`, `SpiProtocolHelper.{cpp,hpp}` |
| `components/ctagSoundProcessor/` (possan variant) | `ctagSoundProcessorPicoSeqRack.{cpp,hpp}` *(→ renamed `…GrooveboxRack.{cpp,hpp}` in Phase 2b)* |
| `components/ctagSoundProcessor/rack/` (possan variant) — 39 files | `RackABD`, `RackASD`, `RackChannelMixer`, `RackClap`, `RackDBD`, `RackDSD`, `RackFMB`, `RackFxDelay`, `RackFxMaster`, `RackFxReverb`, `RackHH1`, `RackHH2`, `RackInput`, `RackMO`, `RackPolyPad`, `RackRimshot`, `RackRompler`, `RackTBD03`, `RackWTOsc` (each `.cpp`+`.hpp`) + `RackSynth.hpp` |
| `components/drivers/` — Pico firmware-update (Johannes copyright, possan-style note) | `pico_firmware_update.{cpp,hpp}`, `pico_reset.{cpp,hpp}`, `picoboot3_master.{cpp,hpp}` |
| `tests/` | `tests/webui/run-tests.js` (a Node test runner — tooling, hence GPLv3 not "WebUI") |

## Bucket B → proprietary WebUI header (14 files)

`(c) 2014-2026 dadamachines / Johannes Elias Lohbihler. All rights reserved.` — *not* GPL.

| Area | Files |
|---|---|
| `sdcard_image/www/` HTML | `index.html`, `preset-macro-manager.html`, `webui-update.html` |
| `sdcard_image/www/js/` | `app.js`, `shared.js`, `plugin-manager.js`, `sample-manager.js`, `display-hints.js`, `factory-manifest.js`, `track-defaults.js`, `designer.js`, `performer.js`, `preset-macro-app.js` |
| `sdcard_image/www/css/` | `app.css` |

## Bucket C → hand-edit in the Phase-2 commit (7 files)

| File | What it needs |
|---|---|
| `LICENSE` | full rewrite — single **GPLv3** for firmware/tooling; **WebUI = proprietary** (© dadamachines / Johannes Elias Lohbihler); delete the "dadamachines Additions = LGPL 3.0" section; hardware (CC BY-NC-SA 4.0 for CTAG V1/V2, proprietary for TBD-16/TBD-Core); trademarks; "commercial licence available — contact us"; "see THIRD-PARTY.md"; disclaimer; keep upstream CTAG attribution |
| `README.md` | replace its LGPL paragraph; add the Bela-style "commercial licence available" note |
| `CONTRIBUTING.md` | LGPL → GPLv3; add the **CLA paragraph** (why: dual-licensing); maintainer list + branch model |
| `docs/about/10_credits.rst` | rewrite the licensing section — drop "dadamachines Additions (LGPL 3.0)" / "Plugins (LGPL 3.0)" / "We chose LGPL because…"; firmware = GPLv3; WebUI = proprietary; plugins = GPLv3 |
| `docs/apps/groovebox.rst` | `PicoSeqRack plugin (LGPL 3.0)` → `GrooveboxRack plugin (GPL 3.0)` (Phase-2b rename) |
| `docs/faq.rst` | rewrite the "**LGPL 3.0**, which is more permissive…" answer → GPLv3 + the commercial-licence option |
| `docs/index.rst` | "fully open source (GPL/LGPL)" / "LGPL 3.0" → GPLv3 (and note the WebUI is proprietary) |

## Bucket D → regenerate, don't edit (2 + .gz)

`sdcard_image/www/js/app-bundle.js`, `sdcard_image/www/js/macro-bundle.js` (and the matching `.gz`): these are concatenations of the bucket-B source `.js` (which carry the LGPL line) plus vendored `Sortable.min.js` (MIT). After fixing the bucket-B sources, run `cd sdcard_image/www && ./build-webui.sh` to regenerate; the production pipeline (`create_sd_archive.sh`) re-gzips. (Once `PicoSeqRack`→`GrooveboxRack` lands, the macro WebUI may also need touch-ups — see `MASTER-PLAN.md` Phase 2b step 3.)

---

## The script — `tools/fix-license-headers.py`

```bash
# from the repo root:
tools/fix-license-headers.py --list      # print this audit (live classification)
tools/fix-license-headers.py --dry-run   # show which files would change; touch nothing
tools/fix-license-headers.py             # apply buckets A + B in place
git diff                                 # review
# then, separately:  cd sdcard_image/www && ./build-webui.sh   # regenerate bucket-D bundles
```

What it does:
- discovers the LGPL-tagged files via `git grep`, classifies each by path/extension into A / B / C(manual) / D(bundle);
- for **A**: replaces the leading comment's LGPL body with the GPLv3 body + `SPDX-License-Identifier: GPL-3.0-only`, **preserving every `(c) …` line** found in the old header (so possan's line stays) and any descriptive lines (title, "Run:", "Vanilla JS", …);
- for **B**: replaces it with the proprietary "all rights reserved — dadamachines / Johannes Elias Lohbihler" body (years carried over from the old `(c)` line);
- preserves the original comment delimiter style (`/* … */` block, `<!-- … -->`, or `// …` lines) and any `#!` shebang;
- never touches the **C** (manual) files or the **D** (bundle) files;
- is idempotent (a file already on the new header is skipped) and prints a summary; exits non-zero if any file can't be classified.

**Do not touch** files carrying the upstream `CTAG TBD >>to be determined<< … (c) … Robert Manzke …` header — those stay GPLv3 with Robert's header as-is. (None of them appear in the list above, but the script also wouldn't match them since it only acts on files containing the LGPL marker.)

Run this as the first part of `MASTER-PLAN.md` **Phase 2** (then hand-edit the bucket-C files, do the `THIRD-PARTY.md` + `.gitmodules` + CI-payload changes, build, and commit it all as the one "clean trunk" commit; cherry-pick onto `staging`).

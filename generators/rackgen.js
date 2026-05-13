#!/usr/bin/env node
/***************
TBD-16 — GrooveBoxRack machine scaffolder.

Reads a small descriptor JSON (see rack-template.json) and emits:
  - components/ctagSoundProcessor/rack/<className>.{hpp,cpp}  — class boilerplate
    (atomic param members, registerParamAndCC() calls in Init(), Process() skeleton)
  - patches for sdcard_image/data/synthdefinitions.json
                 sdcard_image/data/sp/mui-GrooveBoxRack.json
                 sdcard_image/data/sp/mp-GrooveBoxRack.json
  - the exact snippets you still need to paste into
    components/ctagSoundProcessor/ctagSoundProcessorGrooveBoxRack.{hpp,cpp}.

Usage:
  node rackgen.js <descriptor.json>          # dry run: write .hpp/.cpp to cwd, PRINT json patches
  node rackgen.js <descriptor.json> -i       # in-place: write into the source tree
                                             # and patch the 3 JSON files (a *.bak is left)

Mirrors generator.js (for legacy ctag-tbd plugins). Node.js, no external deps.

SPDX-License-Identifier: GPL-3.0-only
***************/

'use strict';
const fs = require('fs');
const path = require('path');

// ------ args + descriptor -----------------------------------------------------------------
const args = process.argv.slice(2);
if (args.length < 1 || args.length > 2 || (args[1] && args[1] !== '-i')) {
    console.error('usage:  node rackgen.js <descriptor.json> [-i]'); process.exit(2);
}
const descPath = args[0];
const inPlace  = args[1] === '-i';

let desc;
try { desc = JSON.parse(fs.readFileSync(descPath, 'utf8')); }
catch (e) { console.error(`could not parse ${descPath}: ${e.message}`); process.exit(2); }

// ------ paths ----------------------------------------------------------------------------
// the descriptor / generators live in <repo>/generators ; resolve repo root from this script
const repoRoot     = path.resolve(__dirname, '..');
const synthdefPath = path.join(repoRoot, 'sdcard_image/data/synthdefinitions.json');
const muiPath      = path.join(repoRoot, 'sdcard_image/data/sp/mui-GrooveBoxRack.json');
const mpPath       = path.join(repoRoot, 'sdcard_image/data/sp/mp-GrooveBoxRack.json');
const rackDir      = path.join(repoRoot, 'components/ctagSoundProcessor/rack');
const tmplDrumHpp  = path.join(__dirname, 'RackTemplateDrum.hpp');
const tmplDrumCpp  = path.join(__dirname, 'RackTemplateDrum.cpp');
const tmplSyHpp    = path.join(__dirname, 'RackTemplateSynth.hpp');
const tmplSyCpp    = path.join(__dirname, 'RackTemplateSynth.cpp');

// ------ validate descriptor --------------------------------------------------------------
const need = ['id','className','name','type','track','params'];
for (const k of need) if (!(k in desc)) die(`descriptor missing required field "${k}"`);
if (!/^[a-z][a-z0-9]{0,11}$/.test(desc.id))        die(`id "${desc.id}" must be 1..12 chars, lowercase / digits, starting with a letter`);
if (!/^[A-Z][A-Za-z0-9]*$/.test(desc.className))   die(`className "${desc.className}" must be PascalCase`);
if (!['drum','synth'].includes(desc.type))         die(`type must be "drum" or "synth" (got "${desc.type}")`);
if (!Number.isInteger(desc.track) || desc.track < 0 || desc.track > 15) die(`track must be an integer 0..15`);
if (!Array.isArray(desc.params) || desc.params.length === 0)            die(`params must be a non-empty array`);

const RESERVED = new Set(['out','enabled','midi_trig','trig_prev','gate','cur_note','cur_vel','this']);
const seenP = new Set(), seenCC = new Set();
for (const p of desc.params) {
    for (const k of ['id','name','ctrl','def']) if (!(k in p)) die(`param missing "${k}": ${JSON.stringify(p)}`);
    if (!/^[a-z_][a-z0-9_]*$/.test(p.id)) die(`param id "${p.id}" must be a snake_case C++ identifier`);
    if (RESERVED.has(p.id))               die(`param id "${p.id}" clashes with a reserved member name`);
    if (seenP.has(p.id))                  die(`duplicate param id "${p.id}"`);
    seenP.add(p.id);
    if (!Number.isInteger(p.ctrl) || p.ctrl < 1 || p.ctrl > 127) die(`param "${p.id}" ctrl must be 1..127`);
    if (p.ctrl < 8) console.warn(`! warning: param "${p.id}" ctrl=${p.ctrl} is in the channel mixer's range (1..7) — will likely collide`);
    if (seenCC.has(p.ctrl))               die(`duplicate ctrl ${p.ctrl} on params "${p.id}" / "${[...seenCC].find(c=>c===p.ctrl)}"`);
    seenCC.add(p.ctrl);
    if (!Number.isInteger(p.def) || p.def < 0 || p.def > 127) die(`param "${p.id}" def must be 0..127 (MIDI-CC default)`);
}

// ------ cross-check against synthdefinitions.json ----------------------------------------
let synth;
try { synth = JSON.parse(fs.readFileSync(synthdefPath, 'utf8')); }
catch (e) { die(`could not read ${synthdefPath}: ${e.message}`); }

const track = (synth.tracks || []).find(t => t.index === desc.track);
if (!track) die(`track index ${desc.track} not found in synthdefinitions.json`);
if (track.type !== desc.type)
    die(`track ${desc.track} ("${track.name}") is type "${track.type}" — descriptor type "${desc.type}" doesn't match`);
if ((track.machines || []).includes(desc.id))
    die(`track ${desc.track} already has a machine with id "${desc.id}"`);
if ((synth.machines || []).some(m => m.id === desc.id))
    die(`a machine with id "${desc.id}" already exists in synthdefinitions.json (machines[])`);

const chN     = desc.track + 1;            // WebUI CHnn (1-based)
const prefix  = `ch${chN}_${desc.id}_`;    // C++ / param-id prefix

// ------ render the C++ from the template -------------------------------------------------
const tHpp = fs.readFileSync(desc.type === 'drum' ? tmplDrumHpp : tmplSyHpp, 'utf8');
const tCpp = fs.readFileSync(desc.type === 'drum' ? tmplDrumCpp : tmplSyCpp, 'utf8');
const tName = desc.type === 'drum' ? 'RackTemplateDrum' : 'RackTemplateSynth';

const hppMembers = '\t\tatomic<int16_t> ' + desc.params.map(p => p.id).join(', ') + ';';
const cppRegs = desc.params.map(p =>
    `\tinitdata->rack->registerParamAndCC(initdata, "${p.id}", ${p.ctrl}, [&](const int val){ this->${p.id} = val; });`
).join('\n');

const hppOut = replaceSection(
    tHpp.replace(new RegExp(tName, 'g'), desc.className),
    'rackgen:hppMembers', '\n' + hppMembers + '\n\t');
const cppOut = replaceSection(
    tCpp.replace(new RegExp(tName, 'g'), desc.className),
    'rackgen:cppRegs', '\n' + cppRegs + '\n');

// ------ build the JSON patches -----------------------------------------------------------
const synthdefMachineEntry = {
    id: desc.id, name: desc.name, type: desc.type,
    parameters: desc.params.map(p => ({ id: p.id, name: p.name, type: 'cc', ctrl: p.ctrl, def: p.def }))
};
const insertBefore = desc.type === 'drum' ? 'extdrum' : 'extsynth';
const machinesPatched = [...track.machines];
const at = machinesPatched.indexOf(insertBefore);
if (at >= 0) machinesPatched.splice(at, 0, desc.id);
else         machinesPatched.push(desc.id);

const muiGroup = {
    id: `ch${chN}_${desc.id}`,
    name: `Channel ${chN} - ${desc.name}`,
    type: 'group',
    params: desc.params.map(p => ({ id: prefix + p.id, name: p.name, type: 'int', min: 0, max: 4095 }))
};
const mpDefaults = desc.params.map(p => ({
    id: prefix + p.id,
    current: Math.round(p.def * 4096 / 128),       // CC default 0..127 → preset value 0..4096
    cv: -1
}));

// ------ apply (in-place) OR print (dry-run) ----------------------------------------------
const outDir = inPlace ? rackDir : process.cwd();
fs.writeFileSync(path.join(outDir, desc.className + '.hpp'), hppOut);
fs.writeFileSync(path.join(outDir, desc.className + '.cpp'), cppOut);

let muiPatched = false, mpPatched = false, synthPatched = false;
if (inPlace) {
    // synthdefinitions.json
    backup(synthdefPath);
    synth.tracks.find(t => t.index === desc.track).machines = machinesPatched;
    synth.machines = synth.machines || [];
    synth.machines.push(synthdefMachineEntry);
    fs.writeFileSync(synthdefPath, JSON.stringify(synth, null, 2) + '\n');
    synthPatched = true;

    // mui-GrooveBoxRack.json — append the group to params[trackIndex]
    const mui = JSON.parse(fs.readFileSync(muiPath, 'utf8'));
    backup(muiPath);
    if (Array.isArray(mui.params) && mui.params[desc.track] && Array.isArray(mui.params[desc.track].params)) {
        // mui's params[N] is each channel's group; the machines hang off its `params` array as sub-groups.
        // (See mui-GrooveBoxRack.json for the structure.)
        // We append our new group right before the mixer-strip params if we can identify them, else at the end.
        mui.params[desc.track].params.push(muiGroup);
        fs.writeFileSync(muiPath, JSON.stringify(mui, null, 2) + '\n');
        muiPatched = true;
    } else {
        console.warn(`! mui-GrooveBoxRack.json: couldn't find params[${desc.track}] — paste the group manually (see below)`);
    }

    // mp-GrooveBoxRack.json — append default values into patches[0].params
    const mp = JSON.parse(fs.readFileSync(mpPath, 'utf8'));
    backup(mpPath);
    if (mp.patches && mp.patches[0] && Array.isArray(mp.patches[0].params)) {
        mp.patches[0].params.push(...mpDefaults);
        fs.writeFileSync(mpPath, JSON.stringify(mp, null, 2) + '\n');
        mpPatched = true;
    } else {
        console.warn(`! mp-GrooveBoxRack.json: unexpected shape — paste defaults manually (see below)`);
    }
}

// ------ report ---------------------------------------------------------------------------
console.log(`generated ${desc.className}.hpp + .cpp ${inPlace ? `in ${rackDir}` : `in ${process.cwd()}`}`);
if (inPlace) {
    console.log(`patched: ${synthPatched ? '✓' : '✗'} synthdefinitions.json   `
              + `${muiPatched ? '✓' : '✗'} mui-GrooveBoxRack.json   ${mpPatched ? '✓' : '✗'} mp-GrooveBoxRack.json   (.bak files kept)`);
} else {
    console.log('\n--- patches to apply (dry run; pass -i to apply them) ---');
    console.log(`\n# 1. sdcard_image/data/synthdefinitions.json — append "${desc.id}" to tracks[${desc.track}].machines (before "${insertBefore}"), then append:`);
    console.log(JSON.stringify(synthdefMachineEntry, null, 2));
    console.log(`\n# 2. sdcard_image/data/sp/mui-GrooveBoxRack.json — push into params[${desc.track}].params:`);
    console.log(JSON.stringify(muiGroup, null, 2));
    console.log(`\n# 3. sdcard_image/data/sp/mp-GrooveBoxRack.json — extend patches[0].params with:`);
    console.log(JSON.stringify(mpDefaults, null, 2));
}

console.log(`\n--- still TODO by hand in ctagSoundProcessorGrooveBoxRack.{hpp,cpp} ---`);
console.log(`\n# in the .hpp's member section, add:`);
console.log(`    ${desc.className} ch${chN}_${desc.id};`);
console.log(`\n# in Init(), inside the track-${desc.track} block (right after dri.cc_base = ${track.basecc}; dri.midi_channel = ${track.midichannel};):`);
console.log(`    dri.prefix = "${prefix}"; ch${chN}_${desc.id}.Init(&dri);`);
console.log(`\n# in Process(), inside the "if (ch${chN}.enabled) { ... }" block:`);
console.log(`    ch${chN}_${desc.id}.Process(idata);`);
console.log(`    if (ch${chN}_${desc.id}.enabled) mixRenderOutputMono(ch${chN}_${desc.id}.out, ch${chN}.level, ch${chN}.pan, ch${chN}.send1, ch${chN}.send2);`);
console.log(`\n# in setTrackMachine(), inside the trackIndex==${desc.track} branch:`);
console.log(`    ch${chN}_${desc.id}.enabled = (machineId == "${desc.id}");`);
console.log(`\n# in setTrackMachineByDeviceValue(), inside the case ${desc.track}: branch — pick "${desc.id}" for the value range you want it on (the WebUI sends 0 or 4095).`);
const ch0  = track.midichannel;             // 0-based
const midi = ch0 + 1;                       // 1-based for the developer-facing log
if (desc.type === 'drum') {
    console.log(`\n# in handleMidiNoteOn(), inside "else if (channel == ${ch0}) { ... }" — for note ${track.drumnote} (the track's drum note):`);
    console.log(`    if (ch${chN}_${desc.id}.enabled) { if (velocity > 0) ch${chN}_${desc.id}.trigger(); }`);
    console.log(`# (handleMidiNoteOff: drum voices don't need a note-off; nothing to add.)`);
} else {
    console.log(`\n# in handleMidiNoteOn(), inside "else if (channel == ${ch0}) { ... }":`);
    console.log(`    if (ch${chN}_${desc.id}.enabled) {`);
    console.log(`        if (velocity > 0) ch${chN}_${desc.id}.noteOn(note, velocity);`);
    console.log(`        else              ch${chN}_${desc.id}.noteOff(note, 0);`);
    console.log(`    }`);
    console.log(`# and symmetric block in handleMidiNoteOff(): ch${chN}_${desc.id}.noteOff(note, 0);`);
}
console.log(`\n(track ${desc.track} = WebUI CH${String(chN).padStart(2,'0')} "${track.name}", MIDI ch ${midi}` +
            (desc.type === 'drum' ? ` note ${track.drumnote}` : ' pitched') + `)`);
if (!inPlace) console.log(`\nWhen the C++ + JSON edits are in place, re-run with -i to write the files into the source tree.`);
console.log('done.');

// ------ helpers --------------------------------------------------------------------------
function die(msg) { console.error('error: ' + msg); process.exit(1); }
function backup(p) { try { fs.copyFileSync(p, p + '.bak'); } catch (e) { /* ok */ } }
function replaceSection(src, marker, body) {
    const m = '// ' + marker;
    const parts = src.split(m);
    if (parts.length !== 3) die(`template is missing the paired "${m}" markers`);
    return parts[0] + m + body + m + parts[2];
}

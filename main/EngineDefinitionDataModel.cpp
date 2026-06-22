/***************
TBD-16 — Macro/Preset System & PicoSeqRack

(c) 2025-2026 Per-Olov Jernberg (possan). https://possan.codes

Licensed under the GNU Lesser General Public License (LGPL 3.0).
https://www.gnu.org/licenses/lgpl-3.0.txt

dadamachines has a commercial license to use this code in the TBD-16 product.
Other commercial use requires a separate license agreement.

Provided "as is" without any express or implied warranties.

License and copyright details for specific submodules are included in their
respective component folders / files if different from this license.
***************/

#include "EngineDefinitionDataModel.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include <string.h>

using namespace CTAG::MACROPRESETS;
using namespace rapidjson;

#define CC(id_, name_, cc_, def_)  {id_, name_, EngineParameterType_CC,   (uint8_t)(cc_), (uint16_t)(def_)}
#define NR(id_, name_, cc_, def_)  {id_, name_, EngineParameterType_NRPM, (uint8_t)(cc_), (uint16_t)(def_)}

static constexpr EngineDef kEngines[] = {
    { "nodrum",  "Empty drum",         EngineType_Drum,  {} },
    { "nosynth", "Empty synth",        EngineType_Synth, {} },
    { "nofx",    "Empty FX",           EngineType_FX,    {} },

    { "fxdelay", "FX Delay", EngineType_FX, {
        CC("time",      "Time",         8,  16),
        CC("sync",      "Sync",         9,   0),
        CC("freeze",    "Freeze",      10,   0),
        CC("tapedig",   "Tapedig",     11,   0),
        CC("stwid",     "Stereo width",12,  32),
        CC("fx2send",   "FX2 Send",    13,   0),
        CC("feedback",  "Feedback",    14,  32),
        CC("base",      "Base",        15,   0),
        CC("width2",    "Width 2",     16,  32),
        CC("level",     "Level",       17,  64),
        CC("inputhp",   "Input HP",    18,   0),
    }},

    { "fxreverb", "FX Reverb", EngineType_FX, {
        CC("time",       "Time",       8,  64),
        CC("lowpass",    "Lowpass",    9,  96),
        CC("level",      "Level",     10,  64),
        CC("diffuse",    "Diffuse",   11,  89),
        CC("predelay",   "PreDly",    12,  14),
        CC("modulation", "ModRate",   13,  64),
        CC("inputgain",  "Drive",     14,  64),
        CC("tanklevel",  "TankLvl",   15, 127),
        CC("hp",         "HP",        16,   0),
    }},

    { "fxmaster", "FX Master", EngineType_FX, {
        CC("compthres",  "Thresh",    8, 100),
        CC("compratio",  "Ratio",     9,  32),
        CC("compatk",    "Attack",   10,   0),
        CC("comprel",    "Release",  11,  20),
        CC("complpf",    "LPF",      12,  48),
        CC("compgain",   "Gain",     13,   0),
        CC("compmix",    "Mix",      14,  64),
        CC("compdlylev", "Dly.Lev",  15,  64),
        CC("comprevlev", "Rev.Lev",  16,  64),
        CC("summute",    "Sum mute", 17,   0),
        CC("sumlev",     "Sum lev",  18,  64),
    }},

    { "db", "Synth Kick", EngineType_Drum, {
        NR("freq",      "Freq",      8, 2048),
        CC("tone",      "Tone",      9,   64),
        CC("decay",     "Decay",    10,   32),
        CC("dirt",      "Dirt",     11,    0),
        NR("fm-env",    "Fm Env",   12, 2048),
        NR("fm-decay",  "Fm Decay", 13, 1024),
        CC("fm-accent", "Fm Accent",14,    8),
    }},

    { "ab", "Analog Bass Drum", EngineType_Drum, {
        NR("freq",   "Freq",   8, 2048),
        CC("tone",   "Tone",   9,   32),
        CC("decay",  "Decay", 10,   64),
        CC("a-fm",   "A FM",  11,  256),
        NR("s-fm",   "S FM",  12,  256),
        CC("accent", "Accent",13,    2),
    }},

    { "fmb", "FM Kick", EngineType_Drum, {
        NR("f-b",       "FM",         8, 3072),
        NR("d-b",       "DB",         9, 6450),
        NR("f-m",       "FM",        10,  896),
        NR("d-m",       "DM",        11,    0),
        NR("b-m",       "BM",        12,  640),
        NR("a-f",       "AF",        13, 2048),
        NR("d-f",       "DF",        14, 4096),
        NR("i",         "I",         15,   80),
        CC("ratiomode", "Ratio Mode",16,    0),
        CC("envsync",   "Env Sync",  17,    0),
    }},

    { "ds", "Digital Snare", EngineType_Drum, {
        CC("freq",   "Freq",   8, 32),
        CC("tone",   "Tone",   9, 16),
        CC("decay",  "Decay", 10, 16),
        CC("noise",  "Noise", 11, 32),
        CC("accent", "Accent",12, 32),
    }},

    { "as", "Analog Snare", EngineType_Drum, {
        CC("freq",   "Freq",   8, 32),
        CC("tone",   "Tone",   9, 16),
        CC("decay",  "Decay", 10, 16),
        CC("noise",  "Noise", 11, 32),
        CC("accent", "Accent",12, 32),
    }},

    { "hh1", "Hi-Hat 1", EngineType_Drum, {
        CC("freq",   "Freq",   8, 32),
        CC("tone",   "Tone",   9, 16),
        CC("decay",  "Decay", 10, 16),
        CC("noise",  "Noise", 11, 32),
        CC("accent", "Accent",12, 32),
    }},

    { "hh2", "Hi-Hat 2", EngineType_Drum, {
        CC("freq",   "Freq",   8, 32),
        CC("tone",   "Tone",   9, 16),
        CC("decay",  "Decay", 10, 16),
        CC("noise",  "Noise", 11, 32),
        CC("accent", "Accent",12, 32),
    }},

    { "rs", "Rimshot", EngineType_Drum, {
        CC("freq",   "Freq",   8, 32),
        CC("tone",   "Tone",   9, 16),
        CC("decay",  "Decay", 10, 16),
        CC("noise",  "Noise", 11, 32),
        CC("accent", "Accent",12, 32),
    }},

    { "cl", "Clap", EngineType_Drum, {
        CC("freq",  "Freq",  8, 16),
        CC("tone",  "Tone",  9, 10),
        CC("decay", "Decay",10, 10),
        CC("scale", "Scale",11,  4),
        CC("trans", "Trans",12,  4),
    }},

    { "ro", "Rompler", EngineType_Synth, {
        CC("bank",     "Bank",    8,   0),
        CC("slice",    "Slice",   9,   0),
        CC("start",    "Start",  10,   0),
        CC("end",      "End",    11, 127),
        CC("cutoff",   "Cutoff", 12, 127),
        CC("reso",     "Reso",   13,   0),
        CC("type",     "Type",   14,   0),
        CC("bitcr",    "Bit.CR", 15,   0),
        CC("attack",   "Attack", 16,   0),
        CC("decay",    "Decay",  17,  64),
        NR("speed",    "Speed",  18, 12287),
        CC("pitch",    "Pitch",  19,  64),
        CC("loop",     "Loop",   20,   0),
        CC("pingpong", "PingPong",21,  0),
        CC("ppstart",  "PPStart",22,  64),
        CC("eg2fm",    "EG2FM",  23,   0),
        CC("tsmode",   "TSMode", 24,   0),
        CC("tsamt",    "TSAmt",  25,  64),
    }},

    { "td3", "TBD03", EngineType_Synth, {
        CC("shape",    "Bank",      8,   0),
        NR("p0",       "P0",        9,   0),
        NR("vca_d",    "VCA D",    10,   8),
        NR("vcf_d",    "VCF D",    11,   8),
        NR("cutoff",   "Cutoff",   12,  64),
        NR("reso",     "Reso",     13,   0),
        NR("envdec",   "EnvDec",   14,  16),
        CC("type",     "Type",     15,   0),
        NR("satur",    "Satur",    16,   0),
        NR("drive",    "Drive",    17,   0),
        CC("slide",    "Slide",    18,   0),
        CC("accent",   "Accent",   19,   0),
        NR("p1",       "P1",       20,   0),
        NR("p0amt",    "P0 Amt",   21,   0),
        NR("p1amt",    "P1 Amt",   22,   0),
        NR("acclev",   "Acc. Lev", 23,   0),
        CC("slidelev", "Slide Lev",24,   0),
        CC("synctrig", "Sync Trig",25,   0),
    }},

    { "mo", "Mono Synth", EngineType_Synth, {
        CC("shape",   "Bank",      8,  0),
        NR("p0",      "P0",        9,  0),
        NR("p1",      "P1",       10,  0),
        NR("waveshap","Waveshape", 11,  0),
        NR("p0a",     "P0 A",     12,  0),
        NR("p1a",     "P1 A",     13,  0),
        NR("fma",     "FM A",     14,  0),
        CC("attack",  "Attack",   16,  0),
        CC("decay",   "Decay",    17, 32),
        CC("loopenv", "Loop Env", 18,  0),
        CC("decim",   "SR Red",   19,  0),
        CC("envmode", "Env Mode", 20,  1),
    }},

    { "wtosc", "Wavetable Osc", EngineType_Synth, {
        CC("wavebank", "Bank",    8,   0),
        NR("wave",     "Wave",    9,   0),
        NR("tune",     "Tune",   10,   0),
        CC("type",     "Type",   11,   0),
        NR("cutoff",   "Cutoff", 12, 127),
        NR("reso",     "Reso",   13,   0),
        NR("gain",     "Gain",   14,  64),
        NR("attack",   "Attack", 15,   0),
        NR("decay",    "Decay",  16,  64),
        NR("sustain",  "Sustain",17,   0),
        NR("release",  "Release",18,  16),
        NR("e2wave",   "E2 Wave",19,   0),
        NR("e2fm",     "E2 FM",  20,   0),
        NR("e2filt",   "E2 Filt",21,   0),
        NR("speed",    "Speed",  22,  10),
        CC("sync",     "Sync",   23,   0),
        NR("l2wave",   "L2 Wave",24,   0),
        NR("l2am",     "L2 AM",  25,   0),
        NR("l2fm",     "L2 FM",  26,   0),
        NR("l2filt",   "L2 Filt",27,   0),
    }},

    { "pp", "Polypad", EngineType_Synth, {
        CC("chord",   "Chord",           8,   0),
        CC("inver",   "Inversion",        9,   0),
        NR("detune",  "Detune",          10,   8),
        NR("cutoff",  "Cutoff",          11,  80),
        NR("reso",    "Reso",            12,   0),
        CC("type",    "Type",            13,  32),
        CC("qscale",  "Q Scale",         14,   0),
        NR("attack",  "Attack",          15,   0),
        NR("decay",   "Decay",           16,   8),
        NR("sustain", "Sustain",         17,   8),
        NR("release", "Release",         18,  16),
        NR("l1spd",   "L1 Speed",        19,   0),
        NR("l1amt",   "L1 Amount",       20,   0),
        NR("l2spd",   "L2 Speed",        21,   0),
        NR("l2amt",   "L2 Amount",       22,   0),
        NR("efltamt", "E Filter Amount", 23,   0),
        CC("l2rand",  "L2 Random",       24, 127),
        CC("nnotes",  "Number of Notes", 25,  32),
    }},

    { "tbd", "TBDings", EngineType_Synth, {
        CC("model",  "Model",     8,    0),
        NR("freq",   "Freq",      9, 8192),
        NR("struc",  "Structure",10, 8192),
        NR("pos",    "Position", 11, 4900),
        NR("bright", "Bright",   12,10000),
        NR("damp",   "Damp",     13, 7000),
        CC("chord",  "Chord",    14,    0),
        CC("poly",   "Poly",     15,    1),
        NR("envsh",  "Env Shape",16, 4000),
        NR("vela",   "Vel Amt",  17, 8000),
        NR("air",    "Air",      18,    0),
        CC("pluck",  "Pluck",    19,    0),
        CC("mtype",  "Mod Type", 20,    0),
        NR("mdpth",  "Mod Depth",21,    0),
        NR("mrate",  "Mod Rate", 22, 8000),
        CC("msnap",  "Mod Snap", 23,    0),
    }},

    { "tbdait", "TBDaits", EngineType_Synth, {
        CC("model",  "Model",  8,     2),
        NR("freq",   "Freq",   9,  8192),
        NR("harm",   "Harm",  10,  8192),
        NR("timbre", "Timbre",11,  8192),
        NR("morph",  "Morph", 12,  8192),
        NR("decay",  "Decay", 13, 13000),
        CC("color",  "Color", 14,     1),
        NR("level",  "Level", 15, 11600),
        NR("fmod",   "FMod",  16,     0),
        NR("tmod",   "TMod",  17,     0),
        NR("mmod",   "MMod",  18,     0),
    }},

    { "extsynth", "External Synth", EngineType_Synth, {
        CC("chan", "MIDI Channel", 8, 0),
    }},

    { "extdrum", "External Drum", EngineType_Drum, {
        CC("chan", "MIDI Channel", 8,  0),
        CC("note", "MIDI Note",   9, 36),
    }},

    { "inp", "Audio Input", EngineType_Synth, {
        NR("in_gain",   "Gain",  8, 1024),
        CC("in_mono",   "Mono",  9,    0),
        NR("in_hp",     "HP",   10,    0),
        NR("in_drive",  "Drive",11,    0),
        CC("in_ftype",  "FType",12,    0),
        NR("in_fcutoff","FCut", 13, 4095),
        NR("in_freso",  "FReso",14,    0),
        NR("in_fenv",   "FEnv", 15,    0),
    }},
};

static constexpr TrackDef kTracks[] = {
    { "Kick",    TRACK_TYPE_DRUM,  9, 36,  0, {"nodrum","db","ab","ro","extdrum"} },
    { "Kick2",   TRACK_TYPE_DRUM,  9, 37, 40, {"nodrum","fmb","ro","extdrum"} },
    { "Snare",   TRACK_TYPE_DRUM,  9, 38, 80, {"nodrum","ds","as","do","extdrum"} },
    { "Hat",     TRACK_TYPE_DRUM, 10, 36,  0, {"nodrum","hh1","hh2","ro","extdrum"} },
    { "Rimshot", TRACK_TYPE_DRUM, 10, 37, 40, {"nodrum","rs","ro","extdrum"} },
    { "Clap",    TRACK_TYPE_DRUM, 10, 38, 80, {"nodrum","cl","ro","extdrum"} },
    { "Rompler", TRACK_TYPE_DRUM, 11, 36,  0, {"nodrum","ro","extdrum"} },
    { "Rompler", TRACK_TYPE_DRUM, 11, 37, 40, {"nodrum","ro","extdrum"} },
    { "Bass",    TRACK_TYPE_SYNTH, 0, -1,  0, {"nosynth","td3","ro","extsynth"} },
    { "Bass2",   TRACK_TYPE_SYNTH, 1, -1,  0, {"nosynth","td3","ro","extsynth"} },
    { "Lead",    TRACK_TYPE_SYNTH, 2, -1,  0, {"nosynth","mo","ro","extsynth"} },
    { "Lead2",   TRACK_TYPE_SYNTH, 3, -1,  0, {"nosynth","wtosc","mo","tbd","tbdait","ro","extsynth"} },
    { "Rompler", TRACK_TYPE_SYNTH, 4, -1,  0, {"nosynth","ro","extsynth"} },
    { "Rompler", TRACK_TYPE_SYNTH, 5, -1,  0, {"nosynth","ro","extsynth"} },
    { "Chords",  TRACK_TYPE_SYNTH, 6, -1,  0, {"nosynth","pp","tbd","ro","extsynth"} },
    { "Input",   TRACK_TYPE_SYNTH, 7, -1,  0, {"nosynth","imp"} },
    { "FX1",     TRACK_TYPE_FX,   12, -1,  0, {"nofx","fxdelay"} },
    { "FX2",     TRACK_TYPE_FX,   12, -1, 20, {"nofx","fxreverb"} },
    { "Master",  TRACK_TYPE_FX,   12, -1, 40, {"nofx","fxmaster"} },
};

#undef CC
#undef NR

// ---------------------------------------------------------------------------

int EngineDefinitionDataModel::GetNumberOfSynthDefinitions() {
    return sizeof(kEngines) / sizeof(kEngines[0]);
}

const EngineDef *EngineDefinitionDataModel::GetSynthDefinition(const std::string &id) {
    for (const auto &e : kEngines) {
        if (strcmp(e.id, id.c_str()) == 0) return &e;
    }
    return nullptr;
}

const TrackDef *EngineDefinitionDataModel::GetTrackDefinition(int index) {
    constexpr int n = sizeof(kTracks) / sizeof(kTracks[0]);
    if (index >= 0 && index < n) return &kTracks[index];
    return nullptr;
}

bool EngineDefinitionDataModel::SerializeIntoJSON(rapidjson::Document &doc) {
    Value machinesarray(kArrayType);
    doc.AddMember("machines", machinesarray, doc.GetAllocator());
    for (const auto &def : kEngines) {
        Value machineObj(kObjectType);
        machineObj.AddMember("id",   Value(def.id,   doc.GetAllocator()), doc.GetAllocator());
        machineObj.AddMember("name", Value(def.name, doc.GetAllocator()), doc.GetAllocator());

        Value paramsarray(kArrayType);
        machineObj.AddMember("parameters", paramsarray, doc.GetAllocator());
        for (int i = 0; i < MaxEngineDefinitionParameters; i++) {
            if (def.params[i].id == nullptr) break;
            const char *typeStr =
                (def.params[i].type == EngineParameterType_CC)   ? "cc"   :
                (def.params[i].type == EngineParameterType_NRPM) ? "nrpm" : "none";
            Value param(kObjectType);
            param.AddMember("id",   Value(def.params[i].id,   doc.GetAllocator()), doc.GetAllocator());
            param.AddMember("name", Value(def.params[i].name, doc.GetAllocator()), doc.GetAllocator());
            param.AddMember("type", Value(typeStr,             doc.GetAllocator()), doc.GetAllocator());
            param.AddMember("ctrl", def.params[i].relCC,                           doc.GetAllocator());
            param.AddMember("def",  def.params[i].defaultValue,                    doc.GetAllocator());
            machineObj["parameters"].PushBack(param, doc.GetAllocator());
        }
        doc["machines"].PushBack(machineObj, doc.GetAllocator());
    }

    Value tracksarray(kArrayType);
    doc.AddMember("tracks", tracksarray, doc.GetAllocator());
    int trackIndex = 0;
    for (const auto &def : kTracks) {
        Value trackObj(kObjectType);
        trackObj.AddMember("index",       trackIndex,                            doc.GetAllocator());
        trackObj.AddMember("name",        Value(def.name, doc.GetAllocator()),   doc.GetAllocator());
        trackObj.AddMember("midichannel", def.midiChannel,                      doc.GetAllocator());
        trackObj.AddMember("drumnote",    def.drumNote,                         doc.GetAllocator());
        trackObj.AddMember("basecc",      def.baseCC,                           doc.GetAllocator());

        Value machinesarray(kArrayType);
        trackObj.AddMember("machines", machinesarray, doc.GetAllocator());
        for (int k = 0; k < MaxTrackDefinitionEngineIds; k++) {
            if (def.engines[k] == nullptr) break;
            trackObj["machines"].PushBack(Value(def.engines[k], doc.GetAllocator()), doc.GetAllocator());
        }
        doc["tracks"].PushBack(trackObj, doc.GetAllocator());
        trackIndex++;
    }
    return true;
}

#pragma GCC diagnostic ignored "-Wstringop-truncation"
void EngineDefinitionDataModel::WriteListResponse(struct GetEngineDefinitionIdListResponse *r) {
    r->numEngines = 0;
    for (const auto &e : kEngines) {
        strncpy(r->engineIds[r->numEngines], e.id, MaxEngineId);
        r->numEngines++;
        if (r->numEngines >= MaxEngines) break;
    }
}

void EngineDefinitionDataModel::WriteEngineDefinitionPageResponse(const struct GetEngineDefinitionsPageRequest *request, struct GetEngineDefinitionsPageResponse *response) {
    constexpr int numEngines = sizeof(kEngines) / sizeof(kEngines[0]);
    response->offset = request->offset;
    response->totalEngineDefinitions = numEngines;
    response->returnedEngineDefinitions = 0;

    for (int k = 0; k < MaxEngineDefinitionsPerPage; k++) {
        int i = request->offset + k;
        if (i >= numEngines) break;

        const EngineDef &src = kEngines[i];
        SharedEngineDefinition &dst = response->engineDefinitions[k];
        memset(&dst, 0, sizeof(dst));

        strlcpy(dst.idStr, src.id,   sizeof(dst.idStr));
        strlcpy(dst.name,  src.name, sizeof(dst.name));
        dst.type = src.type;

        for (int p = 0; p < MaxEngineDefinitionParameters; p++) {
            if (src.params[p].id == nullptr) break;
            strlcpy(dst.parameters[p].id,   src.params[p].id,   sizeof(dst.parameters[p].id));
            strlcpy(dst.parameters[p].name, src.params[p].name, sizeof(dst.parameters[p].name));
            dst.parameters[p].type         = src.params[p].type;
            dst.parameters[p].relCC        = src.params[p].relCC;
            dst.parameters[p].defaultValue = src.params[p].defaultValue;
        }
        response->returnedEngineDefinitions++;
    }
}

static EngineDefinitionDataModel g_enginedef_instance;

EngineDefinitionDataModel *EngineDefinitionDataModel::instance() {
    return &g_enginedef_instance;
}

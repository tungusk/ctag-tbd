// Smoke test: construct + Init + LoadPreset + Process(once) for one (or every)
// sound processor — catches "loading plugin X crashes the simulator" bugs
// without needing an audio device.  Build: target `load-test` (see CMakeLists).
// Usage:  ./load-test [PluginId]      (default: GrooveBoxRack)
//         ./load-test --all           (every registered sound processor)
//         ./load-test --machine <id>  (load GrooveBoxRack, isolate one voice,
//                                      fire a note, report dry & FX-bus peak)
//
// The --machine mode is the fast iteration loop when you're tuning a single rack
// voice's DSP.  It uses a small id→(track, channel, drumnote) table; when adding
// a new voice via rackgen.js, append one line there to make it testable.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "ctagSoundProcessor.hpp"
#include "ctagSoundProcessorFactory.hpp"
#include "ctagSPAllocator.hpp"
#include "ctagSoundProcessorGrooveBoxRack.hpp"
#include "helpers/ctagSampleRom.hpp"
extern "C" {
#include "esp_spi_flash.h"
}

using namespace CTAG::SP;

// Machine-id → (track, channel, note) for --machine mode.  For drum voices the
// note is the track's fixed drum note (36/37/38); for synth voices note=60 just
// means "play middle-C-ish".  The track-15 audio-input voice "in" has no MIDI
// routing and isn't testable this way (omitted).
struct VoiceLookup { const char* id; int track; int channel; int note; };
static const VoiceLookup kVoices[] = {
    { "db",    0,  9, 36 }, { "ab",    0,  9, 36 }, { "ro_t0", 0,  9, 36 }, // track 0 — id "ro" repeats below; we disambiguate by track
    { "fmb",   1,  9, 37 },                          { "ro_t1", 1,  9, 37 },
    { "ds",    2,  9, 38 }, { "as",    2,  9, 38 }, { "ro_t2", 2,  9, 38 },
    { "hh1",   3, 10, 36 }, { "hh2",   3, 10, 36 }, { "ro_t3", 3, 10, 36 },
    { "rs",    4, 10, 37 },                          { "ro_t4", 4, 10, 37 },
    { "cl",    5, 10, 38 },                          { "ro_t5", 5, 10, 38 },
    { "ro_t6", 6, 11, 36 },
    { "ro_t7", 7, 11, 37 },
    { "td3_t8",  8, 0, 60 },                         { "ro_t8",  8, 0, 60 },
    { "td3_t9",  9, 1, 60 },                         { "ro_t9",  9, 1, 60 },
    { "mo_t10", 10, 2, 60 },                         { "ro_t10", 10, 2, 60 },
    { "wtosc",  11, 3, 60 }, { "mo_t11", 11, 3, 60 }, { "ro_t11", 11, 3, 60 },
    { "ro_t12", 12, 4, 60 },
    { "ro_t13", 13, 5, 60 },
    { "pp",    14, 6, 60 },                          { "ro_t14", 14, 6, 60 },
    { nullptr, 0, 0, 0 }
};
// "ro" is ambiguous (a rompler sits on every track); allow the bare id by
// defaulting to track 0.  For the per-track samplers, use "ro_t<N>" suffixes
// (e.g. ./load-test --machine ro_t7 for the CH08 sampler-only track).
static const VoiceLookup* findVoice(const std::string& id) {
    for (const VoiceLookup* v = kVoices; v->id; v++) {
        if (id == v->id) return v;
    }
    // bare id without "_tN" suffix → take the FIRST match (drum/synth voices),
    // or default to the t0 entry for the rompler.
    for (const VoiceLookup* v = kVoices; v->id; v++) {
        // match if the table id starts with the requested id followed by "_t" or ends here
        std::string vid(v->id);
        if (vid == id) return v;
        if (vid.find(id + "_t") == 0) return v;     // e.g. id="td3" matches "td3_t8"
    }
    return nullptr;
}

// Strip the "_tN" suffix so we know what to pass to setTrackMachine (the rack only
// knows the bare id — the suffix is just disambiguation for this table).
static std::string strippedId(const std::string& tableId) {
    auto pos = tableId.find("_t");
    return (pos == std::string::npos) ? tableId : tableId.substr(0, pos);
}

// the simulator defines this in SimSPManager.cpp; the load-test doesn't link that
namespace CTAG { namespace RESOURCES { std::string sdcardRoot {"../../sdcard_image"}; } }

static float blockPeak(const float* buf, int frames) {
    float p = 0.f;
    for (int i = 0; i < frames * 2; i++) { float a = buf[i] < 0 ? -buf[i] : buf[i]; if (a > p) p = a; }
    return p;
}

static int test_one(const std::string& id) {
    printf("=== load-test: %s ===\n", id.c_str()); fflush(stdout);
    // most rack/sample plugins are stereo → allocate STEREO (matches SimSPManager)
    auto aType = ctagSPAllocator::AllocationType::STEREO;
    printf("  Create()...\n"); fflush(stdout);
    ctagSoundProcessor* sp = ctagSoundProcessorFactory::Create(id, aType);
    if (sp == nullptr) { printf("  -> Create returned nullptr (not stereo? unknown id?)\n"); return 0; }
    printf("  LoadPreset(0)...\n"); fflush(stdout);
    sp->LoadPreset(0);
    printf("  Process()...\n"); fflush(stdout);
    float buf[32 * 2];
    float cv[4] = {0,0,0,0};
    uint8_t trig[2] = {0,0};
    ProcessData pd;
    pd.buf = buf; pd.cv = cv; pd.trig = trig;
    pd.sequencer_tempo = 12000; pd.sequencer_quantum = 4;
    pd.midi_bytes_length = 0; memset(pd.midi_bytes, 0, sizeof(pd.midi_bytes));
    for (int i = 0; i < 4; i++) { memset(buf, 0, sizeof(buf)); sp->Process(pd); }

    // For GrooveBoxRack: inject a few MIDI notes (kick=36, snare=38 on ch 10; a melodic
    // note on ch 1) and check that something actually comes out — exercises the whole
    // parseIncomingMidiMessages → handleMidiNoteOn → voice → mix chain headlessly.
    if (id == "GrooveBoxRack") {
        auto fireAndPeak = [&](int warmupBlocks) {
            const uint8_t notes[][3] = {{0x99,36,110},{0x99,38,100},{0x90,48,100}}; // ch10 kick, ch10 snare, ch1 note
            float pL = 0.f, pR = 0.f;
            for (auto& m : notes) { memcpy(pd.midi_bytes, m, 3); pd.midi_bytes_length = 3; memset(buf,0,sizeof(buf)); sp->Process(pd); pd.midi_bytes_length = 0; }
            for (int i = 0; i < warmupBlocks; i++) {
                memset(buf, 0, sizeof(buf)); sp->Process(pd);
                for (int s = 0; s < 32; s++) { float l = std::fabs(buf[s*2]), r = std::fabs(buf[s*2+1]); if (l > pL) pL = l; if (r > pR) pR = r; }
            }
            return std::make_pair(pL, pR);
        };

        auto [peakL, peakR] = fireAndPeak(200);
        printf("  GrooveBoxRack dry: peak L=%.4f R=%.4f  -> %s, %s\n", peakL, peakR,
               (peakL > 1e-4f && peakR > 1e-4f) ? "AUDIBLE" : "SILENT (bug?)",
               (peakL > 1e-4f && peakR > 1e-4f && std::fabs(peakL - peakR) < 0.5f * std::max(peakL, peakR)) ? "STEREO" : "not stereo?!");

        // Exercise the FX bus: turn the kick's FX Send 2 (reverb) up and check the wet path
        // actually adds signal. With the sim's loadPresetInternal() override fx2_amount = 2000
        // and ch1_fx2 = 4000 here, the reverb tail should be clearly audible after the kick decays.
        sp->SetParamValue("ch1_fx2", "current", 4000);
        // process a few blocks for the level change to settle, then fire a kick and look at the tail
        for (int i = 0; i < 16; i++) { memset(buf, 0, sizeof(buf)); sp->Process(pd); }
        const uint8_t kick[3] = {0x99, 36, 110};
        memcpy(pd.midi_bytes, kick, 3); pd.midi_bytes_length = 3;
        memset(buf, 0, sizeof(buf)); sp->Process(pd);
        pd.midi_bytes_length = 0;
        // run ~4096 blocks (~3 s of audio) and measure the tail starting after the dry kick has decayed (~200 blocks in)
        float tailPeak = 0.f;
        for (int i = 0; i < 4096; i++) {
            memset(buf, 0, sizeof(buf)); sp->Process(pd);
            if (i < 200) continue;
            for (int s = 0; s < 32; s++) { float v = std::fabs(buf[s*2]); if (v > tailPeak) tailPeak = v; }
        }
        printf("  GrooveBoxRack wet (ch1_fx2=4000): reverb tail peak = %.4f  -> %s\n",
               tailPeak, (tailPeak > 1e-3f) ? "FX BUS WORKS" : "FX BUS SILENT (bug)");

        // Exercise the Rompler/sample path: track 6 (CH07 Rompler, MIDI ch 12 note 36)
        // defaults to the "ro" machine and should play from the bundled sample-rom.
        sp->SetParamValue("ch1_fx2", "current", 0);             // turn the reverb send back off
        for (int i = 0; i < 16; i++) { memset(buf, 0, sizeof(buf)); sp->Process(pd); }
        const uint8_t smpHit[3] = {0x9B, 36, 110};              // ch 12 (0x9B = 0x90|0x0B), note 36
        memcpy(pd.midi_bytes, smpHit, 3); pd.midi_bytes_length = 3;
        memset(buf, 0, sizeof(buf)); sp->Process(pd);
        pd.midi_bytes_length = 0;
        float smpPeak = 0.f;
        for (int i = 0; i < 200; i++) {
            memset(buf, 0, sizeof(buf)); sp->Process(pd);
            for (int s = 0; s < 32; s++) { float v = std::fabs(buf[s*2]); if (v > smpPeak) smpPeak = v; }
        }
        printf("  GrooveBoxRack sampler (ch7_smp, MIDI 12 / note 36): peak = %.4f  -> %s\n",
               smpPeak, (smpPeak > 1e-4f) ? "SAMPLES PLAY" : "SILENT (sample-rom missing or rompler default-bank empty)");
    }
    delete sp;
    printf("  OK: %s constructed, init'd, preset-loaded and processed without crashing.\n", id.c_str());
    return 0;
}

// Single-voice isolation mode (`load-test --machine <id>`):
//   1. Construct GrooveBoxRack, load preset 0 (every track gets its first machine).
//   2. setTrackMachine(t, id, 1.0) for the requested voice's track — disables every
//      other voice on that track, enables only the one we're testing.
//   3. Inject one note (drum: trigger note on the track's drum channel; synth: note
//      60 on the track's synth channel) and run ~64 audio blocks.
//   4. Report dry peak L/R.  Optionally crank ch<N>_fx2 to verify the FX bus path.
//
// This is the tightest "did my DSP change something?" loop while you're tuning a
// new rack voice — much faster than booting the simulator and clicking through
// the WebUI.  Returns 0 if the voice produced audible output, 1 if it was silent
// (probably a bug worth investigating).
static int test_machine(const std::string& argId) {
    const VoiceLookup* v = findVoice(argId);
    if (!v) {
        printf("=== load-test --machine: unknown voice id \"%s\" ===\n", argId.c_str());
        printf("Try one of:");
        for (const VoiceLookup* p = kVoices; p->id; p++) printf(" %s", p->id);
        printf("\n(use 'foo_tN' to disambiguate per-track romplers — see the table at the\n"
               " top of this file.)\n");
        return 2;
    }
    const std::string id = strippedId(argId);
    printf("=== load-test --machine %s (id=\"%s\" on track %d, ch %d, note %d) ===\n",
           argId.c_str(), id.c_str(), v->track, v->channel, v->note);

    auto* sp = ctagSoundProcessorFactory::Create("GrooveBoxRack",
                                                  ctagSPAllocator::AllocationType::STEREO);
    if (!sp) { printf("  -> Create returned nullptr — sample-rom missing?\n"); return 2; }
    sp->LoadPreset(0);
    auto* rack = static_cast<ctagSoundProcessorGrooveBoxRack*>(sp);
    rack->setTrackMachine(static_cast<uint8_t>(v->track), id, 1.0f);

    float buf[32 * 2];
    float cv[4] = {0, 0, 0, 0};
    uint8_t trig[2] = {0, 0};
    ProcessData pd;
    pd.buf = buf; pd.cv = cv; pd.trig = trig;
    pd.sequencer_tempo = 12000; pd.sequencer_quantum = 4;
    pd.midi_bytes_length = 0; std::memset(pd.midi_bytes, 0, sizeof(pd.midi_bytes));

    // Settle: a few empty blocks to clear any startup transient.
    for (int i = 0; i < 4; i++) { std::memset(buf, 0, sizeof(buf)); sp->Process(pd); }

    // Fire one note event directly into the rack (bypass the MIDI parser).
    rack->handleMidiNoteOn(static_cast<uint8_t>(v->channel), static_cast<uint8_t>(v->note), 110);

    float pL = 0.f, pR = 0.f;
    for (int i = 0; i < 200; i++) {
        std::memset(buf, 0, sizeof(buf)); sp->Process(pd);
        for (int s = 0; s < 32; s++) {
            float l = std::fabs(buf[s * 2]), r = std::fabs(buf[s * 2 + 1]);
            if (l > pL) pL = l;
            if (r > pR) pR = r;
        }
    }
    printf("  dry peak: L=%.4f R=%.4f  -> %s\n", pL, pR,
           (pL > 1e-4f || pR > 1e-4f) ? "AUDIBLE" : "SILENT (bug? voice not enabled? note routed wrong?)");

    // Bonus: crank this track's FX Send 2 and check the reverb tail.
    char fx2Id[32]; std::snprintf(fx2Id, sizeof(fx2Id), "ch%d_fx2", v->track + 1);
    sp->SetParamValue(fx2Id, "current", 4000);
    for (int i = 0; i < 16; i++) { std::memset(buf, 0, sizeof(buf)); sp->Process(pd); }
    rack->handleMidiNoteOn(static_cast<uint8_t>(v->channel), static_cast<uint8_t>(v->note), 110);
    float tail = 0.f;
    for (int i = 0; i < 4096; i++) {
        std::memset(buf, 0, sizeof(buf)); sp->Process(pd);
        if (i < 200) continue;       // ignore dry transient
        for (int s = 0; s < 32; s++) { float v2 = std::fabs(buf[s * 2]); if (v2 > tail) tail = v2; }
    }
    printf("  FX2-bus tail peak: %.4f  -> %s\n", tail,
           (tail > 1e-3f) ? "FX BUS REACHES THIS VOICE" : "FX BUS SILENT FOR THIS VOICE");

    delete sp;
    return (pL > 1e-4f || pR > 1e-4f) ? 0 : 1;
}

int main(int argc, char** argv) {
    // give ctagSampleRom data (rompler/wavetable channels need it) — robust if missing
    spi_flash_emu_init("../../sample_rom/sample-rom.tbd");
    CTAG::SP::HELPERS::ctagSampleRom::RefreshDataStructure();

    // Argument parsing — three modes (default / --all / --machine <id>).
    int rc = 0;
    if (argc >= 3 && std::strcmp(argv[1], "--machine") == 0) {
        rc = test_machine(argv[2]);
    } else {
        std::string which = (argc > 1) ? argv[1] : "GrooveBoxRack";
        if (which == "--all") {
            const char* ids[] = { "TBD03","MacOsc","Subbotnik","Karpuskl","SubSynth","Talkbox",
                                   "GrooveBoxRack","DrumRack","Rompler","WTOsc","PolyPad","Void", nullptr };
            for (int i = 0; ids[i]; i++) test_one(ids[i]);
        } else {
            rc = test_one(which);
        }
    }
    spi_flash_emu_release();
    printf("done.\n");
    return rc;
}

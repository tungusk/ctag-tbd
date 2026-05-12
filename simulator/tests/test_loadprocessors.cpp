// Smoke test: construct + Init + LoadPreset + Process(once) for one (or every)
// sound processor — catches "loading plugin X crashes the simulator" bugs
// without needing an audio device.  Build: target `load-test` (see CMakeLists).
// Usage:  ./load-test [PluginId]   (default: GrooveBoxRack)
//         ./load-test --all        (every registered sound processor)

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "ctagSoundProcessor.hpp"
#include "ctagSoundProcessorFactory.hpp"
#include "ctagSPAllocator.hpp"
#include "helpers/ctagSampleRom.hpp"
extern "C" {
#include "esp_spi_flash.h"
}

using namespace CTAG::SP;

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
        const uint8_t notes[][3] = {{0x99,36,110},{0x99,38,100},{0x90,48,100}}; // ch10 kick, ch10 snare, ch1 note
        float peakL = 0.f, peakR = 0.f;
        for (auto& m : notes) { memcpy(pd.midi_bytes, m, 3); pd.midi_bytes_length = 3; memset(buf,0,sizeof(buf)); sp->Process(pd); pd.midi_bytes_length = 0; }
        for (int i = 0; i < 200; i++) {
            memset(buf, 0, sizeof(buf)); sp->Process(pd);
            for (int s = 0; s < 32; s++) { float l = std::fabs(buf[s*2]), r = std::fabs(buf[s*2+1]); if (l > peakL) peakL = l; if (r > peakR) peakR = r; }
        }
        printf("  GrooveBoxRack: output peak L=%.4f R=%.4f  -> %s, %s\n", peakL, peakR,
               (peakL > 1e-4f && peakR > 1e-4f) ? "AUDIBLE" : "SILENT (bug?)",
               (peakL > 1e-4f && peakR > 1e-4f && std::fabs(peakL - peakR) < 0.5f * std::max(peakL, peakR)) ? "STEREO" : "not stereo?!");
    }
    delete sp;
    printf("  OK: %s constructed, init'd, preset-loaded and processed without crashing.\n", id.c_str());
    return 0;
}

int main(int argc, char** argv) {
    // give ctagSampleRom data (rompler/wavetable channels need it) — robust if missing
    spi_flash_emu_init("../../sample_rom/sample-rom.tbd");
    CTAG::SP::HELPERS::ctagSampleRom::RefreshDataStructure();

    std::string which = (argc > 1) ? argv[1] : "GrooveBoxRack";
    if (which == "--all") {
        // (best-effort list; extend as needed)
        const char* ids[] = { "TBD03","MacOsc","Subbotnik","Karpuskl","SubSynth","Talkbox",
                               "GrooveBoxRack","DrumRack","Rompler","WTOsc","PolyPad","Void", nullptr };
        for (int i = 0; ids[i]; i++) test_one(ids[i]);
    } else {
        test_one(which);
    }
    spi_flash_emu_release();
    printf("done.\n");
    return 0;
}

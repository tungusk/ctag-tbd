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

static int test_one(const std::string& id) {
    printf("=== load-test: %s ===\n", id.c_str()); fflush(stdout);
    // most rack/sample plugins are stereo → allocate STEREO (matches SimSPManager)
    auto aType = ctagSPAllocator::AllocationType::STEREO;
    printf("  Create()...\n"); fflush(stdout);
    ctagSoundProcessor* sp = ctagSoundProcessorFactory::Create(id, aType);
    if (sp == nullptr) { printf("  -> Create returned nullptr (not stereo? unknown id?)\n"); return 0; }
    printf("  LoadPreset(0)...\n"); fflush(stdout);
    sp->LoadPreset(0);
    printf("  Process() x4...\n"); fflush(stdout);
    float buf[32 * 2]; memset(buf, 0, sizeof(buf));
    float cv[4] = {0,0,0,0};
    uint8_t trig[2] = {0,0};
    ProcessData pd; pd.buf = buf; pd.cv = cv; pd.trig = trig;
    for (int i = 0; i < 4; i++) sp->Process(pd);
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

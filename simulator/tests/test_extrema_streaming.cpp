#include "test_extrema_streaming.hpp"

#include "synthesis/ExtremaStreamingStretch.hpp"

#include <array>
#include <cmath>
#include <cstdio>

namespace CTAG::TESTS {

namespace {
uint32_t alignmentCalls = 0;

float alignForward(void *, float, float referencePosition, uint32_t) {
    ++alignmentCalls;
    return referencePosition + 8.f;
}
}

int test_extrema_streaming::DoTest() {
    std::printf("test_extrema_streaming\n");
    int failures = 0;
    alignas(float) std::array<uint8_t, SYNTHESIS::ExtremaStreamingStretch::kWorkspaceBytes> workspace {};
    SYNTHESIS::ExtremaStreamingStretch stretch;
    stretch.Init(workspace.data(), 44100.f);

    std::array<float, 512> input {};
    std::array<float, 32> output {};
    uint32_t sourceIndex = 0;
    for (int block = 0; block < 100; ++block) {
        for (uint32_t i = 0; i < input.size(); ++i, ++sourceIndex) {
            input[i] = std::sin(static_cast<float>(sourceIndex) * 0.071f);
        }
        if (!stretch.Analyze(input.data(), input.size(), false)) {
            std::printf("  FAIL: analyzer ring overflowed before consumption\n");
            ++failures;
            break;
        }
        for (int n = 0; n < 8; ++n) {
            if (!stretch.Process(output.data(), output.size(), 0.5f, 1.f, 0.5f)) {
                std::printf("  FAIL: processing starved with available lookahead\n");
                ++failures;
                break;
            }
            for (float sample : output) {
                if (!std::isfinite(sample) || std::fabs(sample) > 1.1f) {
                    std::printf("  FAIL: invalid output sample %.6f\n", sample);
                    ++failures;
                    break;
                }
            }
        }
    }

    if (std::fabs(stretch.ReferencePosition() - 12800.f) > 1.f) {
        std::printf("  FAIL: reference rate mismatch %.3f\n", stretch.ReferencePosition());
        ++failures;
    }
    if (stretch.KeyCount() > SYNTHESIS::ExtremaStreamingStretch::kKeyCapacity) {
        std::printf("  FAIL: key capacity exceeded\n");
        ++failures;
    }

    if (std::fabs(stretch.WindowSamples(0.f) - 220.5f) > 1.f ||
        std::fabs(stretch.WindowSamples(1.f) - 8820.f) > 1.f) {
        std::printf("  FAIL: window control is not mapped to 5..200 ms\n");
        ++failures;
    }

    stretch.Reset();
    std::array<float, 32> peakInput {};
    peakInput[8] = 1.f;
    if (!stretch.Analyze(peakInput.data(), peakInput.size(), true) ||
        !stretch.Process(output.data(), output.size(), 1.f, 1.f, 0.5f)) {
        std::printf("  FAIL: could not reconstruct isolated peak\n");
        ++failures;
    } else {
        float reconstructedPeak = 0.f;
        for (float sample : output) reconstructedPeak = std::max(reconstructedPeak, sample);
        if (reconstructedPeak < 0.95f) {
            std::printf("  FAIL: source peak amplitude was smoothed to %.6f\n", reconstructedPeak);
            ++failures;
        }
    }

    stretch.Reset();
    sourceIndex = 0;
    for (int block = 0; block < 16; ++block) {
        for (uint32_t i = 0; i < input.size(); ++i, ++sourceIndex) {
            input[i] = std::sin(static_cast<float>(sourceIndex) * 0.071f);
        }
        if (!stretch.Analyze(input.data(), input.size(), false)) {
            std::printf("  FAIL: could not prepare active-pointer lookahead test\n");
            ++failures;
            break;
        }
    }
    for (int n = 0; n < 32; ++n) {
        if (!stretch.Process(output.data(), output.size(), 0.25f, 1.f, 1.f)) {
            std::printf("  FAIL: active-pointer lookahead setup starved\n");
            ++failures;
            break;
        }
    }
    const float referenceOnlyRequirement =
        stretch.ReferencePosition() + 512.f + static_cast<float>(output.size());
    const float activePointerRequirement =
        stretch.RequiredLookaheadPosition(output.size(), 0.25f, 1.f, 512);
    if (activePointerRequirement <= referenceOnlyRequirement + 128.f) {
        std::printf("  FAIL: lookahead does not account for playhead drift\n");
        ++failures;
    }

    alignmentCalls = 0;
    for (int n = 0; n < 32; ++n) {
        if (!stretch.Process(
                output.data(), output.size(), 0.25f, 1.f, 0.f, alignForward, nullptr)) {
            std::printf("  FAIL: aligned splice processing starved\n");
            ++failures;
            break;
        }
    }
    if (alignmentCalls == 0) {
        std::printf("  FAIL: splice aligner was not invoked\n");
        ++failures;
    }

    stretch.Reset();
    sourceIndex = 0;
    for (uint32_t i = 0; i < input.size(); ++i, ++sourceIndex) {
        input[i] = std::sin(static_cast<float>(sourceIndex) * 0.071f);
    }
    if (!stretch.Analyze(input.data(), input.size(), true)) {
        std::printf("  FAIL: could not prepare fast-playback end test\n");
        ++failures;
    } else {
        bool referenceReachedBeforeAudible = false;
        bool audibleCompleted = false;
        for (int n = 0; n < 64; ++n) {
            if (!stretch.Process(output.data(), output.size(), 2.f, 1.f, 0.f)) {
                std::printf("  FAIL: fast playback starved before audible end\n");
                ++failures;
                break;
            }
            if (stretch.ReferencePosition() >= 511.f &&
                !stretch.AudiblePlaybackComplete(511.f)) {
                referenceReachedBeforeAudible = true;
            }
            if (stretch.AudiblePlaybackComplete(511.f)) {
                audibleCompleted = true;
                break;
            }
        }
        if (!referenceReachedBeforeAudible || !audibleCompleted) {
            std::printf("  FAIL: fast playback did not preserve final audible splice\n");
            ++failures;
        }
    }

    stretch.Reset();
    for (uint32_t i = 0; i < input.size(); ++i) {
        input[i] = (i & 1U) == 0U ? -1.f : 1.f;
    }
    while (stretch.AvailableKeyCapacity() > input.size() + 1) {
        if (!stretch.Analyze(input.data(), input.size(), false)) {
            std::printf("  FAIL: dense extrema overflowed despite reserved capacity\n");
            ++failures;
            break;
        }
    }
    const uint32_t safeDenseCount = stretch.AvailableKeyCapacity() - 1;
    if (!stretch.Analyze(input.data(), safeDenseCount, false)) {
        std::printf("  FAIL: capacity-aware dense extrema feed overflowed\n");
        ++failures;
    }

    stretch.Reset();
    sourceIndex = 0;
    bool rebaseSetupOk = true;
    for (int block = 0; block < 48 && rebaseSetupOk; ++block) {
        for (uint32_t i = 0; i < input.size(); ++i, ++sourceIndex) {
            input[i] = std::sin(static_cast<float>(sourceIndex) * 0.071f);
        }
        rebaseSetupOk = stretch.Analyze(input.data(), input.size(), false);
        for (int n = 0; n < 16 && rebaseSetupOk; ++n) {
            rebaseSetupOk = stretch.Process(output.data(), output.size(), 1.f, 1.f, 0.5f);
        }
    }
    const float referenceBeforeRebase = stretch.ReferencePosition();
    const uint32_t rebaseOffset = stretch.Rebase(8192);
    if (!rebaseSetupOk || rebaseOffset == 0 ||
        std::fabs(stretch.ReferencePosition() -
                  (referenceBeforeRebase - static_cast<float>(rebaseOffset))) > 0.01f) {
        std::printf("  FAIL: active stream could not be rebased consistently\n");
        ++failures;
    } else {
        for (uint32_t i = 0; i < input.size(); ++i, ++sourceIndex) {
            input[i] = std::sin(static_cast<float>(sourceIndex) * 0.071f);
        }
        if (!stretch.Analyze(input.data(), input.size(), false) ||
            !stretch.Process(output.data(), output.size(), 1.f, 1.f, 0.5f)) {
            std::printf("  FAIL: rebased stream could not continue processing\n");
            ++failures;
        }
    }

    stretch.Reset();
    if (stretch.KeyCount() != 0 || stretch.ReferencePosition() != 0.f) {
        std::printf("  FAIL: reset did not clear state\n");
        ++failures;
    }

    std::printf("test_extrema_streaming - %d failures\n\n", failures);
    return failures;
}

} // namespace CTAG::TESTS

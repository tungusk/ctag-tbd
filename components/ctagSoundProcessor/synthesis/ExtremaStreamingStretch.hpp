/***************
CTAG TBD >>to be determined<< is an open source eurorack synthesizer module.

A project conceived within the Creative Technologies Arbeitsgruppe of
Kiel University of Applied Sciences: https://www.creative-technologies.de

(c) 2020-2026 by Robert Manzke. All rights reserved.

The CTAG TBD software is licensed under the GNU General Public License
(GPL 3.0), available here: https://www.gnu.org/licenses/gpl-3.0.txt

The CTAG TBD hardware design is released under the Creative Commons
Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0).
Details here: https://creativecommons.org/licenses/by-nc-sa/4.0/

CTAG TBD is provided "as is" without any express or implied warranties.

License and copyright details for specific submodules are included in their
respective component folders / files if different from this license.
***************/

#pragma once

#include <cstddef>
#include <cstdint>

namespace CTAG::SYNTHESIS {

class ExtremaStreamingStretch {
public:
    using SpliceAligner = float (*)(void *context, float playPosition,
                                    float referencePosition, uint32_t searchRadius);

    struct Keyframe {
        float index;
        float value;
    };

    static constexpr uint32_t kKeyCapacity = 1024;
    static constexpr size_t kWorkspaceBytes = kKeyCapacity * sizeof(Keyframe);

    void Init(void *workspace, float sampleRate);
    void Reset();
    bool Analyze(const float *samples, uint32_t count, bool endOfStream);
    bool HasLookahead(float position, uint32_t samples) const;
    bool Process(float *out, uint32_t count, float timeRate, float pitchRate,
                 float grainControl, SpliceAligner aligner = nullptr,
                 void *alignerContext = nullptr);
    uint32_t Rebase(uint32_t maximumOffset);

    float AudiblePosition() const;
    float RequiredLookaheadPosition(uint32_t outputSamples, float timeRate,
                                    float pitchRate, uint32_t safetySamples) const;
    bool AudiblePlaybackComplete(float endPosition) const;
    float ReferencePosition() const { return ref_.phi; }
    uint32_t KeyCount() const { return count_; }
    uint32_t AvailableKeyCapacity() const { return kKeyCapacity - count_; }
    bool ReachedEnd() const { return reachedEnd_; }
    float WindowSamples(float grainControl) const;

private:
    struct Pointer {
        float phi = 0.f;
        uint32_t ordinal = 0;
        float segmentIndex = 0.f;
        float nextIndex = 0.f;
        float segmentValue = 0.f;
        float valueDelta = 0.f;
        float invDuration = 0.f;
        bool segmentValid = false;
    };

    Keyframe *keys_ = nullptr;
    float sampleRate_ = 44100.f;
    uint32_t head_ = 0;
    uint32_t count_ = 0;
    uint32_t firstOrdinal_ = 0;

    float history_[4] {};
    uint32_t historyCount_ = 0;
    uint32_t analyzedSamples_ = 0;
    float previousDerivative_ = 0.f;
    int previousSign_ = 0;
    float lastSavedValue_ = 0.f;
    uint32_t lastSavedIndex_ = 0;
    bool reachedEnd_ = false;

    Pointer ref_;
    Pointer play_;
    Pointer temp_;
    bool splicing_ = false;
    float splicePhase_ = 0.f;
    float spliceIncrement_ = 1.f;

    const Keyframe& Key(uint32_t ordinal) const;
    bool Push(float index, float value);
    void DiscardConsumed();
    bool Advance(Pointer& pointer) const;
    float Render(const Pointer& pointer) const;
    static int Sign(float value);
    static float SmoothStep(float value);
};

} // namespace CTAG::SYNTHESIS

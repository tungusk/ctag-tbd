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

// Inspired by a thread on Elektronauts about keyframe-based time-stretching:
// https://www.elektronauts.com/t/i-fixed-elektrons-timestretching-proof-of-concept-vid/248300/18
//
// This embedded implementation intentionally differs from the described
// offline method. It incrementally analyzes a bounded streaming ring instead
// of precomputing the complete sample, forces a keyframe after long sections
// without extrema, and stores the source extrema amplitudes to preserve peaks.
// Reconstruction and crossfades use smoothstep interpolation. Splices are
// triggered by a configurable sample-time distance, with a key-distance guard
// for dense material, and the rompler aligns splice targets against source PCM.

#include "ExtremaStreamingStretch.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace CTAG::SYNTHESIS {

void ExtremaStreamingStretch::Init(void *workspace, float sampleRate) {
    keys_ = static_cast<Keyframe *>(workspace);
    sampleRate_ = sampleRate;
    Reset();
}

void ExtremaStreamingStretch::Reset() {
    head_ = 0;
    count_ = 0;
    firstOrdinal_ = 0;
    historyCount_ = 0;
    analyzedSamples_ = 0;
    previousDerivative_ = 0.f;
    previousSign_ = 0;
    lastSavedValue_ = 0.f;
    lastSavedIndex_ = 0;
    reachedEnd_ = false;
    ref_ = {};
    play_ = {};
    temp_ = {};
    splicing_ = false;
    splicePhase_ = 0.f;
    spliceIncrement_ = 1.f;
}

const ExtremaStreamingStretch::Keyframe& ExtremaStreamingStretch::Key(uint32_t ordinal) const {
    return keys_[(head_ + ordinal - firstOrdinal_) % kKeyCapacity];
}

bool ExtremaStreamingStretch::Push(float index, float value) {
    if (count_ == kKeyCapacity) return false;
    keys_[(head_ + count_) % kKeyCapacity] = {index, value};
    ++count_;
    return true;
}

int ExtremaStreamingStretch::Sign(float value) {
    return (value > 0.f) - (value < 0.f);
}

float ExtremaStreamingStretch::SmoothStep(float value) {
    value = std::clamp(value, 0.f, 1.f);
    return value * value * (3.f - 2.f * value);
}

bool ExtremaStreamingStretch::Analyze(const float *samples, uint32_t count, bool endOfStream) {
    constexpr float threshold = 0.001f;
    constexpr uint32_t forceKeyEvery = 256;

    for (uint32_t i = 0; i < count; ++i) {
        if (historyCount_ < 4) {
            history_[historyCount_++] = samples[i];
            if (historyCount_ == 1) {
                if (!Push(0.f, samples[i])) return false;
                lastSavedValue_ = samples[i];
            }
            ++analyzedSamples_;
            continue;
        }

        history_[0] = history_[1];
        history_[1] = history_[2];
        history_[2] = history_[3];
        history_[3] = samples[i];
        ++analyzedSamples_;

        // Cubic B-spline derivative at integer n is 0.5 * (x[n+1] - x[n-1]).
        const float derivative = 0.5f * (history_[3] - history_[1]);
        const int sign = Sign(derivative);
        const uint32_t candidateIndex = analyzedSamples_ - 2;
        const bool changed = sign != 0 && previousSign_ != 0 && sign != previousSign_;
        const bool forced = candidateIndex - lastSavedIndex_ >= forceKeyEvery;
        const float candidateValue = changed
            ? (previousSign_ > 0
                ? std::max(history_[1], history_[2])
                : std::min(history_[1], history_[2]))
            : history_[2];

        if ((changed && std::fabs(candidateValue - lastSavedValue_) >= threshold) || forced) {
            const float denominator = std::fabs(previousDerivative_) + std::fabs(derivative);
            const float alpha = denominator > 1.e-12f
                ? std::fabs(previousDerivative_) / denominator : 0.5f;
            // Preserve the source peak amplitude. The B-spline-derived position
            // remains useful, but evaluating the B-spline there smooths every
            // stored extremum and gives reconstruction an inherent low-pass tilt.
            const float index = static_cast<float>(candidateIndex - 1) + alpha;
            if (!Push(index, candidateValue)) return false;
            lastSavedValue_ = candidateValue;
            lastSavedIndex_ = candidateIndex;
        }

        previousDerivative_ = derivative;
        if (sign != 0) previousSign_ = sign;
    }

    if (endOfStream && !reachedEnd_) {
        reachedEnd_ = true;
        const float finalIndex = analyzedSamples_ > 0 ? static_cast<float>(analyzedSamples_ - 1) : 0.f;
        const float finalValue = historyCount_ > 0 ? history_[historyCount_ - 1] : 0.f;
        if (count_ == 0 || Key(firstOrdinal_ + count_ - 1).index < finalIndex) {
            if (!Push(finalIndex, finalValue)) return false;
        }
    }
    return true;
}

bool ExtremaStreamingStretch::HasLookahead(float position, uint32_t samples) const {
    if (count_ < 2) return false;
    return reachedEnd_ || Key(firstOrdinal_ + count_ - 1).index >= position + static_cast<float>(samples);
}

bool ExtremaStreamingStretch::Advance(Pointer& pointer) const {
    if (count_ < 2) return false;
    if (pointer.ordinal < firstOrdinal_) {
        pointer.ordinal = firstOrdinal_;
        pointer.segmentValid = false;
    }
    const uint32_t lastWindow = firstOrdinal_ + count_ - 2;
    if (pointer.segmentValid && pointer.ordinal <= lastWindow &&
        pointer.phi >= pointer.segmentIndex &&
        (pointer.phi < pointer.nextIndex || pointer.ordinal == lastWindow)) {
        return true;
    }
    while (pointer.ordinal < lastWindow && Key(pointer.ordinal + 1).index <= pointer.phi) {
        ++pointer.ordinal;
    }
    while (pointer.ordinal > firstOrdinal_ && Key(pointer.ordinal).index > pointer.phi) {
        --pointer.ordinal;
    }
    if (pointer.ordinal > lastWindow) return false;

    const Keyframe& a = Key(pointer.ordinal);
    const Keyframe& b = Key(pointer.ordinal + 1);
    const float duration = b.index - a.index;
    pointer.segmentIndex = a.index;
    pointer.nextIndex = b.index;
    pointer.segmentValue = a.value;
    pointer.valueDelta = b.value - a.value;
    pointer.invDuration = duration > 1.e-12f ? 1.f / duration : 0.f;
    pointer.segmentValid = true;
    return true;
}

float ExtremaStreamingStretch::Render(const Pointer& pointer) const {
    const float t = (pointer.phi - pointer.segmentIndex) * pointer.invDuration;
    return pointer.segmentValue + SmoothStep(t) * pointer.valueDelta;
}

float ExtremaStreamingStretch::WindowSamples(float grainControl) const {
    constexpr float minimumWindowSeconds = 0.005f;
    constexpr float maximumWindowSeconds = 0.2f;
    const float seconds = minimumWindowSeconds +
        SmoothStep(grainControl) * (maximumWindowSeconds - minimumWindowSeconds);
    return std::max(16.f, seconds * sampleRate_);
}

void ExtremaStreamingStretch::DiscardConsumed() {
    if (count_ < 4) return;
    uint32_t keep = std::min(ref_.ordinal, play_.ordinal);
    if (splicing_) keep = std::min(keep, temp_.ordinal);
    while (count_ > 4 && firstOrdinal_ + 2 < keep) {
        head_ = (head_ + 1) % kKeyCapacity;
        --count_;
        ++firstOrdinal_;
    }
}

bool ExtremaStreamingStretch::Process(float *out, uint32_t count, float timeRate,
                                      float pitchRate, float grainControl,
                                      SpliceAligner aligner, void *alignerContext) {
    if (count_ < 2 || timeRate <= 0.f || pitchRate <= 0.f) return false;
    const float windowSamples = WindowSamples(grainControl);
    const float maximumSplice = sampleRate_ * 0.2f;

    for (uint32_t i = 0; i < count; ++i) {
        if (!Advance(ref_) || !Advance(play_) || (splicing_ && !Advance(temp_))) return false;
        const float distance = std::fabs(ref_.phi - play_.phi);
        const uint32_t keyDistance = ref_.ordinal > play_.ordinal
            ? ref_.ordinal - play_.ordinal : play_.ordinal - ref_.ordinal;
        const float finalPosition = Key(firstOrdinal_ + count_ - 1).index;
        const bool audibleAtEnd = reachedEnd_ && play_.phi >= finalPosition;

        // Keep enough ring capacity for lookahead even when harmonically dense
        // material creates far more extrema per millisecond than percussion.
        constexpr uint32_t maximumKeyDistance = kKeyCapacity * 3 / 4;
        if (!splicing_ && !audibleAtEnd &&
            (distance > windowSamples || keyDistance > maximumKeyDistance)) {
            temp_ = ref_;
            if (aligner != nullptr) {
                constexpr uint32_t searchRadius = 96;
                temp_.phi = std::clamp(
                    aligner(alignerContext, play_.phi, ref_.phi, searchRadius),
                    Key(firstOrdinal_).index,
                    Key(firstOrdinal_ + count_ - 1).index);
                temp_.ordinal = ref_.ordinal;
                temp_.segmentValid = false;
                if (!Advance(temp_)) return false;
            }
            const float length = std::min(
                std::min(windowSamples, maximumSplice), std::max(16.f, distance));
            splicePhase_ = 0.f;
            spliceIncrement_ = 1.f / length;
            splicing_ = true;
        }

        if (splicing_) {
            const float a = Render(play_);
            const float b = Render(temp_);
            const float fade = SmoothStep(splicePhase_);
            out[i] = a + fade * (b - a);
            play_.phi += pitchRate;
            temp_.phi += pitchRate;
            ref_.phi += timeRate;
            if (reachedEnd_) ref_.phi = std::min(ref_.phi, finalPosition);
            splicePhase_ += spliceIncrement_ * pitchRate;
            if (splicePhase_ >= 1.f) {
                play_ = temp_;
                splicing_ = false;
            }
        } else {
            out[i] = Render(play_);
            play_.phi += pitchRate;
            ref_.phi += timeRate;
            if (reachedEnd_) ref_.phi = std::min(ref_.phi, finalPosition);
        }
    }
    DiscardConsumed();
    return true;
}

uint32_t ExtremaStreamingStretch::Rebase(uint32_t maximumOffset) {
    if (maximumOffset == 0 || count_ == 0) return 0;

    float minimumPosition = std::min(Key(firstOrdinal_).index, std::min(ref_.phi, play_.phi));
    if (splicing_) minimumPosition = std::min(minimumPosition, temp_.phi);
    const uint32_t offset = std::min(
        maximumOffset,
        static_cast<uint32_t>(std::max(0.f, floorf(minimumPosition))));
    if (offset == 0 || offset > analyzedSamples_ || offset > lastSavedIndex_) return 0;

    const float floatOffset = static_cast<float>(offset);
    for (uint32_t i = 0; i < count_; ++i) {
        keys_[(head_ + i) % kKeyCapacity].index -= floatOffset;
    }
    analyzedSamples_ -= offset;
    lastSavedIndex_ -= offset;
    ref_.phi -= floatOffset;
    play_.phi -= floatOffset;
    temp_.phi -= floatOffset;
    ref_.segmentValid = false;
    play_.segmentValid = false;
    temp_.segmentValid = false;
    return offset;
}

float ExtremaStreamingStretch::AudiblePosition() const {
    if (!splicing_) return play_.phi;
    return splicePhase_ >= 0.5f ? temp_.phi : play_.phi;
}

bool ExtremaStreamingStretch::AudiblePlaybackComplete(float endPosition) const {
    return !splicing_ && play_.phi >= endPosition;
}

float ExtremaStreamingStretch::RequiredLookaheadPosition(
    uint32_t outputSamples, float timeRate, float pitchRate, uint32_t safetySamples) const {
    float furthest = std::max(ref_.phi, play_.phi);
    if (splicing_) furthest = std::max(furthest, temp_.phi);

    // A splice can start anywhere in the next block. Cover both the reference
    // progression and every audible pointer's pitch-rate progression.
    const float projectedMovement =
        std::max(timeRate, pitchRate) * static_cast<float>(outputSamples);
    return furthest + projectedMovement + static_cast<float>(safetySamples);
}

} // namespace CTAG::SYNTHESIS

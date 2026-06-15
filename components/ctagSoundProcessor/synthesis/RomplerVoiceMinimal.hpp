/***************
CTAG TBD >>to be determined<< is an open source eurorack synthesizer module.

A project conceived within the Creative Technologies Arbeitsgruppe of
Kiel University of Applied Sciences: https://www.creative-technologies.de

(c) 2020 by Robert Manzke. All rights reserved.

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
#include <cstdint>
#include "helpers/ctagSampleRom.hpp"
#include "helpers/ctagADEnv.hpp"
#include "stmlib/dsp/filter.h"
#include "mifx/pitch_shifter_mono.h"
#include "ExtremaStreamingStretch.hpp"

using namespace CTAG::SP::HELPERS;

namespace CTAG::SYNTHESIS{
    class RomplerVoiceMinimal {
    public:
        struct Telemetry {
            uint32_t slice = 0;
            uint32_t sliceLength = 0;
            int32_t readPos = 0;
            int32_t startPos = 0;
            int32_t endPos = 0;
            int32_t loopPos = 0;
            float startOffsetRelative = 0.f;
            float lengthRelative = 1.f;
            float loopMarker = 0.f;
            bool playing = false;
            bool movingBackward = false;
        };

        struct Params{
            enum class TimeStretchAlgorithm : uint32_t {CLASSIC = 0, EXTREMA};
            uint32_t slice;
            float playbackSpeed, pitch;
            float startOffsetRelative, lengthRelative; // relative to entire sliceLength
            float a, d;
            float cutoff, resonance;
            float egFilter;
            enum class FilterType : uint32_t {NONE = 0x00, LP, BP, HP};
            FilterType filterType;
            bool loop, loopPiPo;
            float loopMarker; // relative to length of subsection, not sliceLength
            float egFM;
            uint32_t bitReduction;
            // struct for filter type
            bool gate;
            // Time-stretch controls
            bool timeStretchEnable = false; // bypass when false (no extra CPU)
            float timeStretchWindowSize = 0.5f; // normalized window size
            TimeStretchAlgorithm timeStretchAlgorithm = TimeStretchAlgorithm::CLASSIC;
        };

        void Init(const float samplingRate);
        void Process(float* out, uint32_t size);
        void Reset();
        Telemetry GetTelemetry() const { return telemetry; }

        RomplerVoiceMinimal();
        ~RomplerVoiceMinimal();

        Params params;

    private:
        // mode params
        bool preGate = false;
        // internal modulation
        ctagADEnv ad;
        // multimode filter
        stmlib::Svf svf;

        // pitch shifter, needs 2048 floats as buffer, i.e. 8KiB
        mifx::PitchShifterMono pitch_shifter;
        float *pitch_shifter_buffer;
        ExtremaStreamingStretch extrema_stretch;
        bool extremaWorkspaceActive = false;

        // process methods for modes
        void processBlock(float *out, const uint32_t size);
        bool processExtremaBlock(float *out, uint32_t size, int32_t startPos, int32_t endPos,
                                 int32_t loopPos, float timeRate, float pitchRate);
        uint32_t fillExtremaLogicalSamples(float *dst, uint32_t count, int32_t startPos,
                                           int32_t endPos, int32_t loopPos);
        uint32_t fillExtremaLogicalSamplesAt(int16_t *dst, uint32_t logicalPosition,
                                             uint32_t count, int32_t startPos,
                                             int32_t endPos, int32_t loopPos);
        struct ExtremaSourceCursor {
            int32_t source = 0;
            int32_t direction = 1;
            uint32_t remaining = 0;
            bool valid = false;
        };
        bool initExtremaSourceCursor(ExtremaSourceCursor& cursor, uint32_t logicalPosition,
                                     int32_t startPos, int32_t endPos, int32_t loopPos) const;
        void advanceExtremaSourceCursor(ExtremaSourceCursor& cursor, uint32_t count,
                                        int32_t endPos, int32_t loopPos) const;
        bool extremaSourcePosition(uint32_t logicalPosition, int32_t startPos, int32_t endPos,
                                   int32_t loopPos, int32_t& sourcePosition) const;
        uint32_t extremaLogicalPositionForSource(int32_t sourcePosition, int32_t startPos,
                                                 int32_t endPos, int32_t loopPos) const;
        static float alignExtremaSpliceThunk(void *context, float playPosition,
                                             float referencePosition, uint32_t searchRadius);
        float alignExtremaSplice(float playPosition, float referencePosition,
                                 uint32_t searchRadius);
        void resetExtremaStream(uint32_t logicalOrigin = 0);
        void rebaseExtremaStream(int32_t startPos, int32_t endPos, int32_t loopPos);
        uint32_t normalizeExtremaLogicalOrigin(uint32_t logicalOrigin, int32_t startPos,
                                               int32_t endPos, int32_t loopPos) const;
        void applyBitReduction(float *out, uint32_t size, int16_t bitReductionMask) const;
        // sample data
        ctagSampleRom sampleRom;
        uint32_t slice = 0;
        // anti aliasing filter data
        float coeffs_lpf[5]{0.f};
        float w_lpf1[5]{0.f};
        // params
        float fs = 44100.f;
        float sliceLockedStartOffset = 0.f;
        // modulation
        float fmDecay = 0.f;
        // buffer params
        enum class BufferStatus {STOPPED, READFIRST, READLAST, RUNNING};
        enum class PlayBackDirection {FWD, BWD, LOOPFWD, LOOPBWD, LOOPFWDPIPO, LOOPBWDPIPO};
        PlayBackDirection playBackDir;
        bool pipoFlip = false;
        BufferStatus bufferStatus;
        const uint32_t readBufferMaxSize = 2048; // 2k are ok given default params, there should be a bounds check with downsampling, not to exceed the buffer size
        float readBufferFloat[2048];
        int16_t readBufferInt16[2048];
        int32_t readBufferLength;
        float readBufferPhase = 0.f;
        float phaseIncrement = 0.f; // depending on pitch
        int32_t readPos = 0;
        Telemetry telemetry;
        uint32_t extremaLogicalOrigin = 0;
        uint32_t extremaLogicalWrite = 0;
        ExtremaSourceCursor extremaWriteCursor;
        uint32_t extremaConfigSlice = UINT32_MAX;
        int32_t extremaConfigStart = -1;
        int32_t extremaConfigEnd = -1;
        int32_t extremaConfigLoop = -1;
        PlayBackDirection extremaConfigDirection = PlayBackDirection::FWD;
        // bit reduction masks
        const uint16_t bit_reduction_masks[15] = {
                0xc000,
                0xe000,
                0xf000,
                0xf800,
                0xfc00,
                0xfe00,
                0xff00,
                0xff80,
                0xffc0,
                0xffe0,
                0xfff0,
                0xfff8,
                0xfffc,
                0xfffe,
                0xffff};
    };
}

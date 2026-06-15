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

#include "RomplerVoiceMinimal.hpp"
#include <cmath>
#include <cstring>
#include "stmlib/dsp/dsp.h"
#include "helpers/ctagFastMath.hpp"
#include "dsps_biquad.h"
#include "stmlib/dsp/units.h"
#include "clouds/resources.h" // use fade lut
#include "esp_heap_caps.h"

namespace CTAG::SYNTHESIS {

    static_assert(
        ExtremaStreamingStretch::kWorkspaceBytes == 2048 * sizeof(float),
        "Extrema backend must fit in the existing pitch-shifter PSRAM workspace");

    void RomplerVoiceMinimal::Process(float *out, const uint32_t size) {
        // check for trigger signal
        if (params.gate == true && params.gate != preGate) { // trigger happened reset to beginning of sample
            ad.Trigger();
            bufferStatus = BufferStatus::READFIRST;
            readBufferPhase = 0.f;
            pipoFlip = false;
            fmDecay = 1.f;
            resetExtremaStream();
        }
        preGate = params.gate;

        // compute slice parameters and check if slice data is available
        slice = params.slice;
        sliceLockedStartOffset = params.startOffsetRelative;

        if (!sampleRom.HasSlice(slice)) {
            // Audio-thread: never printf here. Even gated to every 50000 calls
            // the printf can block long enough to corrupt the audio buffer.
            // static uint32_t _hasSliceDiagCtr = 0;
            // if ((_hasSliceDiagCtr++ % 50000) == 0)
            //     printf("DIAG RomplerVoice: HasSlice(%lu) = false\n", (unsigned long)slice);
            memset(out, 0, size * sizeof(float));
            return;
        }
        uint32_t sliceLength = sampleRom.GetSliceSize(slice);
        if (sliceLength == 0) {
            // Audio-thread: never printf here (see note above).
            // static uint32_t _sliceLenDiagCtr = 0;
            // if ((_sliceLenDiagCtr++ % 50000) == 0)
            //     printf("DIAG RomplerVoice: slice=%lu sliceLength=0 (no data)\n", (unsigned long)slice);
        }

        // Bit reduction is an output effect. Keeping the source stream at full
        // resolution avoids rebuilding time-stretch state when BitCr changes.
        const int16_t output_brr_mask =
            static_cast<int16_t>(bit_reduction_masks[14 - params.bitReduction]);
        constexpr int16_t brr_mask = static_cast<int16_t>(0xffff);

        //  set eg and lfo parameters
        ad.SetAttack(params.a);
        ad.SetDecay(params.d);

        // calculate playback speed = dt = phase increment
        // check if time-stretch is active
        bool timeStretch = params.timeStretchEnable;
        phaseIncrement = params.playbackSpeed; // speed
        float pitchFactor = stmlib::SemitonesToRatio(params.pitch); // includes pitch FM
        constexpr float minimumPlaybackSpeed = 0.0001f;
        if (!std::isfinite(phaseIncrement) || !std::isfinite(pitchFactor) ||
            fabsf(phaseIncrement) < minimumPlaybackSpeed) {
            // A stopped scrub must never reach either stretch backend. In
            // particular, the classic backend divides pitch by playback speed;
            // allowing zero through poisons its persistent DSP state with Inf.
            if (bufferStatus != BufferStatus::STOPPED) {
                Reset();
                if (!extremaWorkspaceActive) pitch_shifter.Init(pitch_shifter_buffer);
            }
            telemetry.playing = false;
            memset(out, 0, size * sizeof(float));
            return;
        }
        if (timeStretch &&
            params.timeStretchAlgorithm != Params::TimeStretchAlgorithm::EXTREMA) {
            if (fabsf(phaseIncrement - 1.f) < 0.001f && fabsf(pitchFactor - 1.f) < 0.001f) {
                timeStretch = false;
            }
        }

        // pitch sample if time stretch is off
        if (!timeStretch){
            phaseIncrement *= pitchFactor;
        }


        // evaluate loop settings
        if (params.loop) {
            if (params.loopPiPo) {
                playBackDir = phaseIncrement >= 0.f ? PlayBackDirection::LOOPFWDPIPO
                                                    : PlayBackDirection::LOOPBWDPIPO;
                if (pipoFlip) { // flip direction if currently already in pipo
                    if (playBackDir == PlayBackDirection::LOOPFWDPIPO)
                        playBackDir = PlayBackDirection::LOOPBWDPIPO;
                    else
                        playBackDir = PlayBackDirection::LOOPFWDPIPO;
                }
            } else {
                playBackDir = phaseIncrement >= 0.f ? PlayBackDirection::LOOPFWD
                                                    : PlayBackDirection::LOOPBWD;
            }
        } else {
            playBackDir = phaseIncrement >= 0.f ? PlayBackDirection::FWD : PlayBackDirection::BWD; // playing fwd or bwd
        }
        // now take abs
        phaseIncrement = fabsf(phaseIncrement);
        // TODO: adapt this also in Rompler?
        if(phaseIncrement >= 6.f) phaseIncrement = 6.f; // limit to stay in memory and CPU capability bounds
        phaseIncrement += fmDecay * params.egFM * 4;
        fmDecay *= (0.9f + 0.0999999f * params.d / 50.f);

        // Resolve normalized controls into a valid audible window. Start and
        // Length retain their public semantics, while effective positions are
        // constrained to the current slice and leave at least one 32-sample
        // audio block whenever the slice itself is large enough.
        const int32_t sliceLengthInt = static_cast<int32_t>(sliceLength);
        const int32_t minimumPlaybackLength = std::min<int32_t>(32, sliceLengthInt);
        const int32_t maximumStartPos = std::max<int32_t>(0, sliceLengthInt - minimumPlaybackLength);
        int32_t startPos = std::clamp(
            static_cast<int32_t>(sliceLockedStartOffset * sliceLength),
            static_cast<int32_t>(0), maximumStartPos);
        int32_t playLength = static_cast<int32_t>(params.lengthRelative * sliceLength);
        playLength = std::clamp(playLength, minimumPlaybackLength, sliceLengthInt - startPos);
        const int32_t endPos = startPos + playLength;

        // Loop position has no playback effect while looping is disabled.
        // When enabled, constrain the loop subsection to the same 32-sample
        // minimum so a loop marker at the extreme end cannot silence playback.
        int32_t loopPos = startPos;
        if (params.loop) {
            const int32_t maximumLoopPos = std::max(startPos, endPos - minimumPlaybackLength);
            loopPos = std::clamp(
                startPos + static_cast<int32_t>(params.loopMarker * playLength),
                startPos, maximumLoopPos);
        }

        int32_t visibleReadPos = readPos;
        if (bufferStatus == BufferStatus::READFIRST) {
            const bool movingBackward =
                playBackDir == PlayBackDirection::BWD ||
                playBackDir == PlayBackDirection::LOOPBWD ||
                playBackDir == PlayBackDirection::LOOPBWDPIPO;
            visibleReadPos = movingBackward ? endPos - 1 : startPos;
        }
        visibleReadPos = std::clamp(visibleReadPos, startPos, endPos - 1);

        telemetry.slice = slice;
        telemetry.sliceLength = sliceLength;
        telemetry.readPos = visibleReadPos;
        telemetry.startPos = startPos;
        telemetry.endPos = endPos;
        telemetry.loopPos = loopPos;
        telemetry.startOffsetRelative = params.startOffsetRelative;
        telemetry.lengthRelative = params.lengthRelative;
        telemetry.loopMarker = params.loopMarker;
        telemetry.playing = bufferStatus != BufferStatus::STOPPED;
        telemetry.movingBackward =
            playBackDir == PlayBackDirection::BWD ||
            playBackDir == PlayBackDirection::LOOPBWD ||
            playBackDir == PlayBackDirection::LOOPBWDPIPO;

        // in this cases return silence
        if (bufferStatus == BufferStatus::STOPPED) {
            memset(out, 0, size * sizeof(float));
            return;
        }
        if (startPos >= endPos - 1) {
            memset(out, 0, size * sizeof(float));
            return;
        }
        if (playLength == 0) {
            memset(out, 0, size * sizeof(float));
            return;
        }
        if (params.loop && loopPos >= endPos - 1) {
            memset(out, 0, size * sizeof(float));
            return;
        }

        bool renderedExtrema = false;
        const bool extremaRequested =
            timeStretch &&
            params.timeStretchAlgorithm == Params::TimeStretchAlgorithm::EXTREMA;
        if (extremaRequested) {
            if (!extremaWorkspaceActive) {
                extrema_stretch.Init(pitch_shifter_buffer, fs);
                extremaWorkspaceActive = true;
            }
            if (extremaConfigSlice != slice || extremaConfigStart != startPos ||
                extremaConfigEnd != endPos || extremaConfigLoop != loopPos ||
                extremaConfigDirection != playBackDir) {
                const bool movingBackward =
                    playBackDir == PlayBackDirection::BWD ||
                    playBackDir == PlayBackDirection::LOOPBWD ||
                    playBackDir == PlayBackDirection::LOOPBWDPIPO;
                const int32_t currentSource = bufferStatus == BufferStatus::READFIRST
                    ? (movingBackward ? endPos - 1 : startPos)
                    : std::clamp(readPos, startPos, endPos - 1);
                extremaConfigSlice = slice;
                extremaConfigStart = startPos;
                extremaConfigEnd = endPos;
                extremaConfigLoop = loopPos;
                extremaConfigDirection = playBackDir;
                resetExtremaStream(extremaLogicalPositionForSource(
                    currentSource, startPos, endPos, loopPos));
            }
            renderedExtrema = processExtremaBlock(
                out, size, startPos, endPos, loopPos, phaseIncrement, pitchFactor);
            if (!renderedExtrema) {
                memset(out, 0, size * sizeof(float));
                bufferStatus = BufferStatus::STOPPED;
                resetExtremaStream();
            }
        }

        if (!extremaRequested) {
            if (extremaWorkspaceActive) {
                pitch_shifter.Init(pitch_shifter_buffer);
                extremaWorkspaceActive = false;
            }

            // calculate required buffer length
            // TODO: check if phase increment is within bounds for buffer max size --> partially done with asserts
            //  phaseIncrementMax*size*sizeof(datatype)+4), 32(5octaves up)*32(standard buffer size) --> > 1k words, we use 2k words
            readBufferLength = static_cast<uint32_t>(phaseIncrement * float(size) + readBufferPhase);
        if (readBufferLength > static_cast<int32_t>(readBufferMaxSize - 2)) {
            readBufferLength = readBufferMaxSize - 2;
        }


        // calc marks and read assemble buffers depending on playback mode
        // readPos semantic is: start read position from linear rom buffer, updated after every read cycle
        switch (playBackDir) {
            case PlayBackDirection::FWD:
                // first buffer ?
                if (bufferStatus == BufferStatus::READFIRST) {
                    readPos = startPos; // adjust playhead
                }
                // check bounds
                if (readPos + readBufferLength >= endPos) { // check if at end of sample, then read only last couple of samples
                    int32_t remainBuffer = endPos - readPos;
                    memset(readBufferInt16, 0, readBufferLength * sizeof(int16_t));
                    if(remainBuffer > 0){ // play last couple of samples
                        sampleRom.ReadSlice(readBufferInt16, slice, readPos, remainBuffer);
                        bufferStatus = BufferStatus::READLAST;
                    }else{
                        memset(out, 0, size * sizeof(float)); // silence output
                        bufferStatus = BufferStatus::STOPPED;
                        return;
                    }
                }else{
                    // obtain sample rom data
                    assert(readBufferLength <= (readBufferMaxSize - 2)); // beyond buffer size?
                    sampleRom.ReadSlice(readBufferInt16, slice, readPos, readBufferLength);
                    // and write convert to float buffer
                    for (int i = 0; i < readBufferLength; i++) {
                        readBufferFloat[i + 2] =
                                static_cast<float>(readBufferInt16[i]&brr_mask) * 0.000030518509476f; // only 2 for linear interp
                    }
                }

                // interpolate process buffer
                processBlock(out, size);

                // update read position
                readPos += readBufferLength;
                bufferStatus = BufferStatus::RUNNING;

                break;
            case PlayBackDirection::BWD:
                // first buffer?
                if (bufferStatus == BufferStatus::READFIRST) { // trigger happened reset to beginning of sample
                    readPos = endPos - readBufferLength; // adjust playhead
                }

                // check bounds
                if (readPos <= startPos) { // check if at start of sample, then read only remaining samples
                    memset(readBufferInt16, 0, readBufferLength * sizeof(int16_t));
                    int32_t remainBuffer = readPos - loopPos + readBufferLength;
                    if(remainBuffer > 0){ // read last couple of samples
                        readPos = startPos;
                        sampleRom.ReadSlice(readBufferInt16, slice, readPos, remainBuffer);
                        for (int i = 0; i < readBufferLength; i++) { // read convert reverse
                            readBufferFloat[i + 2] = static_cast<float>(readBufferInt16[remainBuffer - i - 1]&brr_mask) *
                                                     0.000030518509476f; // only 2 for linear interp
                        }
                        bufferStatus = BufferStatus::READLAST;
                    }else{
                        memset(out, 0, size * sizeof(float)); // silence output
                        bufferStatus = BufferStatus::STOPPED;
                        return;
                    }
                }else{
                    assert(readBufferLength <= (readBufferMaxSize - 2)); // beyond buffer size?
                    sampleRom.ReadSlice(readBufferInt16, slice, readPos, readBufferLength);
                    // and write convert reversed to float buffer
                    for (int i = 0; i < readBufferLength; i++) {
                        readBufferFloat[i + 2] = static_cast<float>(readBufferInt16[readBufferLength - i - 1]&brr_mask) *
                                                 0.000030518509476f; // only 2 for linear interp
                    }
                }

                // interpolate process buffer
                processBlock(out, size);

                // update read position
                readPos -= static_cast<uint32_t>(phaseIncrement * float(size) + readBufferPhase);
                bufferStatus = BufferStatus::RUNNING;

                break;
            case PlayBackDirection::LOOPFWD:
                // first buffer ?
                if (bufferStatus == BufferStatus::READFIRST) {
                    readPos = startPos; // adjust playhead
                }
                // check bounds
                if (readPos + readBufferLength >=
                    endPos) { // check if at end of sample, then read only last couple of samples
                    int32_t remainBuffer = endPos - readPos;
                    if (remainBuffer <= 0) {
                        readPos = loopPos;
                        sampleRom.ReadSlice(readBufferInt16, slice, readPos, readBufferLength);
                        readPos += readBufferLength;
                    } else {
                        int16_t *bufPos = readBufferInt16;
                        sampleRom.ReadSlice(bufPos, slice, readPos, remainBuffer);
                        readPos = loopPos;
                        // read wrap from loop pos the rest of the buffer
                        if (readBufferLength > remainBuffer) {
                            bufPos = &readBufferInt16[remainBuffer];
                            remainBuffer = readBufferLength - remainBuffer;
                            sampleRom.ReadSlice(bufPos, slice, readPos, remainBuffer);
                            readPos += remainBuffer;
                        }
                    }
                } else {
                    // obtain sample rom data
                    assert(readBufferLength <= (readBufferMaxSize - 2)); // beyond buffer size?
                    sampleRom.ReadSlice(readBufferInt16, slice, readPos, readBufferLength);
                    // update read position
                    readPos += readBufferLength;
                }

                // and write convert to float buffer
                for (int i = 0; i < readBufferLength; i++) {
                    readBufferFloat[i + 2] =
                            static_cast<float>(readBufferInt16[i]&brr_mask) * 0.000030518509476f; // only 2 for linear interp
                }

                // interpolate process buffer
                processBlock(out, size);

                bufferStatus = BufferStatus::RUNNING;
                break;

            case PlayBackDirection::LOOPBWD:
                // first buffer?
                if (bufferStatus == BufferStatus::READFIRST) { // trigger happened reset to beginning of sample
                    readPos = endPos - readBufferLength;
                }
                // check bounds
                if (readPos <= loopPos) { // in reverse mode go only until loop mark
                    int32_t remainBuffer = readPos - loopPos + readBufferLength;
                    if (remainBuffer <= 0) {
                        readPos = endPos - readBufferLength;
                        sampleRom.ReadSlice(readBufferInt16, slice, readPos, readBufferLength);
                        // and write convert reversed to float buffer
                        for (int i = 0; i < readBufferLength; i++) { // read convert reverse
                            readBufferFloat[i + 2] = static_cast<float>(readBufferInt16[readBufferLength - i - 1]&brr_mask) *
                                                     0.000030518509476f; // only 2 for linear interp
                        }
                        // interpolate process buffer
                        processBlock(out, size);
                        readPos -= static_cast<uint32_t>(phaseIncrement * float(size) + readBufferPhase);
                    } else {
                        readPos = loopPos;
                        int16_t *bufPos = &readBufferInt16[readBufferLength - remainBuffer];;
                        sampleRom.ReadSlice(bufPos, slice, readPos, remainBuffer);
                        if (readBufferLength > remainBuffer) {
                            // read remaining elements
                            bufPos = readBufferInt16;
                            remainBuffer = readBufferLength - remainBuffer;
                            readPos = endPos - remainBuffer;
                            sampleRom.ReadSlice(bufPos, slice, readPos, remainBuffer);
                        }
                        // and write convert reversed to float buffer
                        for (int i = 0; i < readBufferLength; i++) { // read convert reverse
                            readBufferFloat[i + 2] = static_cast<float>(readBufferInt16[readBufferLength - i - 1]&brr_mask) *
                                                     0.000030518509476f; // only 2 for linear interp
                        }
                        // interpolate process buffer
                        processBlock(out, size);
                        readPos -= static_cast<uint32_t>(phaseIncrement * float(size) + readBufferPhase);
                    }
                } else { // normal reverse read
                    // obtain sample rom forward
                    assert(readBufferLength <= (readBufferMaxSize - 2)); // beyond buffer size?
                    sampleRom.ReadSlice(readBufferInt16, slice, readPos, readBufferLength);
                    // and write convert reversed to float buffer
                    for (int i = 0; i < readBufferLength; i++) { // read convert reverse
                        readBufferFloat[i + 2] = static_cast<float>(readBufferInt16[readBufferLength - i - 1]&brr_mask) *
                                                 0.000030518509476f; // only 2 for linear interp
                    }
                    // interpolate process buffer
                    processBlock(out, size);
                    // update read position
                    readPos -= static_cast<uint32_t>(phaseIncrement * float(size) + readBufferPhase);
                }

                bufferStatus = BufferStatus::RUNNING;
                break;

            case PlayBackDirection::LOOPFWDPIPO:
                // first buffer ?
                if (bufferStatus == BufferStatus::READFIRST) {
                    readPos = startPos; // adjust playhead
                }
                // check bounds
                if (readPos + readBufferLength >=
                    endPos) { // check if at end of sample, then read only last couple of samples
                    int32_t remainBuffer = endPos - readPos;
                    if (remainBuffer <= 0) { // pipo event
                        readPos = endPos - readBufferLength;
                        sampleRom.ReadSlice(readBufferInt16, slice, readPos, readBufferLength);
                        // and write convert reversed to float buffer
                        for (int i = 0; i < readBufferLength; i++) { // read convert reverse
                            readBufferFloat[i + 2] = static_cast<float>(readBufferInt16[readBufferLength - i - 1]&brr_mask) *
                                                     0.000030518509476f; // only 2 for linear interp
                        }
                        // interpolate process buffer
                        processBlock(out, size);
                        pipoFlip ^= true; // toggle flip
                        readPos -= static_cast<uint32_t>(phaseIncrement * float(size) + readBufferPhase);
                    } else {
                        sampleRom.ReadSlice(readBufferInt16, slice, readPos, remainBuffer);
                        int i;
                        for (i = 0; i < remainBuffer; i++) { // still fwd
                            readBufferFloat[i + 2] =
                                    static_cast<float>(readBufferInt16[i]&brr_mask) *
                                    0.000030518509476f; // only 2 for linear interp
                        }
                        readPos += remainBuffer;
                        if (readBufferLength > remainBuffer) {
                            // read remaining elements
                            remainBuffer = readBufferLength - remainBuffer;
                            readPos = readPos - remainBuffer;
                            sampleRom.ReadSlice(readBufferInt16, slice, readPos, remainBuffer);
                            for (; i < readBufferLength; i++) { // read convert reverse
                                readBufferFloat[i + 2] = static_cast<float>(readBufferInt16[readBufferLength - i - 1]&brr_mask) *
                                                         0.000030518509476f; // only 2 for linear interp
                            }
                        }
                        // interpolate process buffer
                        processBlock(out, size);
                        readPos -= static_cast<uint32_t>(phaseIncrement * float(size) + readBufferPhase);
                        pipoFlip ^= true;
                    }
                } else {
                    // obtain sample rom data
                    assert(readBufferLength <= (readBufferMaxSize - 2)); // beyond buffer size?
                    sampleRom.ReadSlice(readBufferInt16, slice, readPos, readBufferLength);
                    // update read position
                    readPos += readBufferLength;
                    // and write convert to float buffer
                    for (int i = 0; i < readBufferLength; i++) {
                        readBufferFloat[i + 2] =
                                static_cast<float>(readBufferInt16[i]&brr_mask) * 0.000030518509476f; // only 2 for linear interp
                    }
                    // interpolate process buffer
                    processBlock(out, size);
                }

                bufferStatus = BufferStatus::RUNNING;
                break;

            case PlayBackDirection::LOOPBWDPIPO:
                // first buffer?
                if (bufferStatus == BufferStatus::READFIRST) { // trigger happened reset to beginning of sample
                    readPos = endPos - readBufferLength - 1;
                }
                // check bounds
                if (readPos <= loopPos) { // in reverse mode go only until loop mark
                    int32_t remainBuffer = readPos - loopPos + readBufferLength;
                    readPos = loopPos;
                    if (remainBuffer <= 0) { // jump to loop marker and read entire buffer from there
                        sampleRom.ReadSlice(readBufferInt16, slice, readPos, readBufferLength);
                        // and write convert to float buffer
                        for (int i = 0; i < readBufferLength; i++) { // forward play
                            readBufferFloat[i + 2] =
                                    static_cast<float>(readBufferInt16[i]&brr_mask) *
                                    0.000030518509476f; // only 2 for linear interp
                        }
                        // interpolate process buffer
                        processBlock(out, size);
                        readPos += readBufferLength;
                        pipoFlip ^= true;
                    } else {
                        int16_t *bufPos = readBufferInt16;
                        sampleRom.ReadSlice(bufPos, slice, readPos, remainBuffer);
                        // still reverse
                        int i;
                        // and write convert reversed to float buffer
                        for (i = 0; i < remainBuffer; i++) { // read convert reverse
                            readBufferFloat[i + 2] = static_cast<float>(readBufferInt16[remainBuffer - i - 1]&brr_mask) *
                                                     0.000030518509476f; // only 2 for linear interp
                        }
                        // rest forward
                        if (readBufferLength > remainBuffer) {
                            // read remaining elements
                            bufPos = &readBufferInt16[i];
                            remainBuffer = readBufferLength - remainBuffer;
                            sampleRom.ReadSlice(bufPos, slice, readPos, remainBuffer);
                            readPos += remainBuffer;
                            for (; i < readBufferLength; i++) { // rest forward
                                readBufferFloat[i + 2] =
                                        static_cast<float>(readBufferInt16[i]&brr_mask) *
                                        0.000030518509476f; // only 2 for linear interp
                            }
                        }
                        // interpolate process buffer
                        processBlock(out, size);
                        pipoFlip ^= true;
                    }
                } else { // normal reverse read
                    // obtain sample rom forward
                    assert(readBufferLength <= (readBufferMaxSize - 2)); // beyond buffer size?
                    sampleRom.ReadSlice(readBufferInt16, slice, readPos, readBufferLength);
                    // and write convert reversed to float buffer
                    for (int i = 0; i < readBufferLength; i++) { // read convert reverse
                        readBufferFloat[i + 2] = static_cast<float>(readBufferInt16[readBufferLength - i - 1]&brr_mask) *
                                                 0.000030518509476f; // only 2 for linear interp
                    }
                    // interpolate process buffer
                    processBlock(out, size);
                    // update read position
                    readPos -= static_cast<uint32_t>(phaseIncrement * float(size) + readBufferPhase);
                }

                bufferStatus = BufferStatus::RUNNING;
                break;
        }
        }

        // check if buffer should be stopped
        if (bufferStatus == BufferStatus::READLAST) {
            bufferStatus = BufferStatus::STOPPED;
        }
        if (!ad.GetIsRunning()) bufferStatus = BufferStatus::STOPPED;

        // pitch correct if in timestretch mode
        if (timeStretch && !extremaRequested) {
            pitch_shifter.set_size(params.timeStretchWindowSize);
            pitch_shifter.set_ratio(pitchFactor / params.playbackSpeed);
            pitch_shifter.Process(out, size);
        }

        // apply SVF filter
        // Reuse the amplitude AD envelope for filter modulation. Reading its
        // current value here updates the cutoff once per 32-sample block
        // without advancing or duplicating the envelope generator.
        float fCut = params.cutoff + params.egFilter * ad.GetValue();
        CONSTRAIN(fCut, 0.f, 1.f)
        fCut = 20.f * stmlib::SemitonesToRatio(fCut * 120.f);
        float fReso = params.resonance;
        CONSTRAIN(fReso, .5f, 20.f)
        svf.
                set_f_q<stmlib::FREQUENCY_FAST>(fCut
                                                / 44100.f, fReso);
        switch (params.filterType) {
            case Params::FilterType::LP:
                svf.
                        Process<stmlib::FILTER_MODE_LOW_PASS>(out, out, size
                );
                break;
            case Params::FilterType::BP:
                svf.
                        Process<stmlib::FILTER_MODE_BAND_PASS>(out, out, size
                );
                break;
            case Params::FilterType::HP:
                svf.
                        Process<stmlib::FILTER_MODE_HIGH_PASS>(out, out, size
                );
                break;
            default:
                break;
        }

        applyBitReduction(out, size, output_brr_mask);

    }

    void RomplerVoiceMinimal::Init(const float samplingRate) {
        fs = samplingRate;
        ad.SetSampleRate(fs);
        ad.SetModeExp();
        ad.SetLoop(false);
        bufferStatus = BufferStatus::STOPPED;
        pipoFlip = false;
    }

    RomplerVoiceMinimal::RomplerVoiceMinimal() {
        // pitch shifter memory
        pitch_shifter_buffer = (float*) heap_caps_malloc(2048 * sizeof(float), MALLOC_CAP_SPIRAM);
        assert(pitch_shifter_buffer != nullptr);
        pitch_shifter.Init(pitch_shifter_buffer);

        params = Params{};

        Reset();
    }

    RomplerVoiceMinimal::~RomplerVoiceMinimal() {
        heap_caps_free(pitch_shifter_buffer);
    }

    void RomplerVoiceMinimal::processBlock(float *out, const uint32_t size) {
        // fade first incoming buffer
        if (bufferStatus == BufferStatus::READFIRST) {
            readBufferFloat[0] = 0.01f * readBufferFloat[2];
            readBufferFloat[1] = 0.33f * readBufferFloat[2];
        }
        // TODO fade last buffer
        // apply anti-aliasing low-pass when downsampling, i.e. pitch up, not required if pitch down (upsampling)
        // > 0.1f to limit processing power at high pitch, has aliasing then
        // TODO anti aliasing could possibly completely be removed
        float fAntiAlias = 0.5f / phaseIncrement;
        if (fAntiAlias < 0.5f && fAntiAlias > 0.1f && params.filterType == Params::FilterType::NONE) {
            // the more cascades the better, but beware of cost
            dsps_biquad_gen_lpf_f32(coeffs_lpf, fAntiAlias, .5f);
            dsps_biquad_f32(&readBufferFloat[2], &readBufferFloat[2], readBufferLength, coeffs_lpf, w_lpf1);
        }

        // interpolate sample buffer from data
        // and apply AM
        for (int i = 0; i < size; i++) {
            // interpolate wave
            const float p = readBufferPhase;
            MAKE_INTEGRAL_FRACTIONAL(p);
            // use this to save more cpu, however correct zdelays to 2 instead of 4
            float x = InterpolateWaveLinear(readBufferFloat, p_integral, p_fractional);
            // apply AM
            out[i] = x * ad.Process();
            readBufferPhase += phaseIncrement;
        }
        // first buffer, fade in, TODO check for buffer sizes (LUT is 17 default, size is 32 default)
        if (bufferStatus == BufferStatus::READFIRST) {
            for (int i = 0; i < LUT_XFADE_IN_SIZE; i++) {
                out[i] *= clouds::lut_xfade_in[i];
            }
        }
        // last buffer, fade out, TODO, see above
        if (bufferStatus == BufferStatus::READLAST) {
            for (int i = 0, j = size - LUT_XFADE_OUT_SIZE; i < LUT_XFADE_OUT_SIZE; i++, j++) {
                out[j] *= clouds::lut_xfade_out[i];
            }
        }
        // TODO: interpolation artefact reduction filter
        // update phase
        readBufferPhase = readBufferPhase - static_cast<int32_t >(readBufferPhase); // phase remainder for next cycle
        // update Zs
        readBufferFloat[0] = readBufferFloat[readBufferLength];
        readBufferFloat[1] = readBufferFloat[readBufferLength + 1];
    }

    bool RomplerVoiceMinimal::extremaSourcePosition(
        uint32_t logicalPosition, int32_t startPos, int32_t endPos,
        int32_t loopPos, int32_t& sourcePosition) const {
        logicalPosition += extremaLogicalOrigin;
        const uint32_t length = static_cast<uint32_t>(endPos - startPos);
        const uint32_t loopLength = static_cast<uint32_t>(endPos - loopPos);
        if (length == 0 || loopLength == 0) return false;

        switch (extremaConfigDirection) {
            case PlayBackDirection::FWD:
                if (logicalPosition >= length) return false;
                sourcePosition = startPos + static_cast<int32_t>(logicalPosition);
                return true;
            case PlayBackDirection::BWD:
                if (logicalPosition >= length) return false;
                sourcePosition = endPos - 1 - static_cast<int32_t>(logicalPosition);
                return true;
            case PlayBackDirection::LOOPFWD:
                if (logicalPosition < length) {
                    sourcePosition = startPos + static_cast<int32_t>(logicalPosition);
                } else {
                    sourcePosition = loopPos +
                        static_cast<int32_t>((logicalPosition - length) % loopLength);
                }
                return true;
            case PlayBackDirection::LOOPBWD:
                sourcePosition = endPos - 1 -
                    static_cast<int32_t>(logicalPosition % loopLength);
                return true;
            case PlayBackDirection::LOOPFWDPIPO:
                if (logicalPosition < length) {
                    sourcePosition = startPos + static_cast<int32_t>(logicalPosition);
                    return true;
                }
                logicalPosition -= length;
                if ((logicalPosition / loopLength) % 2 == 0) {
                    sourcePosition = endPos - 1 -
                        static_cast<int32_t>(logicalPosition % loopLength);
                } else {
                    sourcePosition = loopPos +
                        static_cast<int32_t>(logicalPosition % loopLength);
                }
                return true;
            case PlayBackDirection::LOOPBWDPIPO:
                if ((logicalPosition / loopLength) % 2 == 0) {
                    sourcePosition = endPos - 1 -
                        static_cast<int32_t>(logicalPosition % loopLength);
                } else {
                    sourcePosition = loopPos +
                        static_cast<int32_t>(logicalPosition % loopLength);
                }
                return true;
        }
        return false;
    }

    uint32_t RomplerVoiceMinimal::extremaLogicalPositionForSource(
        int32_t sourcePosition, int32_t startPos, int32_t endPos, int32_t loopPos) const {
        sourcePosition = std::clamp(sourcePosition, startPos, endPos - 1);
        switch (extremaConfigDirection) {
            case PlayBackDirection::FWD:
            case PlayBackDirection::LOOPFWD:
            case PlayBackDirection::LOOPFWDPIPO:
                return static_cast<uint32_t>(sourcePosition - startPos);
            case PlayBackDirection::BWD:
                return static_cast<uint32_t>(endPos - 1 - sourcePosition);
            case PlayBackDirection::LOOPBWD:
            case PlayBackDirection::LOOPBWDPIPO:
                return static_cast<uint32_t>(endPos - 1 - std::max(sourcePosition, loopPos));
        }
        return 0;
    }

    bool RomplerVoiceMinimal::initExtremaSourceCursor(
        ExtremaSourceCursor& cursor, uint32_t logicalPosition,
        int32_t startPos, int32_t endPos, int32_t loopPos) const {
        cursor = {};
        logicalPosition += extremaLogicalOrigin;
        const uint32_t length = static_cast<uint32_t>(endPos - startPos);
        const uint32_t loopLength = static_cast<uint32_t>(endPos - loopPos);
        if (length == 0 || loopLength == 0) return false;

        uint32_t phase = 0;
        switch (extremaConfigDirection) {
            case PlayBackDirection::FWD:
                if (logicalPosition >= length) return false;
                cursor.source = startPos + static_cast<int32_t>(logicalPosition);
                cursor.direction = 1;
                cursor.remaining = length - static_cast<uint32_t>(logicalPosition);
                break;
            case PlayBackDirection::BWD:
                if (logicalPosition >= length) return false;
                cursor.source = endPos - 1 - static_cast<int32_t>(logicalPosition);
                cursor.direction = -1;
                cursor.remaining = length - static_cast<uint32_t>(logicalPosition);
                break;
            case PlayBackDirection::LOOPFWD:
                if (logicalPosition < length) {
                    cursor.source = startPos + static_cast<int32_t>(logicalPosition);
                    cursor.direction = 1;
                    cursor.remaining = length - static_cast<uint32_t>(logicalPosition);
                } else {
                    phase = (logicalPosition - length) % loopLength;
                    cursor.source = loopPos + static_cast<int32_t>(phase);
                    cursor.direction = 1;
                    cursor.remaining = loopLength - static_cast<uint32_t>(phase);
                }
                break;
            case PlayBackDirection::LOOPBWD:
                phase = logicalPosition % loopLength;
                cursor.source = endPos - 1 - static_cast<int32_t>(phase);
                cursor.direction = -1;
                cursor.remaining = loopLength - static_cast<uint32_t>(phase);
                break;
            case PlayBackDirection::LOOPFWDPIPO:
                if (logicalPosition < length) {
                    cursor.source = startPos + static_cast<int32_t>(logicalPosition);
                    cursor.direction = 1;
                    cursor.remaining = length - static_cast<uint32_t>(logicalPosition);
                    break;
                }
                logicalPosition -= length;
                phase = logicalPosition % loopLength;
                cursor.direction = ((logicalPosition / loopLength) & 1U) == 0U ? -1 : 1;
                cursor.source = cursor.direction < 0
                    ? endPos - 1 - static_cast<int32_t>(phase)
                    : loopPos + static_cast<int32_t>(phase);
                cursor.remaining = loopLength - static_cast<uint32_t>(phase);
                break;
            case PlayBackDirection::LOOPBWDPIPO:
                phase = logicalPosition % loopLength;
                cursor.direction = ((logicalPosition / loopLength) & 1U) == 0U ? -1 : 1;
                cursor.source = cursor.direction < 0
                    ? endPos - 1 - static_cast<int32_t>(phase)
                    : loopPos + static_cast<int32_t>(phase);
                cursor.remaining = loopLength - static_cast<uint32_t>(phase);
                break;
        }
        cursor.valid = cursor.remaining > 0;
        return cursor.valid;
    }

    void RomplerVoiceMinimal::advanceExtremaSourceCursor(
        ExtremaSourceCursor& cursor, uint32_t count, int32_t endPos, int32_t loopPos) const {
        cursor.source += cursor.direction * static_cast<int32_t>(count);
        cursor.remaining -= count;
        if (cursor.remaining > 0) return;

        const uint32_t loopLength = static_cast<uint32_t>(endPos - loopPos);
        switch (extremaConfigDirection) {
            case PlayBackDirection::FWD:
            case PlayBackDirection::BWD:
                cursor.valid = false;
                return;
            case PlayBackDirection::LOOPFWD:
                cursor.source = loopPos;
                cursor.direction = 1;
                break;
            case PlayBackDirection::LOOPBWD:
                cursor.source = endPos - 1;
                cursor.direction = -1;
                break;
            case PlayBackDirection::LOOPFWDPIPO:
            case PlayBackDirection::LOOPBWDPIPO:
                cursor.direction = -cursor.direction;
                cursor.source = cursor.direction < 0 ? endPos - 1 : loopPos;
                break;
        }
        cursor.remaining = loopLength;
    }

    uint32_t RomplerVoiceMinimal::fillExtremaLogicalSamples(
        float *dst, uint32_t count, int32_t startPos, int32_t endPos,
        int32_t loopPos) {
        if (!extremaWriteCursor.valid &&
            !initExtremaSourceCursor(
                extremaWriteCursor, extremaLogicalWrite, startPos, endPos, loopPos)) {
            return 0;
        }

        uint32_t produced = 0;
        while (produced < count && extremaWriteCursor.valid) {
            const uint32_t run = std::min(
                {count - produced, extremaWriteCursor.remaining, readBufferMaxSize});
            const int32_t firstSource = extremaWriteCursor.source;
            if (extremaWriteCursor.direction > 0) {
                sampleRom.ReadSlice(readBufferInt16, slice, firstSource, run);
                for (uint32_t i = 0; i < run; ++i) {
                    dst[produced + i] =
                        static_cast<float>(readBufferInt16[i]) * 0.000030518509476f;
                }
            } else {
                const int32_t lowestSource = firstSource - static_cast<int32_t>(run) + 1;
                sampleRom.ReadSlice(readBufferInt16, slice, lowestSource, run);
                for (uint32_t i = 0; i < run; ++i) {
                    dst[produced + i] = static_cast<float>(
                        readBufferInt16[run - i - 1]) * 0.000030518509476f;
                }
            }
            produced += run;
            extremaLogicalWrite += run;
            advanceExtremaSourceCursor(extremaWriteCursor, run, endPos, loopPos);
        }
        return produced;
    }

    uint32_t RomplerVoiceMinimal::fillExtremaLogicalSamplesAt(
        int16_t *dst, uint32_t logicalPosition, uint32_t count,
        int32_t startPos, int32_t endPos, int32_t loopPos) {
        ExtremaSourceCursor cursor;
        if (!initExtremaSourceCursor(cursor, logicalPosition, startPos, endPos, loopPos)) {
            return 0;
        }

        uint32_t produced = 0;
        while (produced < count && cursor.valid) {
            const uint32_t run = std::min(
                {count - produced, cursor.remaining, readBufferMaxSize});
            const int32_t firstSource = cursor.source;
            if (cursor.direction > 0) {
                sampleRom.ReadSlice(dst + produced, slice, firstSource, run);
            } else {
                const int32_t lowestSource = firstSource - static_cast<int32_t>(run) + 1;
                sampleRom.ReadSlice(dst + produced, slice, lowestSource, run);
                std::reverse(dst + produced, dst + produced + run);
            }
            produced += run;
            advanceExtremaSourceCursor(cursor, run, endPos, loopPos);
        }
        return produced;
    }

    float RomplerVoiceMinimal::alignExtremaSpliceThunk(
        void *context, float playPosition, float referencePosition, uint32_t searchRadius) {
        return static_cast<RomplerVoiceMinimal *>(context)->alignExtremaSplice(
            playPosition, referencePosition, searchRadius);
    }

    float RomplerVoiceMinimal::alignExtremaSplice(
        float playPosition, float referencePosition, uint32_t searchRadius) {
        constexpr uint32_t matchLength = 32;
        constexpr uint32_t searchStep = 2;
        const uint32_t playStart = static_cast<uint32_t>(std::max(0.f, floorf(playPosition)));
        if (fillExtremaLogicalSamplesAt(
                readBufferInt16, playStart, matchLength,
                extremaConfigStart, extremaConfigEnd, extremaConfigLoop) < matchLength) {
            return referencePosition;
        }
        for (uint32_t i = 0; i < matchLength; ++i) {
            readBufferFloat[i] = static_cast<float>(readBufferInt16[i]);
        }

        const uint32_t reference = static_cast<uint32_t>(std::max(0.f, floorf(referencePosition)));
        const uint32_t candidateStart = reference > searchRadius ? reference - searchRadius : 0;
        const uint32_t candidateCount = searchRadius * 2 + matchLength;
        const uint32_t available = fillExtremaLogicalSamplesAt(
            readBufferInt16, candidateStart, candidateCount,
            extremaConfigStart, extremaConfigEnd, extremaConfigLoop);
        if (available < matchLength) return referencePosition;

        float bestScore = INFINITY;
        uint32_t bestOffset = std::min(reference - candidateStart, available - matchLength);
        for (uint32_t offset = 0; offset + matchLength <= available; offset += searchStep) {
            float score = 0.f;
            for (uint32_t i = 0; i < matchLength; ++i) {
                score += fabsf(readBufferFloat[i] - static_cast<float>(readBufferInt16[offset + i]));
            }
            // Matching the local slope reduces phase-inverted splice choices.
            const float playSlope = readBufferFloat[matchLength - 1] - readBufferFloat[0];
            const float candidateSlope =
                static_cast<float>(readBufferInt16[offset + matchLength - 1]) -
                static_cast<float>(readBufferInt16[offset]);
            score += fabsf(playSlope - candidateSlope) * 4.f;
            if (score < bestScore) {
                bestScore = score;
                bestOffset = offset;
            }
        }
        return static_cast<float>(candidateStart + bestOffset);
    }

    bool RomplerVoiceMinimal::processExtremaBlock(
        float *out, uint32_t size, int32_t startPos, int32_t endPos,
        int32_t loopPos, float timeRate, float pitchRate) {
        constexpr uint32_t feedChunk = 256;
        constexpr uint32_t lookaheadSafety = 512;
        rebaseExtremaStream(startPos, endPos, loopPos);
        const float requiredLookahead = extrema_stretch.RequiredLookaheadPosition(
            size, timeRate, pitchRate, lookaheadSafety);

        while (!extrema_stretch.HasLookahead(requiredLookahead, 0)) {
            const uint32_t availableKeys = extrema_stretch.AvailableKeyCapacity();
            if (availableKeys <= 1) break;
            const uint32_t safeFeedCount = std::min(feedChunk, availableKeys - 1);
            const uint32_t filled = fillExtremaLogicalSamples(
                readBufferFloat, safeFeedCount, startPos, endPos, loopPos);
            const bool endOfStream = filled < safeFeedCount;
            const bool accepted = extrema_stretch.Analyze(readBufferFloat, filled, endOfStream);
            if (endOfStream) break;
            if (!accepted) {
                const float minimumRequired = extrema_stretch.RequiredLookaheadPosition(
                    size, timeRate, pitchRate, 64);
                if (!extrema_stretch.HasLookahead(minimumRequired, 0)) {
                    return false;
                }
                break;
            }
        }

        if (!extrema_stretch.Process(
                out, size, timeRate, pitchRate, params.timeStretchWindowSize,
                &RomplerVoiceMinimal::alignExtremaSpliceThunk, this)) {
            return false;
        }

        for (uint32_t i = 0; i < size; ++i) out[i] *= ad.Process();
        bufferStatus = BufferStatus::RUNNING;

        int32_t audibleSource = startPos;
        const uint32_t audibleLogical = static_cast<uint32_t>(
            std::max(0.f, extrema_stretch.AudiblePosition()));
        if (extremaSourcePosition(audibleLogical, startPos, endPos, loopPos, audibleSource)) {
            readPos = audibleSource;
            telemetry.readPos = audibleSource;
        }

        const float logicalEnd = static_cast<float>(endPos - startPos - 1);
        const float localLogicalEnd =
            logicalEnd - static_cast<float>(extremaLogicalOrigin);
        if (!params.loop &&
            static_cast<float>(extremaLogicalOrigin) +
                extrema_stretch.ReferencePosition() >= logicalEnd &&
            extrema_stretch.AudiblePlaybackComplete(localLogicalEnd)) {
            bufferStatus = BufferStatus::READLAST;
        }
        return true;
    }

    uint32_t RomplerVoiceMinimal::normalizeExtremaLogicalOrigin(
        uint32_t logicalOrigin, int32_t startPos, int32_t endPos, int32_t loopPos) const {
        const uint32_t length = static_cast<uint32_t>(endPos - startPos);
        const uint32_t loopLength = static_cast<uint32_t>(endPos - loopPos);
        if (loopLength == 0) return logicalOrigin;

        switch (extremaConfigDirection) {
            case PlayBackDirection::LOOPFWD:
                return logicalOrigin < length
                    ? logicalOrigin : length + (logicalOrigin - length) % loopLength;
            case PlayBackDirection::LOOPBWD:
                return logicalOrigin % loopLength;
            case PlayBackDirection::LOOPFWDPIPO: {
                const uint32_t period = loopLength * 2;
                return logicalOrigin < length
                    ? logicalOrigin : length + (logicalOrigin - length) % period;
            }
            case PlayBackDirection::LOOPBWDPIPO:
                return logicalOrigin % (loopLength * 2);
            case PlayBackDirection::FWD:
            case PlayBackDirection::BWD:
                return logicalOrigin;
        }
        return logicalOrigin;
    }

    void RomplerVoiceMinimal::rebaseExtremaStream(
        int32_t startPos, int32_t endPos, int32_t loopPos) {
        if (!params.loop) return;
        constexpr uint32_t rebaseThreshold = 8U * 1024U * 1024U;
        constexpr uint32_t rebaseTarget = 4U * 1024U * 1024U;
        if (extremaLogicalWrite < rebaseThreshold) return;

        const uint32_t offset = extrema_stretch.Rebase(rebaseTarget);
        if (offset == 0) return;
        extremaLogicalWrite -= offset;
        extremaLogicalOrigin = normalizeExtremaLogicalOrigin(
            extremaLogicalOrigin + offset, startPos, endPos, loopPos);
    }

    void RomplerVoiceMinimal::resetExtremaStream(uint32_t logicalOrigin) {
        extrema_stretch.Reset();
        extremaLogicalOrigin = logicalOrigin;
        extremaLogicalWrite = 0;
        extremaWriteCursor = {};
    }

    void RomplerVoiceMinimal::applyBitReduction(
        float *out, uint32_t size, int16_t bitReductionMask) const {
        if (static_cast<uint16_t>(bitReductionMask) == 0xffff) return;
        for (uint32_t i = 0; i < size; ++i) {
            const float scaled = std::clamp(out[i], -1.f, 0.999969482f) * 32768.f;
            const int16_t sample = static_cast<int16_t>(lrintf(scaled));
            out[i] = static_cast<float>(
                static_cast<int16_t>(sample & bitReductionMask)) * 0.000030518509476f;
        }
    }

    void RomplerVoiceMinimal::Reset() {
        ad.Reset();
        readBufferFloat[0] = readBufferFloat[1] = 0.f;
        preGate = false;
        bufferStatus = BufferStatus::STOPPED;
        readBufferPhase = 0.f;
        pipoFlip = false;
        resetExtremaStream();
    }

}

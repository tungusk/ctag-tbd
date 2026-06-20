/***************
CTAG TBD >>to be determined<< is an open source eurorack synthesizer module.

A project conceived within the Creative Technologies Arbeitsgruppe of
Kiel University of Applied Sciences: https://www.creative-technologies.de

(c) 2026 by Robert Manzke. All rights reserved.

The CTAG TBD software is licensed under the GNU General Public License
(GPL 3.0), available here: https://www.gnu.org/licenses/gpl-3.0.txt
***************/

#pragma once

#include <cstdint>

namespace CTAG {
    namespace DRIVERS {

        enum class CodecKind : uint8_t {
            None = 0,
            Aic3254,
            Ak4619,
        };

        class Codec {
        public:
            Codec() = delete;

            static void InitCodec();
            static void PrintBootTiming();

            static CodecKind GetActiveCodec();
            static uint32_t GetNumInputChannels();
            static uint32_t GetNumOutputChannels();

            static void ADCHighPassEnable();
            static void ADCHighPassDisable();
            static void SetOutputLevels(uint32_t left, uint32_t right);

            static void ReadBuffer(float *buf, uint32_t frames);
            static void WriteBuffer(float *buf, uint32_t frames);
        };

    }
}

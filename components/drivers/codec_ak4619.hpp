/***************
CTAG TBD >>to be determined<< is an open source eurorack synthesizer module.

(c) 2026 by Robert Manzke. All rights reserved.

The CTAG TBD software is licensed under the GNU General Public License
(GPL 3.0), available here: https://www.gnu.org/licenses/gpl-3.0.txt
***************/

#pragma once

#include <cstdint>

namespace CTAG {
    namespace DRIVERS {

        class CodecAk4619 {
        public:
            CodecAk4619() = delete;

            static bool InitCodec();
            static void PrintBootTiming();
            static void ADCHighPassEnable();
            static void ADCHighPassDisable();
            static void SetOutputLevels(uint32_t left, uint32_t right);
            static void ReadBuffer(float *buf, uint32_t frames);
            static void WriteBuffer(float *buf, uint32_t frames);
        };

    }
}

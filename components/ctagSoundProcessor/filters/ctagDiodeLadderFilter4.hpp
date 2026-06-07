// This code is released under the MIT license (see below).
//
// The MIT License
//
// Copyright (c) 2012 Dominique Wurtz (www.blaukraut.info)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#pragma once

#include "ctagFilterBase.hpp"
#include <cmath>

namespace CTAG::SP::HELPERS {
    class ctagDiodeLadderFilter4 : public ctagFilterBase {
    public:
        virtual void SetCutoff(float cutoff) override;

        virtual void SetResonance(float resonance) override;

        virtual void SetSampleRate(float fs) override;

        virtual float Process(float in) override;

        virtual void Init() override;

    private:
        static float clip(const float x) {
            return x / (1 + fabs(x));
        }

        float k = 0.f;
        float A = 1.f;
        float z[5] = {0.f};
        float a = 1.f;
        float ainv = 1.f;
        float a2 = 1.f;
        float b = 3.f;
        float b2 = 9.f;
        float c = 1.f / 47.f;
        float g = 2.f / 47.f;
    };
}

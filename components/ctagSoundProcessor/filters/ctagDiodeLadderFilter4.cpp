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

#include <algorithm>
#include "ctagDiodeLadderFilter4.hpp"
#include "helpers/ctagFastMath.hpp"

void CTAG::SP::HELPERS::ctagDiodeLadderFilter4::SetCutoff(float cutoff) {
    constexpr float minCutoff = 20.f;
    constexpr float maxCutoff = 15000.f;
    constexpr float nyquistMargin = 0.45f;
    constexpr float curveAmount = 0.7f;

    //if (!std::isfinite(cutoff)) cutoff = minCutoff;
    const float safeMaxCutoff = fmaxf(minCutoff, fminf(maxCutoff, fs_ * nyquistMargin));
    cutoff_ = fmaxf(minCutoff, fminf(cutoff, safeMaxCutoff));

    // Preserve the endpoints, but provide finer control through the first 90%
    // of the range. The final part retains access to the full cutoff range.
    const float normalized = (cutoff_ - minCutoff) / (safeMaxCutoff - minCutoff);
    const float curved = normalized * ((1.f - curveAmount) + curveAmount * normalized);
    const float effectiveCutoff = minCutoff + curved * (safeMaxCutoff - minCutoff);

    a = 2.f * M_PI * effectiveCutoff / fs_; // PI is Nyquist frequency
    a = 2.f * fasttan(0.5f * a); // dewarping, not required with 2x oversampling
    ainv = 1.f / a;
    a2 = a * a;
    b = 2.f * a + 1.f;
    b2 = b * b;
    c = 1.f / (2.f * a2 * a2 - 4.f * a2 * b2 + b2 * b2);
    g = 2.f * a2 * a2 * c;
    feedbackReciprocal = 1.f / (1.f + g * k);
}

void CTAG::SP::HELPERS::ctagDiodeLadderFilter4::SetResonance(float resonance) {
    //if (!std::isfinite(resonance)) resonance = 0.f;
    resonance_ = fmaxf(0.f, fminf(resonance, 1.f));
    k = 20.f * resonance_;
    A = 1.f + 0.5f * k; // resonance gain compensation
    feedbackReciprocal = 1.f / (1.f + g * k);
}

void CTAG::SP::HELPERS::ctagDiodeLadderFilter4::SetSampleRate(float fs) {
    //if (!std::isfinite(fs) || fs < 100.f) return;
    ctagFilterBase::SetSampleRate(fs);
    SetCutoff(cutoff_);
}

float CTAG::SP::HELPERS::ctagDiodeLadderFilter4::Process(float in) {
    float x = in;
    // current state
    const float s0 = (a2 * a * z[0] + a2 * b * z[1] + z[2] * (b2 - 2 * a2) * a + z[3] * (b2 - 3 * a2) * b) * c;
    const float s = s0 - z[4];

    // solve feedback loop (linear)
    float y5 = (g * x + s) * feedbackReciprocal;

    // input clipping
    const float y0 = clip(x - k * y5);
    y5 = g * y0 + s;

    // compute integrator outputs
    const float y4 = g * y0 + s0;
    const float y3 = (b * y4 - z[3]) * ainv;
    const float y2 = (b * y3 - a * y4 - z[2]) * ainv;
    const float y1 = (b * y2 - a * y3 - z[1]) * ainv;

    // update filter state
    z[0] += 4 * a * (y0 - y1 + y2);
    z[1] += 2 * a * (y1 - 2 * y2 + y3);
    z[2] += 2 * a * (y2 - 2 * y3 + y4);
    z[3] += 2 * a * (y3 - 2 * y4);
    z[4] = y4 - y5;

    return A * y4;
}

void CTAG::SP::HELPERS::ctagDiodeLadderFilter4::Init() {
    std::fill(z, z + 5, 0);
    SetResonance(resonance_);
    SetCutoff(cutoff_);
}

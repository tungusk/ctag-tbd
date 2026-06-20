/***************
CTAG TBD >>to be determined<< is an open source eurorack synthesizer module.

A project conceived within the Creative Technologies Arbeitsgruppe of
Kiel University of Applied Sciences: https://www.creative-technologies.de

(c) 2026 by Robert Manzke. All rights reserved.

The CTAG TBD software is licensed under the GNU General Public License
(GPL 3.0), available here: https://www.gnu.org/licenses/gpl-3.0.txt
***************/

#include "codec.hpp"

#include "codec_aic3254.hpp"
#include "codec_ak4619.hpp"
#include "esp_attr.h"
#include "esp_log.h"

using namespace CTAG::DRIVERS;

static const char *TAG = "CODEC";
static CodecKind active_codec = CodecKind::None;

void Codec::InitCodec() {
    if (CodecAk4619::InitCodec()) {
        active_codec = CodecKind::Ak4619;
        ESP_LOGI(TAG, "Active codec: AK4619 (%lu in, %lu out)",
                 GetNumInputChannels(), GetNumOutputChannels());
        return;
    }

    CodecAic3254::InitCodec();
    active_codec = CodecKind::Aic3254;
    ESP_LOGI(TAG, "Active codec: AIC3254 (%lu in, %lu out)",
             GetNumInputChannels(), GetNumOutputChannels());
}

void Codec::PrintBootTiming() {
    if (active_codec == CodecKind::Aic3254) {
        CodecAic3254::PrintBootTiming();
    } else if (active_codec == CodecKind::Ak4619) {
        CodecAk4619::PrintBootTiming();
    }
}

CodecKind Codec::GetActiveCodec() {
    return active_codec;
}

uint32_t Codec::GetNumInputChannels() {
    return active_codec == CodecKind::Ak4619 ? 4u : 2u;
}

uint32_t Codec::GetNumOutputChannels() {
    return active_codec == CodecKind::Ak4619 ? 4u : 2u;
}

void Codec::ADCHighPassEnable() {
    if (active_codec == CodecKind::Ak4619) {
        CodecAk4619::ADCHighPassEnable();
    } else {
        CodecAic3254::ADCHighPassEnable();
    }
}

void Codec::ADCHighPassDisable() {
    if (active_codec == CodecKind::Ak4619) {
        CodecAk4619::ADCHighPassDisable();
    } else {
        CodecAic3254::ADCHighPassDisable();
    }
}

void Codec::SetOutputLevels(uint32_t left, uint32_t right) {
    if (active_codec == CodecKind::Ak4619) {
        CodecAk4619::SetOutputLevels(left, right);
    } else {
        CodecAic3254::SetOutputLevels(left, right);
    }
}

void IRAM_ATTR Codec::ReadBuffer(float *buf, uint32_t frames) {
    if (active_codec == CodecKind::Ak4619) {
        CodecAk4619::ReadBuffer(buf, frames);
    } else {
        CodecAic3254::ReadBuffer(buf, frames);
    }
}

void IRAM_ATTR Codec::WriteBuffer(float *buf, uint32_t frames) {
    if (active_codec == CodecKind::Ak4619) {
        CodecAk4619::WriteBuffer(buf, frames);
    } else {
        CodecAic3254::WriteBuffer(buf, frames);
    }
}

/***************
TBD-16 — Macro/Preset System & GrooveBoxRack

(c) 2025-2026 Per-Olov Jernberg (possan). https://possan.codes
(c) 2024-2026 Johannes Elias Lohbihler for dadamachines.
Based in part on the CTAG TBD DrumRack / engine by Robert Manzke (CTAG Kiel).

Licensed under the GNU General Public License (GPL 3.0):
https://www.gnu.org/licenses/gpl-3.0.txt

A commercial licence is available — contact https://dadamachines.com/contact/

Provided "as is" without any express or implied warranties.
See LICENSE in the repository root for full terms.

SPDX-License-Identifier: GPL-3.0-only
***************/

#include "RackSynth.hpp"
#include "RackChannelMixer.hpp"
#include "../ctagSoundProcessorGrooveBoxRack.hpp"

using namespace CTAG::SP;

#define minVolume 0.000001f
#define maxFXSendLevelDly 2.f
#define maxFXSendLevelRev 1.5f

void RackChannelMixer::Init(const GrooveBoxRackInitData *initdata) {
	cc_base = initdata->cc_base;
	const int   track = initdata->track_index;          // copies — `initdata` is a transient local in GrooveBoxRack::Init
	ctagSoundProcessorGrooveBoxRack* const rack = initdata->rack;

	// mixer-strip params (cc 1..5). cc 6/7 are unused here, so put the channel's
	// machine selector ("device") and mute there. "device" used to exist, was
	// dropped, and the WebUI still drives it — re-wire it to setTrackMachine().
	initdata->rack->registerParamAndCC(initdata, "lev", 1, [&](const int val){ mix_lev = val;});
	initdata->rack->registerParamAndCC(initdata, "pan", 2, [&](const int val){ mix_pan = val;});
	initdata->rack->registerParamAndCC(initdata, "fx1", 3, [&](const int val){ mix_fx1 = val;});
	initdata->rack->registerParamAndCC(initdata, "fx2", 4, [&](const int val){ mix_fx2 = val;});
	initdata->rack->registerParamAndCC(initdata, "tracklength", 5, [&](const int val){ mix_track_length = val; });
	initdata->rack->registerParamAndCC(initdata, "device", 6, [&, track, rack](const int val){ mix_device = val; rack->setTrackMachineByDeviceValue(track, val); });
	initdata->rack->registerParamAndCC(initdata, "mute", 7, [&](const int val){ mix_mute = val; });

	this->enabled = false;
	this->track_length = 16;
	this->volumeMultiplier = 1.0f;
}

void RackChannelMixer::PreProcess(const GrooveBoxRackProcessData &data) {
    // mix_pan ("chN_pan") is a bipolar parameter, -4095..4095 with 0 = centre (see mui-...json),
    // so map it straight to -1..1. (It used to be read as 0..4096 then *2-1, which made 0 = hard
    // left — every track defaulted to the left speaker.)
    MK_FLT_PAR_ABS_NOCV(fPan, mix_pan, 4095.f, 1.f)
    if (fPan < -1.f) fPan = -1.f; else if (fPan > 1.f) fPan = 1.f;
    MK_FLT_PAR_ABS_NOCV(fLev, mix_lev, 4096.f, 2.f); fLev *= fLev;
    MK_FLT_PAR_ABS_NOCV(fFX1Send, mix_fx1, 4095.f, maxFXSendLevelDly); fFX1Send *= fFX1Send;
    MK_FLT_PAR_ABS_NOCV(fFX2Send, mix_fx2, 4095.f, maxFXSendLevelRev); fFX2Send *= fFX2Send;
    MK_FLT_PAR_ABS_NOCV(fTrackLength, mix_track_length, 4096.f, 128.f);

	fLev *= volumeMultiplier;

	if (fLev != this->level) {
		ESP_LOGI("RackChannelMixer", "Level changed from %f to %f", this->level, fLev);
		this->level = fLev;
	}

    this->enabled = (level > minVolume) && (mix_mute != 0);   // chN_mute: 0 = silent

	if (fPan != this->pan) {
		// ESP_LOGI("RackChannelMixer", "Pan changed from %f to %f", this->pan, fPan);
		this->pan = fPan;
	}

	fTrackLength = (int)floor(fTrackLength);
	if (fTrackLength != this->track_length) {
		// ESP_LOGI("RackChannelMixer", "Track length changed from %d to %d",
			// this->track_length, (int)fTrackLength);
		this->track_length = (int)fTrackLength;
	}

	this->send1 = fFX1Send;
	this->send2 = fFX2Send;
}

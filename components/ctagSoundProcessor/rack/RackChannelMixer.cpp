/***************
TBD-16 — Macro/Preset System & GrooveBoxRack

(c) 2024-2026 Per-Olov Jernberg (possan). https://possan.codes
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
	const uint8_t track = initdata->track_index;
	ctagSoundProcessorGrooveBoxRack* rack = initdata->rack;

	// "device" (cc 6) — WebUI machine selector: 0..4095 buckets across the track's N
	// machines (see setTrackMachineByDeviceValue).  Sim needs this to come up with
	// each track's first machine assigned (chN_device defaults to 0 in the factory
	// preset).  On hardware the macro/RP2350 layer calls setTrackMachine() directly
	// afterwards and is authoritative — the WebUI knob still works on top of it.
	// "mute" (cc 7) — Pico-driven and WebUI-driven track mute; PreProcess gates
	// this->enabled on (!muted) to silence the sum output regardless of level.
	initdata->rack->registerParamAndCC(initdata, "lev", 1, [&](const int val){ mix_lev = val;});
	initdata->rack->registerParamAndCC(
	    initdata, "pan", 2,
	    [&](const int val){ mix_pan = val; },
	    [&](const int val){
		    // MIDI CC pan is unipolar 0..127 with an exact centre at 64.
		    // handleMidiControlChange expands it to exact multiples of 32.
		    const int cc = val / 32;
		    if (cc < 64) {
			    mix_pan = (cc * 4095) / 64 - 4095;
		    } else if (cc > 64) {
			    mix_pan = ((cc - 64) * 4095) / 63;
		    } else {
			    mix_pan = 0;
		    }
	    });
	initdata->rack->registerParamAndCC(initdata, "fx1", 3, [&](const int val){ mix_fx1 = val;});
	initdata->rack->registerParamAndCC(initdata, "fx2", 4, [&](const int val){ mix_fx2 = val;});
	initdata->rack->registerParamAndCC(initdata, "tracklength", 5, [&](const int val){ mix_track_length = val; });
	initdata->rack->registerParamAndCC(initdata, "device", 6, [&, track, rack](const int val){ mix_device = val; rack->setTrackMachineByDeviceValue(track, val); });
	initdata->rack->registerParamAndCC(initdata, "mute", 7, [&](const int val){ muted = (val == 0); });

	this->enabled = false;
	this->track_length = 16;
	this->volumeMultiplier = 1.0f;
}

void RackChannelMixer::PreProcess(const GrooveBoxRackProcessData &data) {
    // mix_pan ("chN_pan") is BIPOLAR -4095..4095 with 0 = centre (see
    // mui-GrooveBoxRack.json: min=-4095 max=4095, default 0).  Map straight
    // to -1..1 (clamped).  Earlier code treated it as 0..4064 unipolar then
    // applied fPan*2-1, which made the default value 0 -> pan=-1 = HARD LEFT
    // (every track defaulted to the left speaker — mono-on-the-left bug).
    MK_FLT_PAR_ABS_NOCV(fPan, mix_pan, 4095.f, 1.f)
    if (fPan < -1.f) fPan = -1.f; else if (fPan > 1.f) fPan = 1.f;
    MK_FLT_PAR_ABS_NOCV(fLev, mix_lev, 4096.f, 2.f); fLev *= fLev;
    MK_FLT_PAR_ABS_NOCV(fFX1Send, mix_fx1, 4095.f, maxFXSendLevelDly); fFX1Send *= fFX1Send;
    MK_FLT_PAR_ABS_NOCV(fFX2Send, mix_fx2, 4095.f, maxFXSendLevelRev); fFX2Send *= fFX2Send;
    MK_FLT_PAR_ABS_NOCV(fTrackLength, mix_track_length, 4096.f, 128.f);

	fLev *= volumeMultiplier;

	if (fLev != this->level) {
		// Audio-thread: never printf/ESP_LOGx here — blocking log call corrupts audio.
		// ESP_LOGI("RackChannelMixer", "Level changed from %f to %f", this->level, fLev);
		this->level = fLev;
	}

    // `muted` is set from the Pico via SoundProcessorManager::SetTrackMute.
    // Gating `enabled` here silences the Input track's continuous audio and
    // cuts synth tails on tracks 1-15 the moment the user toggles mute.
    this->enabled = (level > minVolume) && !muted;

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

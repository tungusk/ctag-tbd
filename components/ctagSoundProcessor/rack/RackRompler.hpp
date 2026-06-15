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

#pragma once

#include "RackSynth.hpp"
#include "synthesis/RomplerVoiceMinimal.hpp"
#include "helpers/ctagSampleRom.hpp"

using namespace CTAG::SP;

class RackRompler {
public:
	static constexpr uint8_t NaturalPitchMidiNote = 48;

    void Process(const GrooveBoxRackProcessData &data);
    void Init(const GrooveBoxRackInitData *initdata);
	bool enabled;
	atomic<int16_t> bank_index;
	int track_length {16};
    float s1_out[BUF_SZ];
	void noteOn(uint8_t note, uint8_t vel);
	void noteOff(uint8_t note, uint8_t vel);
	void SetMarkerControl(float startOffsetRelative, float lengthRelative,
	                      float loopMarker, uint32_t revision);
	void SetTimeStretchReferenceTempo(uint32_t referenceTempo) {
		timeStretchReferenceTempo.store(referenceTempo);
	}
	CTAG::SYNTHESIS::RomplerVoiceMinimal::Telemetry GetTelemetry() const;
	uint32_t GetMarkerRevision() const { return marker_revision; }

private:
	CTAG::SYNTHESIS::RomplerVoiceMinimal rompler;
	bool trig_prev {false};
	float midi_freq {0.0f};
	int midi_note {NaturalPitchMidiNote};
	bool midi_trig {false};
	uint8_t timeStretchMode {0};
	atomic<uint32_t> timeStretchReferenceTempo {0};
	atomic<int16_t> s1_speed;
	atomic<int16_t> s1_pitch;
	atomic<int16_t> s1_bank;
	atomic<int16_t> s1_slice;
	atomic<float> s1_start {0.f};
	atomic<float> s1_end {1.f};
	atomic<int16_t> s1_lp;
	atomic<int16_t> s1_lp_pp;
	atomic<float> s1_lp_pos {0.f};
	atomic<uint32_t> marker_revision {0};
	atomic<int16_t> s1_atk;
	atomic<int16_t> s1_dcy;
	atomic<int16_t> s1_eg2fm;
	atomic<int16_t> s1_eg2flt {2048};
	atomic<int16_t> s1_brr;
	atomic<int16_t> s1_ft;
	atomic<int16_t> s1_fc;
	atomic<int16_t> s1_fq;
	atomic<int16_t> s1_tsmode {0};
	atomic<int16_t> s1_tsamount {2048};
	atomic<int16_t> s1_tssteps;
};

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
#include "braids/analog_oscillator.h"
#include "braids/signature_waveshaper.h"
#include "braids/macro_oscillator.h"
#include "braids/settings.h"
#include "braids/svf.h"
#include "synthesis/RomplerVoiceMinimal.hpp"
#include "helpers/ctagSampleRom.hpp"
#include "helpers/ctagSineSource.hpp"
#include "helpers/ctagADSREnv.hpp"
#include "plaits/dsp/oscillator/wavetable_oscillator.h"

using namespace CTAG::SP;

class RackWTOsc {
public:
	void Process(const GrooveBoxRackProcessData &data);
	void Init(const GrooveBoxRackInitData *initdata);
	bool enabled;
    float out[BUF_SZ * 2];
	void noteOn(uint8_t note, uint8_t vel);
	void noteOff(uint8_t note, uint8_t vel);

private:
	// Single-bank preprocessing: ~33 KB PSRAM for one bank's preprocessed
	// wavetables + a 2 KB float scratch. Bank change in Process triggers
	// a one-shot ~10 ms preprocessing pass (audible block-level glitch on
	// the bank-change tick — acceptable since bank changes are rare and
	// non-real-time). The previous Option-A (preprocess all 32 banks at
	// Init = 1 MiB PSRAM) was reverted because it pushed total free PSRAM
	// below 1 MB and starved the file-manager REST API's rapidjson
	// allocator → Store-access-fault crash on file-manager open.
	static constexpr int kMaxBanks = 32;
	static constexpr int kBankSamples = 260 * 64;          // int16 per bank
	static constexpr int kBankBytes = kBankSamples * 2;

	void prepareBank(int bankIndex, HELPERS::ctagSampleRom *sampleRom);

	int16_t *bankBuffer = nullptr;          // [kBankSamples] — current bank
	float *fbufScratch = nullptr;           // [512] — preprocessing scratch
	const int16_t *wavetables[64];
	int currentBank = 0;
	int lastBank = -1;
	bool isWaveTableGood = false;
	struct Voice {
			plaits::WavetableOscillator<256, 64> oscillator;
			ctagSineSource lfo;
			ctagADSREnv adsr;
			braids::Svf svf;
		float valADSR = 0.f;
		float valLFO = 0.f;
		float pre_fWt = 0.f;
		int midi_note = 0;
		uint32_t note_serial = 0;
		bool gate = false;
		bool preGate = false;
		bool note_held = false;
		uint8_t pending_velocity = 100;
		volatile bool pending_retrigger = false;
		int silence_tail_blocks = 0;
	};

	static constexpr int kNumVoices = 2;
	Voice voices[kNumVoices];
	uint32_t note_serial_counter = 0;

	// Silence gate — once a voice's ADSR has fully released and no note is
	// held, skip that voice while still rendering any other active voice.
	static constexpr int kSilenceTailBlocks = 6890;   // ~5 s

	// All atomics explicitly zero-initialized. std::atomic<int32_t>'s
	// default constructor leaves the value indeterminate — and a
	// not-yet-pushed parameter being read at boot would put bizarre
	// values on the audio thread. Bipolar params land at "0" (cv mid)
	// once the preset push happens; for the first few audio blocks
	// before that push, 0 is a safer default than garbage.
	atomic<int32_t> mode {0};          // 0=Duo, 1..36=Mono unison 0..35 cents
	atomic<int32_t> tune {2048};       // bipolar mid (no detune)
	atomic<int32_t> wavebank {0};
	atomic<int32_t> wave {0};
	atomic<int32_t> fmode {0};
	atomic<int32_t> fcut {4095};       // filter open
	atomic<int32_t> freso {0};
	atomic<int32_t> lfo2wave {0};
	atomic<int32_t> lfo2am {0};
	atomic<int32_t> lfo2fm {0};
	atomic<int32_t> lfo2filtfm {0};
	atomic<int32_t> eg2wave {2048};    // bipolar mid (no EG mod)
	atomic<int32_t> eg2am {2048};
	atomic<int32_t> eg2fm {2048};
	atomic<int32_t> eg2filtfm {2048};
	atomic<int32_t> lfospeed {0};
	atomic<int32_t> lfophase {0};      // 0..4095 -> voice LFO phase spread 0..180 degrees
	atomic<int32_t> egfasl {0};
	atomic<int32_t> attack {0};
	atomic<int32_t> decay {2048};      // some decay so first note isn't clipped
	atomic<int32_t> sustain {2048};    // some sustain so first note isn't silent
	atomic<int32_t> release {1024};    // moderate release, well below drone-cap
};

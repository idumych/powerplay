/*
 * PowerPlay — a 10-band graphic equalizer with a preamp, Winamp's bands.
 *
 * Each band is a peaking filter (RBJ biquad) at its centre frequency; a
 * band above 45% of the sample rate is left out. Runs on interleaved stereo
 * float, in place, on the audio thread: set it up with the same lock held.
 *
 * Portable: no Haiku headers, so the host tests can use it.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#ifndef POWERPLAY_EQUALIZER_H
#define POWERPLAY_EQUALIZER_H

#include <cstddef>

namespace audio {

class Equalizer {
public:
	static const int kBands = 10;
	static const float kFrequencies[kBands];	// Hz
	static constexpr float kMaxGain = 12;		// dB, either way

								Equalizer();

			void				SetSampleRate(double rate);
			void				SetEnabled(bool enabled);
			bool				IsEnabled() const { return fEnabled; }
			// dB, -12..12
			void				SetPreamp(float gain);
			float				Preamp() const { return fPreamp; }
			void				SetBand(int band, float gain);
			float				Band(int band) const { return fGains[band]; }

			// Interleaved stereo, in place. Off, or flat, it leaves the
			// samples alone.
			void				Process(float* samples, size_t frames);

private:
	struct Biquad {
		float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
		float x1[2] = {0, 0}, x2[2] = {0, 0}, y1[2] = {0, 0}, y2[2] = {0, 0};
		bool active = false;
	};

			void				_Update(int band);

			double				fRate;
			bool				fEnabled;
			float				fPreamp;
			float				fGains[kBands];
			Biquad				fFilters[kBands];
};

}	// namespace audio

#endif	// POWERPLAY_EQUALIZER_H

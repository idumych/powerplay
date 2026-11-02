/*
 * PowerPlay — a 10-band graphic equalizer with a preamp.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#include "Equalizer.h"

#include <algorithm>
#include <cmath>

namespace audio {

const float Equalizer::kFrequencies[kBands] = {
	60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000, 16000
};

// About an octave and a half wide, so neighbouring bands blend smoothly
static const double kQ = 1.0;


Equalizer::Equalizer()
	:
	fRate(44100),
	fEnabled(false),
	fPreamp(0)
{
	for (float& gain : fGains)
		gain = 0;
}


void
Equalizer::SetSampleRate(double rate)
{
	if (rate <= 0 || rate == fRate)
		return;
	fRate = rate;
	for (int band = 0; band < kBands; band++)
		_Update(band);
}


void
Equalizer::SetEnabled(bool enabled)
{
	if (enabled && !fEnabled) {
		// Start the filters from silence, not from what they last saw
		for (Biquad& filter : fFilters) {
			for (int c = 0; c < 2; c++)
				filter.x1[c] = filter.x2[c] = filter.y1[c] = filter.y2[c] = 0;
		}
	}
	fEnabled = enabled;
}


void
Equalizer::SetPreamp(float gain)
{
	fPreamp = std::clamp(gain, -kMaxGain, kMaxGain);
}


void
Equalizer::SetBand(int band, float gain)
{
	if (band < 0 || band >= kBands)
		return;
	fGains[band] = std::clamp(gain, -kMaxGain, kMaxGain);
	_Update(band);
}


void
Equalizer::_Update(int band)
{
	Biquad& filter = fFilters[band];
	double frequency = kFrequencies[band];
	if (std::fabs(fGains[band]) < 0.01 || frequency >= fRate * 0.45) {
		filter.active = false;
		return;
	}
	// A peaking filter, from Robert Bristow-Johnson's cookbook
	double a = std::pow(10.0, fGains[band] / 40.0);
	double w0 = 2 * M_PI * frequency / fRate;
	double alpha = std::sin(w0) / (2 * kQ);
	double cosW0 = std::cos(w0);
	double a0 = 1 + alpha / a;
	filter.b0 = float((1 + alpha * a) / a0);
	filter.b1 = float(-2 * cosW0 / a0);
	filter.b2 = float((1 - alpha * a) / a0);
	filter.a1 = float(-2 * cosW0 / a0);
	filter.a2 = float((1 - alpha / a) / a0);
	if (!filter.active) {
		for (int c = 0; c < 2; c++)
			filter.x1[c] = filter.x2[c] = filter.y1[c] = filter.y2[c] = 0;
	}
	filter.active = true;
}


void
Equalizer::Process(float* samples, size_t frames)
{
	if (!fEnabled)
		return;
	float preamp = float(std::pow(10.0, fPreamp / 20.0));
	if (preamp != 1.0f) {
		for (size_t i = 0; i < frames * 2; i++)
			samples[i] *= preamp;
	}
	for (Biquad& f : fFilters) {
		if (!f.active)
			continue;
		for (size_t i = 0; i < frames; i++) {
			for (int c = 0; c < 2; c++) {
				float x = samples[i * 2 + c];
				float y = f.b0 * x + f.b1 * f.x1[c] + f.b2 * f.x2[c]
					- f.a1 * f.y1[c] - f.a2 * f.y2[c];
				f.x2[c] = f.x1[c];
				f.x1[c] = x;
				f.y2[c] = f.y1[c];
				if (std::fabs(y) < 1e-20f)
					y = 0;	// no denormals in a fading tail
				f.y1[c] = y;
				samples[i * 2 + c] = y;
			}
		}
	}
}

}	// namespace audio

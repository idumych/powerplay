/*
 * PowerPlay — VU meter ballistics and needle geometry.
 * Portable: no Haiku headers, so the host tests can use it.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#ifndef POWERPLAY_VU_METER_H
#define POWERPLAY_VU_METER_H

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace vu {

// Standard VU ballistics: the signal is full-wave rectified (an average-
// responding meter, calibrated for sines) and smoothed by two identical
// one-pole stages, a critically damped movement that reaches 99% of a step
// in 300 ms, both rising and falling.
//
// Deflection is calibrated so a sine reads its peak amplitude: a full-scale
// sine (0 dBFS) gives 1.0, which is +3 VU at the right end of the scale;
// -3 dBFS reads 0 VU (0.708), -6 dBFS 0.5, -20 dBFS 0.1.
class Ballistics {
public:
	static constexpr double kRiseTime = 0.300;	// 99% of a step, seconds

	explicit Ballistics(double sampleRate = 44100.0);

	void SetSampleRate(double sampleRate);
	double SampleRate() const { return fSampleRate; }
	void Reset() { fStage1 = fStage2 = 0.0; }

	// One sample, 1.0 = full scale.
	void Process(float sample)
	{
		double rectified = std::fabs(sample) * kAverageToPeak;
		fStage1 += fCoefficient * (rectified - fStage1);
		fStage2 += fCoefficient * (fStage1 - fStage2);
	}

	// Every stride'th sample of an interleaved buffer.
	void ProcessBlock(const float* samples, size_t frames, size_t stride);

	// Lets the needle fall for this long with no signal, without
	// processing silence sample by sample (exact for zero input).
	void Advance(double seconds);

	double Deflection() const { return fStage2; }

private:
	// The mean of |sin| is 2/pi of the peak.
	static constexpr double kAverageToPeak = M_PI / 2.0;

	double fSampleRate;
	double fCoefficient;
	double fStage1;
	double fStage2;
};


// Left and right meters fed from interleaved float audio.
class StereoMeter {
public:
	explicit StereoMeter(double sampleRate = 44100.0);

	void SetSampleRate(double sampleRate);
	void Reset();
	// A mono stream drives both needles.
	void ProcessInterleaved(const float* samples, size_t frames,
		size_t channels);
	void Advance(double seconds);

	double Left() const { return fLeft.Deflection(); }
	double Right() const { return fRight.Deflection(); }

private:
	Ballistics fLeft;
	Ballistics fRight;
};


// VU units (dB, 0 VU = 0.708 deflection) and deflection.
double DeflectionToVU(double deflection);
double VUToDeflection(double vu);


// One needle: a straight line from a pivot that sits below the visible
// face, so only its upper part shows. Angles are from vertical, negative
// to the left; deflection 0 rests at restAngle, 1.0 reaches fullAngle.
struct Needle {
	double pivotX;
	double pivotY;
	double length;
	double restAngle;	// radians
	double fullAngle;	// radians

	// The needle can't swing past its end stops.
	static constexpr double kMaxDeflection = 1.05;

	double Angle(double deflection) const;
	void Tip(double deflection, double& x, double& y) const;
};


// The Deskbar meter's face: 48 x 16 with a 1 px border, two needles side
// by side. Coordinates are pixel centres, as in a BView.
struct DeskbarFace {
	static constexpr float kWidth = 48;
	static constexpr float kHeight = 16;
	static constexpr double kNeedleSpacing = 22;

	static Needle LeftNeedle();
	static Needle RightNeedle();

	// Draws the face with both needles, scale times the size, into 32-bit
	// pixels 0xAARRGGBB (Haiku's B_RGB32). bytesPerRow may include padding.
	// Grey needles, antialiased by their coverage of each pixel, look the
	// same whatever the system's antialiasing settings are.
	static void Render(uint32_t* pixels, size_t bytesPerRow, int scale,
		double left, double right);
};

}	// namespace vu

#endif	// POWERPLAY_VU_METER_H

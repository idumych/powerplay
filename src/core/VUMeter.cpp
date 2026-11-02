/*
 * PowerPlay — VU meter ballistics and needle geometry.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#include "VUMeter.h"

#include <algorithm>

namespace vu {

// Two cascaded one-pole stages with time constant tau answer a step with
// 1 - (1 + t/tau) e^(-t/tau). That reaches 0.99 at t/tau = 6.6384.
static const double kStepTo99 = 6.6384;


Ballistics::Ballistics(double sampleRate, Response response)
	:
	fResponse(response),
	fStage1(0.0),
	fStage2(0.0)
{
	SetSampleRate(sampleRate);
}


void
Ballistics::SetSampleRate(double sampleRate)
{
	fSampleRate = sampleRate > 0 ? sampleRate : 44100.0;
	_Update();
}


void
Ballistics::SetResponse(Response response)
{
	fResponse = response;
	_Update();
}


static double
Coefficient(double timeConstant, double sampleRate)
{
	return 1.0 - std::exp(-1.0 / (timeConstant * sampleRate));
}


void
Ballistics::_Update()
{
	if (fResponse == kLively) {
		fScale = kLivelySensitivity;
		fAttack = Coefficient(kLivelyAttack, fSampleRate);
		fRelease = Coefficient(kLivelyRelease, fSampleRate);
		fNeedle = Coefficient(kLivelyInertia, fSampleRate);
	} else {
		// Two identical stages, rising and falling alike
		fScale = kAverageToPeak * kSensitivity;
		fAttack = fRelease = fNeedle
			= Coefficient(kRiseTime / kStepTo99, fSampleRate);
	}
}


void
Ballistics::ProcessBlock(const float* samples, size_t frames, size_t stride)
{
	for (size_t i = 0; i < frames; i++)
		Process(samples[i * stride]);
}


void
Ballistics::Advance(double seconds)
{
	if (seconds <= 0)
		return;
	// With zero input, after n samples, where stage 1 falls by
	// dx = 1 - release a sample and the needle follows the already updated
	// stage 1 with c = needle (dy = 1 - c):
	//   stage1 = dx^n stage1
	//   stage2 = dy^n stage2 + c stage1 dx (dx^n - dy^n) / (dx - dy)
	// which is dy^n (stage2 + n c stage1) when dx = dy (the VU's stages).
	double n = seconds * fSampleRate;
	double dx = 1.0 - fRelease;
	double dy = 1.0 - fNeedle;
	double dxn = std::pow(dx, n);
	double dyn = std::pow(dy, n);
	double follow;
	if (std::fabs(dx - dy) < 1e-12)
		follow = n * fNeedle * dyn * fStage1;
	else
		follow = fNeedle * fStage1 * dx * (dxn - dyn) / (dx - dy);
	fStage2 = dyn * fStage2 + follow;
	fStage1 = dxn * fStage1;
}


//	#pragma mark - StereoMeter


StereoMeter::StereoMeter(double sampleRate)
	:
	fLeft(sampleRate),
	fRight(sampleRate)
{
}


void
StereoMeter::SetSampleRate(double sampleRate)
{
	fLeft.SetSampleRate(sampleRate);
	fRight.SetSampleRate(sampleRate);
}


void
StereoMeter::SetResponse(Ballistics::Response response)
{
	fLeft.SetResponse(response);
	fRight.SetResponse(response);
}


void
StereoMeter::Reset()
{
	fLeft.Reset();
	fRight.Reset();
}


void
StereoMeter::ProcessInterleaved(const float* samples, size_t frames,
	size_t channels)
{
	if (channels == 0)
		return;
	fLeft.ProcessBlock(samples, frames, channels);
	fRight.ProcessBlock(channels > 1 ? samples + 1 : samples, frames,
		channels);
}


void
StereoMeter::Advance(double seconds)
{
	fLeft.Advance(seconds);
	fRight.Advance(seconds);
}


//	#pragma mark - Scale


double
DeflectionToVU(double deflection)
{
	if (deflection <= 0)
		return -INFINITY;
	return 20.0 * std::log10(deflection) + 3.0;
}


double
VUToDeflection(double vu)
{
	return std::pow(10.0, (vu - 3.0) / 20.0);
}


//	#pragma mark - Needle


double
Needle::Angle(double deflection) const
{
	deflection = std::clamp(deflection, 0.0, kMaxDeflection);
	return restAngle + (fullAngle - restAngle) * deflection;
}


void
Needle::Tip(double deflection, double& x, double& y) const
{
	double angle = Angle(deflection);
	x = pivotX + length * std::sin(angle);
	y = pivotY - length * std::cos(angle);
}


// Fitted to the original at rest (least squares over the needle's
// darkness-weighted centre in each row): the needle crosses the face at
// 28.7 degrees left of vertical, through x = 12.9 at y = 23, and its tip
// ends between rows 3 and 4. A pivot 8 px below the face fits that line;
// the scale is taken as symmetric about vertical.
static const double kRestAngle = -28.7 * M_PI / 180.0;
static const double kPivotX = 12.9;
static const double kPivotY = 23.0;
static const double kTipY = 3.5;
static const double kLength = (kPivotY - kTipY) / std::cos(kRestAngle);


Needle
DeskbarFace::LeftNeedle()
{
	return Needle{kPivotX, kPivotY, kLength, kRestAngle, -kRestAngle};
}


Needle
DeskbarFace::RightNeedle()
{
	Needle needle = LeftNeedle();
	needle.pivotX += kNeedleSpacing;
	return needle;
}


// Colours as the original showed them on a 16-bit (RGB565) screen, hence
// the green tint: black border, light grey face, and needles that darken
// the face as much as a 1 px line of the needle colour would.
struct Color { double r, g, b; };
static const Color kBorder = {0, 0, 0};
static const Color kFace = {208, 212, 208};
static const Color kNeedle = {116, 120, 116};


static inline uint32_t
Pixel(const Color& color)
{
	auto channel = [](double v) {
		return uint32_t(std::lround(std::clamp(v, 0.0, 255.0)));
	};
	return 0xff000000u | (channel(color.r) << 16) | (channel(color.g) << 8)
		| channel(color.b);
}


static inline Color
Mix(const Color& a, const Color& b, double amount)
{
	return Color{a.r + (b.r - a.r) * amount, a.g + (b.g - a.g) * amount,
		a.b + (b.b - a.b) * amount};
}


// How much of the device pixel whose centre is at face coordinates (x, y)
// the needle covers: a line one face pixel wide, ending at its tip.
static double
Coverage(const Needle& needle, double deflection, double x, double y,
	int scale)
{
	double tipX, tipY;
	needle.Tip(deflection, tipX, tipY);
	double dx = (tipX - needle.pivotX) / needle.length;
	double dy = (tipY - needle.pivotY) / needle.length;
	double px = x - needle.pivotX;
	double py = y - needle.pivotY;
	double along = px * dx + py * dy;			// from the pivot
	double across = std::fabs(px * dy - py * dx);	// from the line
	// Distances in device pixels; the line is `scale` device pixels wide.
	double coverage = std::clamp(0.5 * (scale + 1) - across * scale, 0.0, 1.0);
	double end = std::clamp(0.5 + (needle.length - along) * scale, 0.0, 1.0);
	return coverage * end;
}


void
DeskbarFace::Render(uint32_t* pixels, size_t bytesPerRow, int scale,
	double left, double right)
{
	if (scale < 1)
		scale = 1;
	const int width = int(kWidth) * scale;
	const int height = int(kHeight) * scale;
	const Needle needles[2] = {LeftNeedle(), RightNeedle()};
	const double levels[2] = {left, right};

	for (int row = 0; row < height; row++) {
		uint32_t* line = (uint32_t*)((uint8_t*)pixels + row * bytesPerRow);
		for (int column = 0; column < width; column++) {
			if (row < scale || row >= height - scale || column < scale
				|| column >= width - scale) {
				line[column] = Pixel(kBorder);
				continue;
			}
			// The device pixel's centre in face coordinates, where face
			// pixel centres are whole numbers.
			double x = (column + 0.5) / scale - 0.5;
			double y = (row + 0.5) / scale - 0.5;
			double covered = 0;
			for (int i = 0; i < 2; i++) {
				covered = std::max(covered,
					Coverage(needles[i], levels[i], x, y, scale));
			}
			line[column] = Pixel(Mix(kFace, kNeedle, covered));
		}
	}
}

}	// namespace vu

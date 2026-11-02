/*
 * PowerPlay — host tests for the portable core.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#include "VUMeter.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int sFailures = 0;

#define CHECK(cond, ...) do { \
	if (!(cond)) { \
		std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
		std::printf(__VA_ARGS__); \
		std::printf("\n"); \
		sFailures++; \
	} \
} while (0)

static const double kRate = 44100.0;


// Interleaved stereo 1 kHz sine at dbfs on the left, and on the right too
// unless leftOnly.
static std::vector<float>
Tone(double dbfs, double seconds, bool leftOnly = false)
{
	size_t frames = size_t(seconds * kRate);
	std::vector<float> buffer(frames * 2);
	double amplitude = std::pow(10.0, dbfs / 20.0);
	for (size_t i = 0; i < frames; i++) {
		float s = float(amplitude * std::sin(2 * M_PI * 1000.0 * i / kRate));
		buffer[i * 2] = s;
		buffer[i * 2 + 1] = leftOnly ? 0.0f : s;
	}
	return buffer;
}


// Tones read where they should: an EBU alignment tone (-18 dBFS RMS, a
// -15 dBFS sine) at 0 VU, and the rest in step.
static void
TestCalibration()
{
	const double levels[] = {-15, -18, -21, -25, -35};
	for (double dbfs : levels) {
		vu::StereoMeter meter(kRate);
		std::vector<float> tone = Tone(dbfs, 1.0);
		meter.ProcessInterleaved(tone.data(), tone.size() / 2, 2);
		double expected = std::pow(10.0, dbfs / 20.0)
			* vu::Ballistics::kSensitivity;
		CHECK(std::fabs(meter.Left() - expected) < 0.01 * expected,
			"%g dBFS reads %.4f, expected %.4f", dbfs, meter.Left(), expected);
		CHECK(std::fabs(vu::DeflectionToVU(meter.Left()) - (dbfs + 15)) < 0.1,
			"%g dBFS reads %.2f VU", dbfs, vu::DeflectionToVU(meter.Left()));
	}
	CHECK(std::fabs(vu::VUToDeflection(0) - 0.7079) < 0.001, "0 VU position");
	CHECK(std::fabs(20 * std::log10(vu::Ballistics::kSensitivity) - 12) < 1e-6,
		"12 dB of sensitivity");
}


// 99% of a step in 300 ms, rising and falling, with no overshoot.
static void
TestRiseAndFall()
{
	vu::Ballistics meter(kRate);
	std::vector<float> tone = Tone(-15, 0.6);		// reads 0 VU
	const double target = vu::VUToDeflection(0);
	double crossed = -1, peak = 0;
	for (size_t i = 0; i < tone.size() / 2; i++) {
		meter.Process(tone[i * 2]);
		peak = std::max(peak, meter.Deflection());
		if (crossed < 0 && meter.Deflection() >= 0.99 * target)
			crossed = i / kRate;
	}
	CHECK(std::fabs(crossed - 0.300) < 0.005, "rise to 99%% took %.4f s",
		crossed);
	CHECK(peak < 1.01 * target, "overshoot to %.4f", peak);

	double start = meter.Deflection();
	crossed = -1;
	for (int i = 0; i < int(0.6 * kRate); i++) {
		meter.Process(0.0f);
		if (crossed < 0 && meter.Deflection() <= start * 0.01)
			crossed = i / kRate;
	}
	CHECK(std::fabs(crossed - 0.300) < 0.005, "fall to 1%% took %.4f s",
		crossed);
}


// A steady tone holds the needle still (no visible ripple).
static void
TestRipple()
{
	vu::Ballistics meter(kRate);
	std::vector<float> tone = Tone(-18, 1.0);
	double low = 2, high = 0;
	for (size_t i = 0; i < tone.size() / 2; i++) {
		meter.Process(tone[i * 2]);
		if (i > kRate * 0.8) {
			low = std::min(low, meter.Deflection());
			high = std::max(high, meter.Deflection());
		}
	}
	CHECK(high - low < 0.001, "ripple %.5f", high - low);
}


static void
TestChannels()
{
	vu::StereoMeter meter(kRate);
	std::vector<float> tone = Tone(-18, 1.0, true);	// reads 0.5
	meter.ProcessInterleaved(tone.data(), tone.size() / 2, 2);
	CHECK(std::fabs(meter.Left() - 0.5) < 0.005, "left %.4f", meter.Left());
	CHECK(meter.Right() == 0.0, "right %.4f on a left-only tone",
		meter.Right());

	// Mono drives both needles.
	vu::StereoMeter mono(kRate);
	std::vector<float> samples(44100);
	for (size_t i = 0; i < samples.size(); i++)
		samples[i] = float(0.5 / vu::Ballistics::kSensitivity
			* std::sin(2 * M_PI * 1000.0 * i / kRate));
	mono.ProcessInterleaved(samples.data(), samples.size(), 1);
	CHECK(std::fabs(mono.Left() - mono.Right()) < 1e-12
		&& std::fabs(mono.Left() - 0.5) < 0.005, "mono %.4f %.4f",
		mono.Left(), mono.Right());
}


// Advance() matches processing the same stretch of silence.
static void
TestAdvance()
{
	const double times[] = {0.001, 0.05, 0.2, 1.0};
	for (double seconds : times) {
		vu::Ballistics a(kRate), b(kRate);
		std::vector<float> tone = Tone(0, 0.15);
		for (size_t i = 0; i < tone.size() / 2; i++) {
			a.Process(tone[i * 2]);
			b.Process(tone[i * 2]);
		}
		int n = int(std::lround(seconds * kRate));
		for (int i = 0; i < n; i++)
			a.Process(0.0f);
		b.Advance(n / kRate);
		CHECK(std::fabs(a.Deflection() - b.Deflection()) < 1e-9,
			"after %g s: processed %.9f, advanced %.9f", seconds,
			a.Deflection(), b.Deflection());
	}
}


// The lively needle: reads a -12 dBFS sine at 0 VU, is quick up and back,
// and kicks on a short beat where a VU needle barely stirs.
static void
TestLively()
{
	const double frequencies[] = {1000, 100};
	for (double frequency : frequencies) {
		vu::Ballistics meter(kRate, vu::Ballistics::kLively);
		double amplitude = std::pow(10.0, -12 / 20.0);
		for (int i = 0; i < int(kRate); i++) {
			meter.Process(float(amplitude
				* std::sin(2 * M_PI * frequency * i / kRate)));
		}
		CHECK(std::fabs(vu::DeflectionToVU(meter.Deflection())) < 0.5,
			"lively: a -12 dBFS sine at %g Hz reads %+.2f VU", frequency,
			vu::DeflectionToVU(meter.Deflection()));
	}

	// Up and back
	vu::Ballistics meter(kRate, vu::Ballistics::kLively);
	std::vector<float> tone = Tone(-12, 0.5);
	double target = vu::VUToDeflection(0);
	double up = -1;
	for (size_t i = 0; i < tone.size() / 2; i++) {
		meter.Process(tone[i * 2]);
		if (up < 0 && meter.Deflection() >= 0.9 * target)
			up = i / kRate;
	}
	CHECK(up > 0 && up < 0.060, "lively: 90%% in %.3f s", up);
	double start = meter.Deflection(), down = -1;
	for (int i = 0; i < int(kRate); i++) {
		meter.Process(0.0f);
		if (down < 0 && meter.Deflection() <= 0.1 * start)
			down = i / kRate;
	}
	CHECK(down > 0.2 && down < 0.45, "lively: back to 10%% in %.3f s", down);

	// A 20 ms beat
	double kick[2];
	const vu::Ballistics::Response responses[] = {vu::Ballistics::kVU,
		vu::Ballistics::kLively};
	for (int r = 0; r < 2; r++) {
		vu::Ballistics beat(kRate, responses[r]);
		std::vector<float> burst = Tone(-12, 0.020);
		double peak = 0;
		for (size_t i = 0; i < burst.size() / 2; i++) {
			beat.Process(burst[i * 2]);
			peak = std::max(peak, beat.Deflection());
		}
		for (int i = 0; i < int(kRate * 0.2); i++) {
			beat.Process(0.0f);
			peak = std::max(peak, beat.Deflection());
		}
		kick[r] = peak / target;
	}
	CHECK(kick[1] > 0.6 && kick[0] < 0.3,
		"a 20 ms beat: VU to %.0f%%, lively to %.0f%% of its full reading",
		kick[0] * 100, kick[1] * 100);

	// Advance() is exact here too.
	const double times[] = {0.002, 0.03, 0.2};
	for (double seconds : times) {
		vu::Ballistics a(kRate, vu::Ballistics::kLively);
		vu::Ballistics b(kRate, vu::Ballistics::kLively);
		std::vector<float> loud = Tone(-6, 0.1);
		for (size_t i = 0; i < loud.size() / 2; i++) {
			a.Process(loud[i * 2]);
			b.Process(loud[i * 2]);
		}
		int n = int(std::lround(seconds * kRate));
		for (int i = 0; i < n; i++)
			a.Process(0.0f);
		b.Advance(n / kRate);
		CHECK(std::fabs(a.Deflection() - b.Deflection()) < 1e-9,
			"lively, after %g s: processed %.9f, advanced %.9f", seconds,
			a.Deflection(), b.Deflection());
	}
}


// The needles sit where the original's do at rest, and swing symmetrically.
static void
TestNeedles()
{
	vu::Needle left = vu::DeskbarFace::LeftNeedle();
	vu::Needle right = vu::DeskbarFace::RightNeedle();
	double x, y;
	left.Tip(0, x, y);
	CHECK(std::fabs(y - 3.5) < 0.01 && std::fabs(x - 2.2) < 0.2,
		"left needle at rest ends at %.2f,%.2f", x, y);
	double rx, ry;
	right.Tip(0, rx, ry);
	CHECK(std::fabs(rx - x - 22) < 1e-9 && ry == y, "right needle offset");
	// The resting needle crosses the bottom row near x = 8.
	double bottomX = left.pivotX + (left.pivotY - 14) * std::tan(left.restAngle);
	CHECK(std::fabs(bottomX - 8.0) < 0.1, "crosses the bottom row at %.2f",
		bottomX);

	left.Tip(0.5, x, y);
	CHECK(std::fabs(x - left.pivotX) < 1e-9, "half scale is vertical");
	CHECK(std::fabs(left.Angle(1.0) + left.Angle(0.0)) < 1e-12,
		"full scale mirrors rest");
	CHECK(left.Angle(5.0) == left.Angle(vu::Needle::kMaxDeflection)
		&& left.Angle(-1.0) == left.Angle(0.0), "end stops");
}


// The rendered face at rest has the original's shape: border, face, and
// one dark pixel pair per row along the measured needle line.
static void
TestRender()
{
	const int w = 48, h = 16;
	std::vector<uint32_t> pixels(w * h);
	vu::DeskbarFace::Render(pixels.data(), w * 4, 1, 0, 0);
	auto grey = [&](int x, int y) {
		uint32_t p = pixels[y * w + x];
		return (int((p >> 16) & 0xff) + int((p >> 8) & 0xff) + int(p & 0xff))
			/ 3.0;
	};
	CHECK(grey(0, 0) == 0 && grey(47, 15) == 0 && grey(20, 0) == 0
		&& grey(0, 8) == 0, "border");
	CHECK(pixels[8 * w + 20] == 0xffd0d4d0 && pixels[2 * w + 45] == 0xffd0d4d0,
		"face %08x", pixels[8 * w + 20]);
	CHECK((pixels[0] >> 24) == 0xff, "opaque");

	// Where the original's needles cross each row (darkness-weighted
	// centre, from vm/ref/deskbarvu-idle.png), and how dark they make it.
	const double originalX[] = {0, 0, 0, 0, 2.54, 3.09, 3.62, 4.17, 4.70,
		5.24, 5.82, 6.38, 6.91, 7.46, 7.99};
	const double face = (208 + 212 + 208) / 3.0;
	for (int y = 4; y <= 14; y++) {
		double sum = 0, weighted = 0;
		for (int x = 1; x < 23; x++) {
			double dark = face - grey(x, y);
			sum += dark;
			weighted += dark * x;
		}
		CHECK(std::fabs(weighted / sum - originalX[y]) < 0.15,
			"row %d: needle at %.2f, original at %.2f", y, weighted / sum,
			originalX[y]);
		CHECK(std::fabs(sum - 105) < 12, "row %d darkness %.0f, original 105",
			y, sum);
		// The right needle is the left one moved 22 px.
		for (int x = 1; x < 23; x++)
			CHECK(grey(x, y) == grey(x + 22, y), "row %d: right needle", y);
	}
	// Nothing visible above the tips.
	for (int x = 1; x < 47; x++)
		CHECK(grey(x, 2) == face && face - grey(x, 3) < 5, "above the tip at %d",
			x);

	// Scaled, the border and the needle scale with it.
	std::vector<uint32_t> big(w * 2 * h * 2);
	vu::DeskbarFace::Render(big.data(), w * 2 * 4, 2, 0.5, 0.5);
	auto bigGrey = [&](int x, int y) { return int(big[y * w * 2 + x] & 0xff); };
	CHECK(bigGrey(1, 1) == 0 && bigGrey(2, 2) == 208, "scaled border");
	// Half scale is vertical: at 2x the needle is 2 device pixels wide,
	// centred on the pivot (device x = 2 * pivotX + 0.5).
	double centre = 0, darkness = 0;
	for (int x = 2; x < 46; x++) {
		double dark = 208 - bigGrey(x, 20);
		darkness += dark;
		centre += dark * (x + 0.5);
	}
	centre /= darkness;
	CHECK(std::fabs(centre - (2 * vu::DeskbarFace::LeftNeedle().pivotX + 1)) < 0.05
		&& std::fabs(darkness - 2 * (208 - 116)) < 1,
		"vertical needle at 2x: centre %.2f darkness %.0f", centre, darkness);
}


int
main()
{
	TestCalibration();
	TestRiseAndFall();
	TestRipple();
	TestChannels();
	TestAdvance();
	TestLively();
	TestNeedles();
	TestRender();
	if (sFailures == 0)
		std::printf("all core tests passed\n");
	return sFailures == 0 ? 0 : 1;
}

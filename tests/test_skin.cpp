/*
 * PowerPlay — host tests for zip and BMP reading and Winamp skins, with
 * skins made here (no one's artwork).
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#include "Archive.h"
#include "WinampSkin.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include <zlib.h>

static int sFailures = 0;

#define CHECK(cond, ...) do { \
	if (!(cond)) { \
		std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
		std::printf(__VA_ARGS__); \
		std::printf("\n"); \
		sFailures++; \
	} \
} while (0)

using skin::Image;
using skin::State;
using skin::WinampSkin;


static void
Put16(std::vector<uint8_t>& b, uint16_t v)
{
	b.push_back(v & 0xff);
	b.push_back(v >> 8);
}


static void
Put32(std::vector<uint8_t>& b, uint32_t v)
{
	for (int i = 0; i < 4; i++)
		b.push_back((v >> (8 * i)) & 0xff);
}


// A BMP of width x height at bpp (4, 8: a palette of the colours used; 24),
// bottom-up unless topDown.
static std::vector<uint8_t>
MakeBMP(int width, int height, int bpp,
	std::function<uint32_t(int, int)> pixel, bool topDown = false)
{
	std::vector<uint32_t> palette;
	auto index = [&](uint32_t color) {
		for (size_t i = 0; i < palette.size(); i++)
			if (palette[i] == color)
				return int(i);
		palette.push_back(color);
		return int(palette.size() - 1);
	};
	std::vector<std::vector<int>> indices;
	if (bpp <= 8) {
		for (int y = 0; y < height; y++) {
			indices.emplace_back();
			for (int x = 0; x < width; x++)
				indices.back().push_back(index(pixel(x, y)));
		}
	}
	int paletteSize = bpp <= 8 ? (1 << bpp) : 0;
	size_t rowBytes = ((size_t(width) * bpp + 31) / 32) * 4;
	uint32_t offset = 14 + 40 + paletteSize * 4;
	std::vector<uint8_t> b = {'B', 'M'};
	Put32(b, offset + uint32_t(rowBytes * height));
	Put32(b, 0);
	Put32(b, offset);
	Put32(b, 40);
	Put32(b, width);
	Put32(b, uint32_t(topDown ? -height : height));
	Put16(b, 1);
	Put16(b, bpp);
	Put32(b, 0);
	Put32(b, uint32_t(rowBytes * height));
	Put32(b, 2835);
	Put32(b, 2835);
	Put32(b, paletteSize);
	Put32(b, 0);
	for (int i = 0; i < paletteSize; i++) {
		uint32_t c = i < (int)palette.size() ? palette[i] : 0;
		b.push_back(c & 0xff);
		b.push_back((c >> 8) & 0xff);
		b.push_back((c >> 16) & 0xff);
		b.push_back(0);
	}
	for (int r = 0; r < height; r++) {
		int y = topDown ? r : height - 1 - r;
		std::vector<uint8_t> row(rowBytes, 0);
		for (int x = 0; x < width; x++) {
			if (bpp == 24) {
				uint32_t c = pixel(x, y);
				row[x * 3] = c & 0xff;
				row[x * 3 + 1] = (c >> 8) & 0xff;
				row[x * 3 + 2] = (c >> 16) & 0xff;
			} else if (bpp == 8) {
				row[x] = uint8_t(indices[y][x]);
			} else {
				row[x / 2] |= uint8_t(indices[y][x] << (x % 2 == 0 ? 4 : 0));
			}
		}
		b.insert(b.end(), row.begin(), row.end());
	}
	return b;
}


// A zip of the given files, deflated or stored.
static std::vector<uint8_t>
MakeZip(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files,
	bool compress)
{
	std::vector<uint8_t> zip, directory;
	for (const auto& file : files) {
		std::vector<uint8_t> data = file.second;
		uint16_t method = 0;
		if (compress) {
			z_stream stream;
			std::memset(&stream, 0, sizeof(stream));
			deflateInit2(&stream, 9, Z_DEFLATED, -MAX_WBITS, 8,
				Z_DEFAULT_STRATEGY);
			std::vector<uint8_t> out(deflateBound(&stream, data.size()));
			stream.next_in = const_cast<Bytef*>(file.second.data());
			stream.avail_in = uInt(file.second.size());
			stream.next_out = out.data();
			stream.avail_out = uInt(out.size());
			deflate(&stream, Z_FINISH);
			out.resize(stream.total_out);
			deflateEnd(&stream);
			data = out;
			method = 8;
		}
		uint32_t crc = uint32_t(crc32(0, file.second.data(),
			uInt(file.second.size())));
		uint32_t offset = uint32_t(zip.size());
		Put32(zip, 0x04034b50);
		Put16(zip, 20); Put16(zip, 0); Put16(zip, method);
		Put16(zip, 0); Put16(zip, 0);
		Put32(zip, crc);
		Put32(zip, uint32_t(data.size()));
		Put32(zip, uint32_t(file.second.size()));
		Put16(zip, uint16_t(file.first.size())); Put16(zip, 0);
		zip.insert(zip.end(), file.first.begin(), file.first.end());
		zip.insert(zip.end(), data.begin(), data.end());

		Put32(directory, 0x02014b50);
		Put16(directory, 20); Put16(directory, 20); Put16(directory, 0);
		Put16(directory, method); Put16(directory, 0); Put16(directory, 0);
		Put32(directory, crc);
		Put32(directory, uint32_t(data.size()));
		Put32(directory, uint32_t(file.second.size()));
		Put16(directory, uint16_t(file.first.size()));
		Put16(directory, 0); Put16(directory, 0); Put16(directory, 0);
		Put16(directory, 0); Put32(directory, 0);
		Put32(directory, offset);
		directory.insert(directory.end(), file.first.begin(),
			file.first.end());
	}
	uint32_t directoryOffset = uint32_t(zip.size());
	zip.insert(zip.end(), directory.begin(), directory.end());
	Put32(zip, 0x06054b50);
	Put16(zip, 0); Put16(zip, 0);
	Put16(zip, uint16_t(files.size())); Put16(zip, uint16_t(files.size()));
	Put32(zip, uint32_t(directory.size()));
	Put32(zip, directoryOffset);
	Put16(zip, 0);
	return zip;
}


static void
TestBMP()
{
	auto pattern = [](int x, int y) {
		return 0xff000000u | uint32_t((x * 40) << 16) | uint32_t((y * 60) << 8)
			| uint32_t((x + y) % 2 ? 0x80 : 0x10);
	};
	const int bpps[] = {24, 8, 4};
	for (int bpp : bpps) {
		for (int topDown = 0; topDown < 2; topDown++) {
			// 4 bits hold 16 colours: a pattern of few colours there
			std::function<uint32_t(int, int)> pixel = pattern;
			if (bpp == 4) {
				pixel = [](int x, int y) {
					return 0xff000000u | uint32_t(((x + 3 * y) % 7) * 0x202020);
				};
			}
			Image image;
			bool ok = skin::DecodeBMP(MakeBMP(5, 3, bpp, pixel, topDown),
				image);
			bool same = ok && image.width == 5 && image.height == 3;
			for (int y = 0; same && y < 3; y++)
				for (int x = 0; x < 5; x++)
					same = same && image.At(x, y) == pixel(x, y);
			CHECK(same, "%d-bit BMP, %s", bpp, topDown ? "top-down" : "bottom-up");
		}
	}
	Image image;
	CHECK(!skin::DecodeBMP(std::vector<uint8_t>{'B', 'M', 0, 0}, image),
		"a truncated BMP");
}


static void
TestZip()
{
	std::vector<uint8_t> hello = {'h', 'e', 'l', 'l', 'o'};
	std::vector<uint8_t> big(5000, 'x');
	for (int deflate = 0; deflate < 2; deflate++) {
		skin::ZipArchive archive;
		CHECK(archive.Open(MakeZip({{"a/hello.txt", hello}, {"big", big}},
			deflate)), "open (%s)", deflate ? "deflated" : "stored");
		std::vector<uint8_t> out;
		CHECK(archive.Extract("a/hello.txt", out) && out == hello,
			"extract hello");
		CHECK(archive.Extract("big", out) && out == big, "extract big");
		CHECK(!archive.Extract("missing", out), "missing file");
		CHECK(archive.Names().size() == 2, "names");
	}
	skin::ZipArchive archive;
	CHECK(!archive.Open(std::vector<uint8_t>(100, 0)), "not a zip");
}


static std::vector<uint8_t>
Bytes(const char* text)
{
	return std::vector<uint8_t>(text, text + strlen(text));
}


// Colours that say where a pixel came from
enum : uint32_t {
	kMain = 0xff102030,
	kTitleActive = 0xff200000, kTitleInactive = 0xff210000,
	kButtonUp = 0xff000100, kButtonDown = 0xff000200,
	kStatusPlaying = 0xff000010, kStatusPaused = 0xff000011,
	kStatusStopped = 0xff000012,
	kPosGroove = 0xff300000, kPosThumb = 0xff310000,
	kPosThumbDown = 0xff320000,
	kShuffleOff = 0xff400000, kShuffleOn = 0xff410000,
	kRepeatOff = 0xff420000, kRepeatOn = 0xff430000,
};


static uint32_t
Digit(int d) { return 0xff500000u | uint32_t(d); }
static uint32_t
VolumeFrame(int f) { return 0xff600000u | uint32_t(f); }


// A skin with each part a colour of its own, in the formats skins use.
static std::vector<std::pair<std::string, std::vector<uint8_t>>>
TestSkinFiles(const char* folder)
{
	std::string f = folder;
	return {
		{f + "Main.BMP", MakeBMP(275, 116, 8,
			[](int, int) -> uint32_t { return kMain; })},
		{f + "titlebar.bmp", MakeBMP(344, 87, 24, [](int x, int y) -> uint32_t {
			return y >= 15 && y < 29 && x >= 27 ? kTitleInactive : kTitleActive;
		})},
		{f + "CBUTTONS.bmp", MakeBMP(136, 36, 4, [](int x, int y) -> uint32_t {
			return (x < 114 ? y >= 18 : y >= 16) ? kButtonDown : kButtonUp;
		})},
		{f + "playpaus.bmp", MakeBMP(42, 9, 8, [](int x, int) -> uint32_t {
			return x < 9 ? kStatusPlaying : x < 18 ? kStatusPaused
				: kStatusStopped;
		})},
		{f + "posbar.bmp", MakeBMP(307, 10, 24, [](int x, int) -> uint32_t {
			return x < 248 ? kPosGroove : x < 278 ? kPosThumb : kPosThumbDown;
		})},
		{f + "shufrep.bmp", MakeBMP(92, 85, 24, [](int x, int y) -> uint32_t {
			if (y < 60) {
				bool on = y >= 30;
				if (x >= 28)
					return on ? kShuffleOn : kShuffleOff;
				return on ? kRepeatOn : kRepeatOff;
			}
			return 0xff777777u;
		})},
		{f + "numbers.bmp", MakeBMP(99, 13, 24, [](int x, int) {
			return Digit(x / 9);
		})},
		{f + "volume.bmp", MakeBMP(68, 433, 24, [](int, int y) -> uint32_t {
			return y < 420 ? VolumeFrame(y / 15) : 0xff999999u;
		})},
		{f + "viscolor.txt", Bytes("9,8,7, // background\n1,2,3\n")},
		{f + "readme.txt", {'h', 'i'}},
	};
}


static void
TestSkin()
{
	for (int deflate = 0; deflate < 2; deflate++) {
		WinampSkin s;
		// Mixed case and a folder around the files, as real skins have
		CHECK(s.LoadArchive(MakeZip(TestSkinFiles("My Skin/"), deflate)),
			"load the test skin");
		CHECK(s.HasImage("main") && s.HasImage("cbuttons")
			&& !s.HasImage("balance"), "found the images by name");
		CHECK(s.VisColors()[0] == 0xff090807 && s.VisColors()[1] == 0xff010203
			&& s.VisColors().size() == 24, "viscolor.txt, then defaults");

		std::vector<uint32_t> px(skin::kMainWidth * skin::kMainHeight);
		auto at = [&](int x, int y) { return px[y * skin::kMainWidth + x]; };

		State state;
		s.Render(state, px.data(), skin::kMainWidth * 4);
		CHECK(at(140, 50) == kMain, "main.bmp underneath");
		CHECK(at(100, 5) == kTitleActive, "active title bar");
		CHECK(at(30, 30) == kStatusStopped, "stopped");
		CHECK(at(20, 95) == kButtonUp, "previous, up");
		CHECK(at(60, 75) == kPosGroove, "position groove");
		CHECK(at(55, 30) == kMain, "no time without a file");
		CHECK(at(170, 95) == kShuffleOff && at(215, 95) == kRepeatOff,
			"shuffle and repeat off");
		CHECK(at(30, 47) == kMain, "visualizer blank when stopped");

		state.playback = State::kPlaying;
		state.active = false;
		state.hasFile = true;
		state.seconds = 125;		// 02:05
		state.position = 0;
		state.shuffle = true;
		state.volume = 0.5;
		state.levelLeft = 1;
		state.pressed = skin::kPlay;
		s.Render(state, px.data(), skin::kMainWidth * 4);
		CHECK(at(100, 5) == kTitleInactive, "inactive title bar");
		CHECK(at(30, 30) == kStatusPlaying, "playing");
		CHECK(at(45, 95) == kButtonDown && at(20, 95) == kButtonUp,
			"play pressed, previous not");
		CHECK(at(50, 30) == Digit(0) && at(62, 30) == Digit(2)
			&& at(80, 30) == Digit(0) && at(92, 30) == Digit(5),
			"02:05 from numbers.bmp: %08x %08x %08x %08x", at(50, 30),
			at(62, 30), at(80, 30), at(92, 30));
		CHECK(at(20, 75) == kPosThumb, "position thumb at the start");
		CHECK(at(170, 95) == kShuffleOn, "shuffle on");
		CHECK(at(165, 60) == VolumeFrame(14), "volume 0.5: frame 14, %08x",
			at(165, 60));
		CHECK(at(26, 46) != kMain && at(26, 46) != s.VisColors()[0],
			"the left level lights the visualizer");

		state.seconds = -125;
		state.position = 1;
		state.pressed = skin::kPosition;
		s.Render(state, px.data(), skin::kMainWidth * 4);
		CHECK(at(41, 32) == Digit(2), "a minus for time remaining");
		CHECK(at(255, 75) == kPosThumbDown, "position thumb at the end, held");
	}

	WinampSkin none;
	CHECK(!none.LoadArchive(MakeZip({{"readme.txt", {'x'}}}, true)),
		"no main.bmp: not a skin");
}


static void
TestLayout()
{
	CHECK(skin::HitTest(20, 95) == skin::kPrevious, "previous");
	CHECK(skin::HitTest(140, 95) == skin::kEject, "eject");
	CHECK(skin::HitTest(268, 6) == skin::kCloseButton, "close");
	CHECK(skin::HitTest(150, 6) == skin::kTitleBar, "title bar");
	CHECK(skin::HitTest(100, 76) == skin::kPosition, "position");
	CHECK(skin::HitTest(140, 110) == skin::kNoControl, "background");

	CHECK(skin::SliderValue(skin::kVolume, 107) == 0
		&& skin::SliderValue(skin::kVolume, 175) == 1, "volume ends");
	CHECK(skin::SliderValue(skin::kBalance, 177 + 19) == 0, "balance middle");
	CHECK(skin::SliderValue(skin::kBalance, 177) == -1
		&& skin::SliderValue(skin::kBalance, 215) == 1, "balance ends");
	double half = skin::SliderValue(skin::kPosition, 16 + 124);
	CHECK(half > 0.49 && half < 0.51, "position middle %.3f", half);

	std::vector<uint32_t> colors = skin::ParseVisColors("");
	CHECK(colors.size() == 24 && colors[0] == 0xff000000, "default colours");
}


// PowerPlay's own skin (data/PowerStation.wsz): it loads, has every sheet
// the main window uses, and draws its time digits where they belong.
static void
TestBuiltInSkin()
{
	skin::WinampSkin skin;
	if (!skin.Load("data/PowerStation.wsz")) {
		CHECK(false, "the built-in skin loads");
		return;
	}
	for (const char* name : {"main", "titlebar", "cbuttons", "numbers", "text",
			"playpaus", "monoster", "volume", "balance", "posbar", "shufrep"})
		CHECK(skin.HasImage(name), "the built-in skin has %s.bmp", name);
	skin::State stopped, playing;
	playing.playback = skin::State::kPlaying;
	playing.hasFile = true;
	playing.seconds = 125;
	std::vector<uint32_t> a(skin::kMainWidth * skin::kMainHeight);
	std::vector<uint32_t> b(a.size());
	skin.Render(stopped, a.data(), skin::kMainWidth * 4);
	skin.Render(playing, b.data(), skin::kMainWidth * 4);
	bool timeChanged = false;
	for (int y = 26; y < 39; y++) {
		for (int x = 48; x < 99; x++)
			timeChanged |= a[y * skin::kMainWidth + x] != b[y * skin::kMainWidth + x];
	}
	CHECK(timeChanged, "the built-in skin shows the time");
}


int
main()
{
	TestBMP();
	TestZip();
	TestSkin();
	TestLayout();
	TestBuiltInSkin();
	if (sFailures == 0)
		std::printf("all skin tests passed\n");
	return sFailures == 0 ? 0 : 1;
}

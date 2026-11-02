/*
 * PowerPlay — classic Winamp (2.x) skins.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#include "WinampSkin.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <sstream>
#include <sys/stat.h>

namespace skin {


//	#pragma mark - Layout


Rect
ControlRect(Control control)
{
	switch (control) {
		case kMenuButton:		return {6, 3, 9, 9};
		case kMinimizeButton:	return {244, 3, 9, 9};
		case kShadeButton:		return {254, 3, 9, 9};
		case kCloseButton:		return {264, 3, 9, 9};
		case kTitleBar:			return {0, 0, 275, 14};
		case kPrevious:			return {16, 88, 23, 18};
		case kPlay:				return {39, 88, 23, 18};
		case kPause:			return {62, 88, 23, 18};
		case kStop:				return {85, 88, 23, 18};
		case kNext:				return {108, 88, 22, 18};
		case kEject:			return {136, 89, 22, 16};
		case kPosition:			return {16, 72, 248, 10};
		case kVolume:			return {107, 57, 68, 13};
		case kBalance:			return {177, 57, 38, 13};
		case kShuffle:			return {164, 89, 47, 15};
		case kRepeat:			return {210, 89, 28, 15};
		case kEqualizer:		return {219, 58, 23, 12};
		case kPlaylist:			return {242, 58, 23, 12};
		case kTime:				return {36, 26, 63, 13};
		case kVisualizer:		return {24, 43, 76, 16};
		case kTitleText:		return {111, 27, 154, 6};
		case kDoubleSize:		return {10, 47, 8, 8};
		case kNoControl:		break;
	}
	return {0, 0, 0, 0};
}


Control
HitTest(int x, int y)
{
	// Small controls before the areas they sit in
	static const Control kOrder[] = {
		kMenuButton, kMinimizeButton, kShadeButton, kCloseButton, kTitleBar,
		kDoubleSize,
		kPrevious, kPlay, kPause, kStop, kNext, kEject, kPosition, kVolume,
		kBalance, kShuffle, kRepeat, kEqualizer, kPlaylist, kTime,
		kVisualizer, kTitleText
	};
	for (Control control : kOrder) {
		if (ControlRect(control).Contains(x, y))
			return control;
	}
	return kNoControl;
}


// Thumb sizes
static const int kPositionThumb = 29;
static const int kSliderThumb = 14;


double
SliderValue(Control slider, int x)
{
	Rect rect = ControlRect(slider);
	int thumb = slider == kPosition ? kPositionThumb : kSliderThumb;
	double travel = rect.width - thumb;
	double value = (x - rect.x - thumb / 2.0) / travel;
	value = std::clamp(value, 0.0, 1.0);
	if (slider == kBalance) {
		value = value * 2 - 1;
		// A notch in the middle, as the original has
		if (std::fabs(value) < 0.08)
			value = 0;
	}
	return value;
}


//	#pragma mark - Loading


static std::string
Lower(std::string text)
{
	for (char& c : text)
		c = char(tolower((unsigned char)c));
	return text;
}


static std::string
BaseName(const std::string& path)
{
	size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}


void
WinampSkin::_AddFile(const std::string& path, std::vector<uint8_t> data)
{
	std::string name = Lower(BaseName(path));
	for (auto& file : fFiles) {
		if (file.first == name)
			return;		// the first one wins
	}
	fFiles.emplace_back(name, std::move(data));
}


bool
WinampSkin::LoadArchive(std::vector<uint8_t> data)
{
	ZipArchive archive;
	if (!archive.Open(std::move(data)))
		return false;
	fFiles.clear();
	for (const std::string& name : archive.Names()) {
		std::string lower = Lower(name);
		// Only what a skin is drawn from
		bool wanted = lower.size() > 4
			&& (lower.compare(lower.size() - 4, 4, ".bmp") == 0
				|| lower.compare(lower.size() - 4, 4, ".txt") == 0);
		std::vector<uint8_t> contents;
		if (wanted && archive.Extract(name, contents))
			_AddFile(name, std::move(contents));
	}
	return _Finish();
}


static bool
ReadFile(const std::string& path, std::vector<uint8_t>& data)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
		return false;
	data.assign(std::istreambuf_iterator<char>(file),
		std::istreambuf_iterator<char>());
	return true;
}


bool
WinampSkin::Load(const std::string& path)
{
	std::string name = BaseName(path);
	size_t dot = name.find_last_of('.');
	fName = dot != std::string::npos && Lower(name.substr(dot)) == ".wsz"
		? name.substr(0, dot) : name;

	struct stat info;
	if (stat(path.c_str(), &info) != 0)
		return false;
	if (!S_ISDIR(info.st_mode)) {
		std::vector<uint8_t> data;
		return ReadFile(path, data) && LoadArchive(std::move(data));
	}

	// A folder: its files, and those of folders inside (some skins are
	// zipped with a folder around them)
	fFiles.clear();
	std::vector<std::string> folders = {path};
	for (size_t i = 0; i < folders.size() && i < 8; i++) {
		DIR* dir = opendir(folders[i].c_str());
		if (dir == NULL)
			continue;
		while (dirent* entry = readdir(dir)) {
			std::string child = entry->d_name;
			if (child == "." || child == "..")
				continue;
			std::string full = folders[i] + "/" + child;
			struct stat childInfo;
			if (stat(full.c_str(), &childInfo) != 0)
				continue;
			if (S_ISDIR(childInfo.st_mode))
				folders.push_back(full);
			else if (childInfo.st_size < 16 * 1024 * 1024) {
				std::vector<uint8_t> data;
				if (ReadFile(full, data))
					_AddFile(child, std::move(data));
			}
		}
		closedir(dir);
	}
	return _Finish();
}


bool
WinampSkin::_Finish()
{
	fImages.clear();
	for (auto& file : fFiles) {
		const std::string& name = file.first;
		if (name.size() < 4 || name.compare(name.size() - 4, 4, ".bmp") != 0)
			continue;
		Image image;
		if (DecodeBMP(file.second, image))
			fImages.emplace_back(name.substr(0, name.size() - 4), image);
	}
	fExtendedNumbers = _Image("nums_ex") != NULL;

	std::string visColors;
	for (auto& file : fFiles) {
		if (file.first == "viscolor.txt")
			visColors.assign(file.second.begin(), file.second.end());
		if (file.first == "pledit.txt") {
			fPlaylistColors = ParsePlaylistColors(std::string(
				file.second.begin(), file.second.end()));
		}
	}
	fVisColors = ParseVisColors(visColors);
	fFiles.clear();		// decoded; the raw files aren't needed now

	const Image* main = _Image("main");
	return main != NULL && main->width >= kMainWidth
		&& main->height >= kMainHeight;
}


const Image*
WinampSkin::_Image(const char* name) const
{
	for (const auto& image : fImages) {
		if (image.first == name)
			return &image.second;
	}
	return NULL;
}


bool
WinampSkin::HasImage(const std::string& name) const
{
	return _Image(name.c_str()) != NULL;
}


std::vector<uint32_t>
ParseVisColors(const std::string& text)
{
	// Winamp's own defaults, for skins without (or with a short) file:
	// black, grey dots, the spectrum from red down to green, the
	// oscilloscope's light-to-dark greys, and white peaks.
	static const uint8_t kDefaults[24][3] = {
		{0, 0, 0}, {24, 33, 41}, {239, 49, 16}, {206, 41, 16}, {214, 90, 0},
		{214, 102, 0}, {214, 115, 0}, {198, 123, 8}, {222, 165, 24},
		{214, 181, 33}, {189, 222, 41}, {148, 222, 33}, {41, 206, 16},
		{50, 190, 16}, {57, 181, 16}, {49, 156, 8}, {41, 148, 0},
		{24, 132, 8}, {255, 255, 255}, {214, 214, 222}, {181, 189, 189},
		{160, 170, 175}, {148, 156, 165}, {150, 150, 150}
	};
	std::vector<uint32_t> colors;
	std::istringstream lines(text);
	std::string line;
	while (std::getline(lines, line) && colors.size() < 24) {
		size_t comment = line.find("//");
		if (comment != std::string::npos)
			line.erase(comment);
		int r, g, b;
		if (sscanf(line.c_str(), " %d , %d , %d", &r, &g, &b) == 3) {
			colors.push_back(0xff000000u | (uint32_t(std::clamp(r, 0, 255)) << 16)
				| (uint32_t(std::clamp(g, 0, 255)) << 8)
				| uint32_t(std::clamp(b, 0, 255)));
		}
	}
	for (size_t i = colors.size(); i < 24; i++) {
		colors.push_back(0xff000000u | (uint32_t(kDefaults[i][0]) << 16)
			| (uint32_t(kDefaults[i][1]) << 8) | kDefaults[i][2]);
	}
	return colors;
}


//	#pragma mark - Drawing


struct WinampSkin::Canvas {
	uint32_t* pixels;
	size_t stride;	// in pixels
	int width = kMainWidth;
	int height = kMainHeight;

	void Put(int x, int y, uint32_t pixel)
	{
		if (x >= 0 && y >= 0 && x < width && y < height)
			pixels[y * stride + x] = pixel;
	}
};


void
WinampSkin::_Blit(Canvas& canvas, const char* name, int sx, int sy,
	int width, int height, int dx, int dy) const
{
	const Image* image = _Image(name);
	if (image == NULL)
		return;		// a skin without it shows main.bmp there
	for (int y = 0; y < height; y++) {
		if (sy + y < 0 || sy + y >= image->height)
			continue;
		for (int x = 0; x < width; x++) {
			if (sx + x < 0 || sx + x >= image->width)
				continue;
			canvas.Put(dx + x, dy + y, image->At(sx + x, sy + y));
		}
	}
}


// text.bmp: 5 x 6 characters in rows of 31.
static bool
GlyphFor(char c, int& gx, int& gy)
{
	static const char* kRows[] = {
		"abcdefghijklmnopqrstuvwxyz\"@  ",
		"0123456789\x85.:()-'!_+\\/[]^&%,=$#",
		"\x86\x87\x88?*"
	};
	c = char(tolower((unsigned char)c));
	if (c == ' ') {
		gx = 30 * 5;
		gy = 0;
		return true;
	}
	for (int row = 0; row < 3; row++) {
		const char* found = strchr(kRows[row], c);
		if (found != NULL && *found != '\0') {
			gx = int(found - kRows[row]) * 5;
			gy = row * 6;
			return true;
		}
	}
	return false;
}


int
WinampSkin::TextWidth(const std::string& text) const
{
	return int(text.size()) * 5;
}


void
WinampSkin::_DrawText(Canvas& canvas, const std::string& text, int x, int y,
	int width, int offset) const
{
	const Image* font = _Image("text");
	if (font == NULL)
		return;
	for (int px = 0; px < width; px++) {
		int textX = px + offset;
		int index = textX / 5;
		if (index < 0 || index >= int(text.size()))
			continue;
		int gx, gy;
		if (!GlyphFor(text[index], gx, gy))
			GlyphFor(' ', gx, gy);
		int column = gx + textX % 5;
		for (int row = 0; row < 6; row++) {
			if (column < font->width && gy + row < font->height)
				canvas.Put(x + px, y + row, font->At(column, gy + row));
		}
	}
}


void
WinampSkin::_DrawTime(Canvas& canvas, const State& state) const
{
	if (!state.hasFile)
		return;
	const char* font = fExtendedNumbers ? "nums_ex" : "numbers";
	int seconds = std::abs(state.seconds);
	int minutes = std::min(seconds / 60, 99);
	int digits[4] = {minutes / 10, minutes % 10, (seconds % 60) / 10,
		seconds % 10};
	static const int kX[4] = {48, 60, 78, 90};
	for (int i = 0; i < 4; i++)
		_Blit(canvas, font, digits[i] * 9, 0, 9, 13, kX[i], 26);

	if (state.seconds < 0) {
		if (fExtendedNumbers) {
			// nums_ex.bmp has a minus after the digits and a blank
			_Blit(canvas, font, 11 * 9, 0, 9, 13, 36, 26);
		} else {
			// numbers.bmp has none: a dash from the middle of its 2
			_Blit(canvas, font, 2 * 9 + 2, 6, 5, 1, 39, 32);
		}
	}
}


void
WinampSkin::_DrawVisualizer(Canvas& canvas, const State& state) const
{
	if (state.playback == State::kStopped)
		return;
	// The levels as two bars, left above right, in the skin's spectrum
	// colours (green at the low end to red at the top), over its
	// background colour.
	Rect area = ControlRect(kVisualizer);
	const float levels[2] = {state.levelLeft, state.levelRight};
	for (int channel = 0; channel < 2; channel++) {
		int top = area.y + 1 + channel * 8;
		int length = int(std::lround(std::clamp(levels[channel], 0.0f, 1.0f)
			* area.width));
		for (int x = 0; x < area.width; x++) {
			// Colours 2 (top, red) to 17 (bottom, green)
			int color = 17 - x * 15 / (area.width - 1);
			uint32_t pixel = x < length ? fVisColors[color] : fVisColors[0];
			for (int y = 0; y < 6; y++)
				canvas.Put(area.x + x, top + y, pixel);
		}
	}
}


void
WinampSkin::Render(const State& state, uint32_t* pixels,
	size_t bytesPerRow) const
{
	Canvas canvas = {pixels, bytesPerRow / 4};
	const Control pressed = state.pressed;

	_Blit(canvas, "main", 0, 0, kMainWidth, kMainHeight, 0, 0);

	// The title bar, and its buttons when pressed
	_Blit(canvas, "titlebar", 27, state.active ? 0 : 15, kMainWidth, 14, 0, 0);
	if (pressed == kMenuButton)
		_Blit(canvas, "titlebar", 0, 9, 9, 9, 6, 3);
	if (pressed == kMinimizeButton)
		_Blit(canvas, "titlebar", 9, 9, 9, 9, 244, 3);
	if (pressed == kShadeButton)
		_Blit(canvas, "titlebar", 9, 27, 9, 9, 254, 3);
	if (pressed == kCloseButton)
		_Blit(canvas, "titlebar", 18, 9, 9, 9, 264, 3);
	// The "clutter bar" down the left
	_Blit(canvas, "titlebar", 304, 0, 8, 43, 10, 22);
	// Its buttons lit, each from a diagonal below it: O A I D V at 3, 11,
	// 18, 25 and 33 from its top, x 304 to 336. Only D does anything.
	if (state.doubleSize || pressed == kDoubleSize)
		_Blit(canvas, "titlebar", 328, 44 + 25, 8, 8, 10, 22 + 25);

	// Playing, paused or stopped
	int status = state.playback == State::kPlaying ? 0
		: state.playback == State::kPaused ? 9 : 18;
	_Blit(canvas, "playpaus", status, 0, 9, 9, 26, 28);

	_DrawTime(canvas, state);
	_DrawVisualizer(canvas, state);

	// The title, scrolling when it doesn't fit
	Rect title = ControlRect(kTitleText);
	_DrawText(canvas, state.title, title.x, title.y, title.width,
		state.titleOffset);

	// kbps and kHz, right-aligned in their places
	if (state.hasFile && state.kbps > 0) {
		std::string kbps = std::to_string(std::min(state.kbps, 999));
		kbps.insert(0, 3 - kbps.size(), ' ');
		_DrawText(canvas, kbps, 111, 43, 15, 0);
	}
	if (state.hasFile && state.khz > 0) {
		std::string khz = std::to_string(std::min(state.khz, 99));
		khz.insert(0, 2 - khz.size(), ' ');
		_DrawText(canvas, khz, 156, 43, 10, 0);
	}

	// Mono and stereo, lit or not
	_Blit(canvas, "monoster", 0, state.channels == 2 ? 0 : 12, 29, 12, 212, 41);
	_Blit(canvas, "monoster", 29, state.channels == 1 ? 0 : 12, 27, 12, 239,
		41);

	// Volume and balance: a background frame for the value, and the thumb
	int volumeFrame = int(std::lround(std::clamp(state.volume, 0.0, 1.0) * 27));
	_Blit(canvas, "volume", 0, volumeFrame * 15, 68, 13, 107, 57);
	int volumeThumb = int(std::lround(std::clamp(state.volume, 0.0, 1.0)
		* (68 - kSliderThumb)));
	const Image* volume = _Image("volume");
	if (volume != NULL && volume->height >= 433) {
		_Blit(canvas, "volume", pressed == kVolume ? 0 : 15, 422, 14, 11,
			107 + volumeThumb, 58);
	}
	double balance = std::clamp(state.balance, -1.0, 1.0);
	int balanceFrame = int(std::lround(std::fabs(balance) * 27));
	_Blit(canvas, "balance", 9, balanceFrame * 15, 38, 13, 177, 57);
	int balanceThumb = int(std::lround((balance + 1) / 2 * (38 - kSliderThumb)));
	const Image* balanceImage = _Image("balance");
	if (balanceImage != NULL && balanceImage->height >= 433) {
		_Blit(canvas, "balance", pressed == kBalance ? 0 : 15, 422, 14, 11,
			177 + balanceThumb, 58);
	}

	// Equalizer and playlist buttons
	_Blit(canvas, "shufrep", pressed == kEqualizer ? 46 : 0,
		state.equalizer ? 73 : 61, 23, 12, 219, 58);
	_Blit(canvas, "shufrep", pressed == kPlaylist ? 69 : 23,
		state.playlist ? 73 : 61, 23, 12, 242, 58);

	// The position bar: its groove, and the thumb when there's a file
	_Blit(canvas, "posbar", 0, 0, 248, 10, 16, 72);
	if (state.hasFile) {
		int thumb = int(std::lround(std::clamp(state.position, 0.0, 1.0)
			* (248 - kPositionThumb)));
		_Blit(canvas, "posbar", pressed == kPosition ? 278 : 248, 0,
			kPositionThumb, 10, 16 + thumb, 72);
	}

	// The transport buttons, pressed or not
	struct Button { Control control; int sx; int width; };
	static const Button kButtons[] = {
		{kPrevious, 0, 23}, {kPlay, 23, 23}, {kPause, 46, 23},
		{kStop, 69, 23}, {kNext, 92, 22}
	};
	for (const Button& button : kButtons) {
		Rect rect = ControlRect(button.control);
		_Blit(canvas, "cbuttons", button.sx, pressed == button.control ? 18 : 0,
			button.width, 18, rect.x, rect.y);
	}
	_Blit(canvas, "cbuttons", 114, pressed == kEject ? 16 : 0, 22, 16, 136, 89);

	// Shuffle and repeat: off or on, pressed or not
	_Blit(canvas, "shufrep", 28, (state.shuffle ? 30 : 0)
		+ (pressed == kShuffle ? 15 : 0), 47, 15, 164, 89);
	_Blit(canvas, "shufrep", 0, (state.repeat ? 30 : 0)
		+ (pressed == kRepeat ? 15 : 0), 28, 15, 210, 89);
}

//	#pragma mark - Equalizer


Rect
EqControlRect(int control)
{
	if (control >= kEqBand && control < kEqBand + kEqBands)
		return {78 + 18 * (control - kEqBand), 38, 14, 63};
	switch (control) {
		case kEqTitleBar:	return {0, 0, 275, 14};
		case kEqClose:		return {264, 3, 9, 9};
		case kEqOn:			return {14, 18, 26, 12};
		case kEqAuto:		return {40, 18, 32, 12};
		case kEqPresets:	return {217, 18, 44, 12};
		case kEqPreamp:		return {21, 38, 14, 63};
	}
	return {0, 0, 0, 0};
}


int
EqHitTest(int x, int y)
{
	static const int kOrder[] = {kEqClose, kEqOn, kEqAuto, kEqPresets,
		kEqPreamp};
	for (int control : kOrder) {
		if (EqControlRect(control).Contains(x, y))
			return control;
	}
	for (int band = 0; band < kEqBands; band++) {
		if (EqControlRect(kEqBand + band).Contains(x, y))
			return kEqBand + band;
	}
	if (EqControlRect(kEqTitleBar).Contains(x, y))
		return kEqTitleBar;
	return kEqNone;
}


// A slider's 11-pixel thumb travels 51 pixels down its 63, +12 dB at the top
static const int kEqThumb = 11;
static const int kEqTravel = 51;


float
EqSliderGain(int y)
{
	double t = (y - 38 - kEqThumb / 2.0) / kEqTravel;
	float gain = float(12 - std::clamp(t, 0.0, 1.0) * 24);
	// A notch at 0 dB
	return std::fabs(gain) < 0.75f ? 0 : gain;
}


static int
EqThumbTop(float gain)
{
	return 38 + int(std::lround((12 - std::clamp(gain, -12.0f, 12.0f)) / 24
		* kEqTravel));
}


void
WinampSkin::RenderEqualizer(const EqState& state, uint32_t* pixels,
	size_t bytesPerRow) const
{
	Canvas canvas = {pixels, bytesPerRow / 4};
	const char* image = "eqmain";
	_Blit(canvas, image, 0, 0, kMainWidth, kMainHeight, 0, 0);
	_Blit(canvas, image, 0, state.active ? 134 : 149, kMainWidth, 14, 0, 0);
	if (state.pressed == kEqClose)
		_Blit(canvas, image, 0, 125, 9, 9, 264, 3);

	bool pressed = state.pressed == kEqOn;
	_Blit(canvas, image, state.on ? (pressed ? 187 : 69) : (pressed ? 128 : 10),
		119, 26, 12, 14, 18);
	pressed = state.pressed == kEqAuto;
	_Blit(canvas, image, state.autoLoad ? (pressed ? 213 : 95)
		: (pressed ? 154 : 36), 119, 32, 12, 40, 18);
	_Blit(canvas, image, 224, state.pressed == kEqPresets ? 176 : 164, 44, 12,
		217, 18);

	_DrawEqGraph(canvas, state);

	// Sliders: a background frame for the value (28, 14 a row), the thumb
	for (int i = -1; i < kEqBands; i++) {
		int control = i < 0 ? kEqPreamp : kEqBand + i;
		float gain = i < 0 ? state.preamp : state.bands[i];
		Rect rect = EqControlRect(control);
		int frame = int(std::lround((std::clamp(gain, -12.0f, 12.0f) + 12) / 24
			* 27));
		_Blit(canvas, image, 13 + (frame % 14) * 15, 164 + (frame / 14) * 65,
			14, 63, rect.x, rect.y);
		_Blit(canvas, image, 0, state.pressed == control ? 176 : 164,
			kEqThumb, kEqThumb, rect.x + 1, EqThumbTop(gain));
	}
}


// The response curve: the bands through a smooth line, coloured by height
// from the skin's 19 line colours, over the preamp's level.
void
WinampSkin::_DrawEqGraph(Canvas& canvas, const EqState& state) const
{
	const Image* image = _Image("eqmain");
	if (image == NULL || image->height < 315)
		return;
	const int left = 86, top = 17, width = 113, height = 19;
	_Blit(canvas, "eqmain", 0, 294, width, height, left, top);
	int preampY = int(std::lround((12 - state.preamp) / 24 * (height - 1)));
	_Blit(canvas, "eqmain", 0, 314, width, 1, left, top + preampY);

	// The bands' heights at evenly spaced points, through a Catmull-Rom line
	double points[kEqBands];
	for (int i = 0; i < kEqBands; i++)
		points[i] = (12 - std::clamp(state.bands[i], -12.0f, 12.0f)) / 24
			* (height - 1);
	auto at = [&](int i) { return points[std::clamp(i, 0, kEqBands - 1)]; };
	int lastY = -1;
	for (int x = 0; x < width; x++) {
		double position = double(x) / (width - 1) * (kEqBands - 1);
		int i = std::min(int(position), kEqBands - 2);
		double t = position - i;
		double p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
		double y = 0.5 * (2 * p1 + (-p0 + p2) * t
			+ (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t
			+ (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t);
		int row = std::clamp(int(std::lround(y)), 0, height - 1);
		// Fill steep steps so the line stays joined
		int from = lastY < 0 ? row : std::min(row, lastY + (row > lastY));
		int to = lastY < 0 ? row : std::max(row, lastY - (row < lastY));
		for (int r = std::min(from, to); r <= std::max(from, to); r++)
			canvas.Put(left + x, top + r, image->At(115, 294 + r));
		lastY = row;
	}
}


//	#pragma mark - Playlist


Rect
PlaylistControlRect(PlaylistControl control, int w, int h)
{
	// The buttons in the bottom corners, by the corner's top left
	const int bottom = h - 38, right = w - 150;
	switch (control) {
		case kPlTitleBar:	return {0, 0, w, 20};
		case kPlClose:		return {w - 11, 3, 9, 9};
		case kPlItems:		return {12, 20, w - 32, h - 58};
		case kPlScrollBar:	return {w - 15, 20, 8, h - 58};
		case kPlResize:		return {w - 20, h - 20, 20, 20};
		case kPlAdd:		return {14, bottom + 12, 22, 18};
		case kPlRemove:		return {43, bottom + 12, 22, 18};
		case kPlSelect:		return {72, bottom + 12, 22, 18};
		case kPlMisc:		return {101, bottom + 12, 22, 18};
		case kPlList:		return {right + 104, bottom + 12, 22, 18};
		case kPlPrevious:	return {right + 6, bottom + 22, 8, 8};
		case kPlPlay:		return {right + 14, bottom + 22, 9, 8};
		case kPlPause:		return {right + 23, bottom + 22, 9, 8};
		case kPlStop:		return {right + 32, bottom + 22, 9, 8};
		case kPlNext:		return {right + 41, bottom + 22, 9, 8};
		case kPlEject:		return {right + 50, bottom + 22, 9, 8};
		case kPlNone:		break;
	}
	return {0, 0, 0, 0};
}


PlaylistControl
PlaylistHitTest(int x, int y, int w, int h)
{
	static const PlaylistControl kOrder[] = {
		kPlClose, kPlResize, kPlScrollBar, kPlItems, kPlAdd, kPlRemove,
		kPlSelect, kPlMisc, kPlList, kPlPrevious, kPlPlay, kPlPause, kPlStop,
		kPlNext, kPlEject, kPlTitleBar
	};
	for (PlaylistControl control : kOrder) {
		if (PlaylistControlRect(control, w, h).Contains(x, y))
			return control;
	}
	return kPlNone;
}


static uint32_t
ParseColor(const std::string& value, uint32_t fallback)
{
	size_t hash = value.find('#');
	if (hash == std::string::npos)
		return fallback;
	unsigned int rgb;
	if (sscanf(value.c_str() + hash + 1, "%6x", &rgb) != 1)
		return fallback;
	return 0xff000000u | rgb;
}


PlaylistColors
ParsePlaylistColors(const std::string& text)
{
	PlaylistColors colors;
	std::istringstream lines(text);
	std::string line;
	while (std::getline(lines, line)) {
		size_t equals = line.find('=');
		if (equals == std::string::npos)
			continue;
		std::string key = Lower(line.substr(0, equals));
		std::string value = line.substr(equals + 1);
		while (!value.empty() && (value.back() == '\r' || value.back() == ' '))
			value.pop_back();
		while (!key.empty() && key.back() == ' ')
			key.pop_back();
		if (key == "normal")
			colors.normal = ParseColor(value, colors.normal);
		else if (key == "current")
			colors.current = ParseColor(value, colors.current);
		else if (key == "normalbg")
			colors.normalBackground = ParseColor(value, colors.normalBackground);
		else if (key == "selectedbg")
			colors.selectedBackground = ParseColor(value,
				colors.selectedBackground);
		else if (key == "font" && !value.empty())
			colors.font = value;
	}
	return colors;
}


void
WinampSkin::RenderPlaylist(const PlaylistState& state, int w, int h,
	uint32_t* pixels, size_t bytesPerRow) const
{
	Canvas canvas = {pixels, bytesPerRow / 4, w, h};
	const char* image = "pledit";

	Rect items = PlaylistControlRect(kPlItems, w, h);
	for (int y = items.y; y < items.y + items.height; y++) {
		for (int x = items.x; x < items.x + items.width; x++)
			canvas.Put(x, y, fPlaylistColors.normalBackground);
	}

	// The top: tiles, the title in the middle, the corners
	int sy = state.active ? 0 : 21;
	for (int x = 25; x < w - 25; x += 25)
		_Blit(canvas, image, 127, sy, 25, 20, x, 0);
	_Blit(canvas, image, 26, sy, 100, 20, (w - 100) / 2, 0);
	_Blit(canvas, image, 0, sy, 25, 20, 0, 0);
	_Blit(canvas, image, 153, sy, 25, 20, w - 25, 0);

	// The sides, down to the bottom part
	for (int y = 20; y < h - 38; y += 29) {
		int rows = std::min(29, h - 38 - y);
		_Blit(canvas, image, 0, 42, 12, rows, 0, y);
		_Blit(canvas, image, 31, 42, 20, rows, w - 20, y);
	}

	// The bottom: tiles between the corners
	for (int x = 125; x < w - 150; x += 25)
		_Blit(canvas, image, 179, 0, std::min(25, w - 150 - x), 38, x, h - 38);
	_Blit(canvas, image, 0, 72, 125, 38, 0, h - 38);
	_Blit(canvas, image, 126, 72, 150, 38, w - 150, h - 38);

	if (state.pressed == kPlClose)
		_Blit(canvas, image, 52, 42, 9, 9, w - 11, 3);

	Rect bar = PlaylistControlRect(kPlScrollBar, w, h);
	int thumb = bar.y + int(std::lround(std::clamp(state.scroll, 0.0, 1.0)
		* std::max(0, bar.height - 18)));
	_Blit(canvas, image, state.pressed == kPlScrollBar ? 61 : 52, 53, 8, 18,
		bar.x, thumb);

	// The times, in the main window's little font
	_DrawText(canvas, state.runningTime, w - 143, h - 28,
		TextWidth(state.runningTime), 0);
	_DrawText(canvas, state.miniTime, w - 84, h - 15,
		TextWidth(state.miniTime), 0);
}


void
WinampSkin::FillMissing(const WinampSkin& base)
{
	for (const auto& image : base.fImages) {
		if (_Image(image.first.c_str()) != NULL)
			continue;
		fImages.push_back(image);
		if (image.first == "pledit")
			fPlaylistColors = base.fPlaylistColors;
	}
}

}	// namespace skin

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

	void Put(int x, int y, uint32_t pixel)
	{
		if (x >= 0 && y >= 0 && x < kMainWidth && y < kMainHeight)
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

}	// namespace skin

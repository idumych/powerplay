/*
 * PowerPlay — classic Winamp (2.x) skins: loading a .wsz or a skin folder,
 * drawing the 275 x 116 main window from it, and finding the control under
 * a point.
 *
 * The layout is the classic skin format's: which part of which bitmap is
 * which control, and where it goes in the window. A skin is someone's
 * artwork; PowerPlay ships none.
 *
 * Portable: no Haiku headers, so the host tests can use it.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#ifndef POWERPLAY_WINAMP_SKIN_H
#define POWERPLAY_WINAMP_SKIN_H

#include <cstdint>
#include <string>
#include <vector>

#include "Archive.h"

namespace skin {

static const int kMainWidth = 275;
static const int kMainHeight = 116;


struct Rect {
	int x, y, width, height;
	bool Contains(int px, int py) const
		{ return px >= x && py >= y && px < x + width && py < y + height; }
};


// The main window's controls.
enum Control {
	kNoControl,
	kMenuButton, kMinimizeButton, kShadeButton, kCloseButton, kTitleBar,
	kPrevious, kPlay, kPause, kStop, kNext, kEject,
	kPosition, kVolume, kBalance,
	kShuffle, kRepeat, kEqualizer, kPlaylist,
	kTime, kVisualizer, kTitleText
};

// Where each control is in the window.
Rect ControlRect(Control control);
// The control at a point in the window (kNoControl over plain background).
Control HitTest(int x, int y);
// A slider's value (0..1; balance -1..1) for a point at x in the window,
// with the thumb's middle under the pointer.
double SliderValue(Control slider, int x);


// What the window shows.
struct State {
	enum Playback { kStopped, kPlaying, kPaused };
	Playback playback = kStopped;
	bool active = true;			// window focus: the title bar's look
	bool hasFile = false;
	int seconds = 0;			// time shown; negative = remaining
	std::string title;			// scrolls when wider than its space
	int titleOffset = 0;		// in pixels, for scrolling
	int kbps = 0;				// 0 = don't show
	int khz = 0;
	int channels = 0;			// 1 mono, 2 stereo, 0 neither lit
	double position = 0;		// 0..1
	double volume = 0.8;		// 0..1
	double balance = 0;			// -1..1
	bool shuffle = false;
	bool repeat = false;
	bool equalizer = false;
	bool playlist = false;
	float levelLeft = 0;		// visualizer: needle deflections, 0..~1
	float levelRight = 0;
	Control pressed = kNoControl;
};


class WinampSkin {
public:
	// A .wsz (zip) file or a folder of skin files. False if it has no
	// main.bmp, which every skin has.
	bool Load(const std::string& path);
	bool LoadArchive(std::vector<uint8_t> data);

	const std::string& Name() const { return fName; }

	// Draws the main window into 0xAARRGGBB pixels (kMainWidth x
	// kMainHeight), rows bytesPerRow apart.
	void Render(const State& state, uint32_t* pixels,
		size_t bytesPerRow) const;

	// The title's width in pixels, for scrolling it.
	int TextWidth(const std::string& text) const;

	// What the skin has, for tests.
	bool HasImage(const std::string& name) const;
	const std::vector<uint32_t>& VisColors() const { return fVisColors; }

private:
	struct Canvas;

	void _AddFile(const std::string& path, std::vector<uint8_t> data);
	bool _Finish();
	const Image* _Image(const char* name) const;
	void _Blit(Canvas& canvas, const char* image, int sx, int sy, int width,
		int height, int dx, int dy) const;
	void _DrawText(Canvas& canvas, const std::string& text, int x, int y,
		int width, int offset) const;
	void _DrawTime(Canvas& canvas, const State& state) const;
	void _DrawVisualizer(Canvas& canvas, const State& state) const;

	std::string fName;
	// Files by lower-case name, wherever they were in the archive
	std::vector<std::pair<std::string, std::vector<uint8_t>>> fFiles;
	std::vector<std::pair<std::string, Image>> fImages;
	std::vector<uint32_t> fVisColors;	// 24, from viscolor.txt
	bool fExtendedNumbers = false;		// nums_ex.bmp rather than numbers.bmp
};


// The visualizer's 24 colours from viscolor.txt ("r,g,b" a line, // comments);
// the classic defaults where the file has fewer.
std::vector<uint32_t> ParseVisColors(const std::string& text);

}	// namespace skin

#endif	// POWERPLAY_WINAMP_SKIN_H

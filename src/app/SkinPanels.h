/*
 * PowerPlay — a Winamp skin's equalizer and playlist windows.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#ifndef POWERPLAY_SKIN_PANELS_H
#define POWERPLAY_SKIN_PANELS_H

#include <Window.h>

#include <memory>

#include "WinampSkin.h"

class SkinEqView;
class SkinPlaylistView;


// The equalizer: what it changes goes to the app as kMsgSetEqualizer, and
// the app sends back what to show with SetEqualizer().
class SkinEqWindow : public BWindow {
public:
								SkinEqWindow(BPoint where);

	virtual	bool				QuitRequested();
	virtual	void				WindowActivated(bool active);

			// With the window locked
			void				SetSkin(std::shared_ptr<skin::WinampSkin> skin);
			void				SetEqualizer(const skin::EqState& state);
			void				SetDoubleSize(bool doubleSize);

private:
			SkinEqView*			fView;
};


// The playlist: kMsgPlaylistUpdate from the app fills it, as it does the
// plain playlist window; edits go back as the same messages.
class SkinPlaylistWindow : public BWindow {
public:
								SkinPlaylistWindow(BRect frame);

	virtual	bool				QuitRequested();
	virtual	void				WindowActivated(bool active);
	virtual	void				MessageReceived(BMessage* message);

			// With the window locked
			void				SetSkin(std::shared_ptr<skin::WinampSkin> skin);
			// The playing file's place and length, for the time displays
			void				SetTimes(double position, double duration);

private:
			SkinPlaylistView*	fView;
};

#endif	// POWERPLAY_SKIN_PANELS_H

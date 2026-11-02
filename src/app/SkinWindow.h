/*
 * PowerPlay — the main window drawn by a classic Winamp skin.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#ifndef POWERPLAY_SKIN_WINDOW_H
#define POWERPLAY_SKIN_WINDOW_H

#include <String.h>
#include <Window.h>

#include "WinampSkin.h"

class SkinView;


class SkinWindow : public BWindow {
public:
								SkinWindow(BPoint where);

	virtual	bool				QuitRequested();
	virtual	void				WindowActivated(bool active);
	virtual	void				DispatchMessage(BMessage* message,
									BHandler* handler);
	virtual	void				MessageReceived(BMessage* message);

			// With the window locked
			void				SetSkin(skin::WinampSkin* skin,
									const char* path);
			const char*			SkinPath() const { return fSkinPath.String(); }
			// What the app knows; the window keeps its own pressed control,
			// scroll offset and focus.
			void				SetState(const skin::State& state);
			// Winamp's double size: every skin pixel drawn 2 x 2
			void				SetDoubleSize(bool doubleSize);
			bool				DoubleSize() const { return fDoubleSize; }

private:
			bool				_HandleShortcut(BMessage* message);

			SkinView*			fView;
			BString				fSkinPath;
			bool				fDoubleSize;
};

#endif	// POWERPLAY_SKIN_WINDOW_H

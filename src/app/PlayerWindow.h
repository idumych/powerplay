/*
 * PowerPlay — the main window: what's playing, the controls and a meter.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#ifndef POWERPLAY_PLAYER_WINDOW_H
#define POWERPLAY_PLAYER_WINDOW_H

#include <Window.h>

class BButton;
class BMenuItem;
class BStringView;
class VUView;


class PlayerWindow : public BWindow {
public:
								PlayerWindow(BRect frame);

	virtual	bool				QuitRequested();
	virtual	void				MenusBeginning();
	virtual	void				MessageReceived(BMessage* message);

			// Called by the app, with the window locked
			void				SetFileName(const char* name);
			void				SetPlaying(bool playing, bool hasFile);
			void				SetLevels(float left, float right);

private:
			BStringView*		fFileName;
			BButton*			fPlayButton;
			BButton*			fStopButton;
			BMenuItem*			fPlayItem;
			BMenuItem*			fStopItem;
			BMenuItem*			fDeskbarItem;
			VUView*				fMeter;
};

#endif	// POWERPLAY_PLAYER_WINDOW_H

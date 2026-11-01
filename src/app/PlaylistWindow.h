/*
 * PowerPlay — the playlist editor.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#ifndef POWERPLAY_PLAYLIST_WINDOW_H
#define POWERPLAY_PLAYLIST_WINDOW_H

#include <Window.h>

class BButton;
class BListView;
class BStringView;


class PlaylistWindow : public BWindow {
public:
								PlaylistWindow(BRect frame);
	virtual						~PlaylistWindow();

	virtual	bool				QuitRequested();
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_Update(BMessage* update);
			void				_RemoveSelected();
			void				_UpdateButtons();

			BListView*			fList;
			BButton*			fRemoveButton;
			BStringView*		fCount;
};

#endif	// POWERPLAY_PLAYLIST_WINDOW_H

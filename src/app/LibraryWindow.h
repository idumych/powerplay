/*
 * PowerPlay — the Library window: a music library and the playlist in
 * columns, with the transport, a display of what's playing and a search
 * field above them, all in Haiku's own controls.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#ifndef POWERPLAY_LIBRARY_WINDOW_H
#define POWERPLAY_LIBRARY_WINDOW_H

#include <Window.h>

#include <map>
#include <string>
#include <vector>

class BButton;
class BColumnListView;
class BMenu;
class BMenuItem;
class BRow;
class BSlider;
class BStringView;
class BTextControl;
class InfoLoader;
class NowPlayingView;
class SongRow;

// What the window knows about a file, from its tags.
struct SongInfo {
	std::string			title;
	std::string			artist;
	std::string			album;
	int					track = 0;
	double				duration = 0;
	int64_t				modified = 0;
	bool				loaded = false;
};

class LibraryWindow : public BWindow {
public:
	enum Source { kLibrarySource = 0, kPlaylistSource = 1 };

								LibraryWindow(BRect frame,
									const BMessage& state);
	virtual						~LibraryWindow();

	virtual	bool				QuitRequested();
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				DispatchMessage(BMessage* message,
									BHandler* handler);
	virtual	void				MenusBeginning();

			// From the app, with the window locked
			void				SetState(bool playing, bool hasFile);
			void				SetPosition(double position,
									double duration);
			void				SetVolume(float volume);
			void				SetRepeat(bool repeat);
			void				SetShuffle(bool shuffle);
			void				SetLively(bool lively);
			void				ShowSource(Source source);
			// The columns and the source shown, for the settings
			void				SaveState(BMessage& state);

private:
			void				_UpdateLibrary(const BMessage* message);
			void				_UpdatePlaylist(const BMessage* message);
			void				_Rebuild();
			bool				_Matches(const std::string& path) const;
			void				_MarkPlaying();
			void				_UpdateSummary();
			void				_UpdateDisplay();
			void				_SongInfoArrived(const BMessage* message);
			void				_PlaySelected();
			void				_RemoveSelected();
			void				_SelectAll();
			const SongInfo&		_Info(const std::string& path);
			const std::string&	_PlayingPath() const;
			bool				_SongsFocused();
			void				_LoadCache();
			void				_SaveCache();

			BColumnListView*	fSources;
			BColumnListView*	fSongs;
			NowPlayingView*		fDisplay;
			BButton*			fPreviousButton;
			BButton*			fPlayButton;
			BButton*			fNextButton;
			BButton*			fShuffleButton;
			BButton*			fRepeatButton;
			BSlider*			fVolume;
			BTextControl*		fSearch;
			BStringView*		fSummary;
			BMenu*				fSkinsMenu;
			BMenuItem*			fPlayItem;
			BMenuItem*			fShuffleItem;
			BMenuItem*			fRepeatItem;
			BMenuItem*			fDeskbarItem;
			BMenuItem*			fLivelyItem;

			Source				fSource;
			std::vector<std::string> fLibrary;
			std::vector<std::string> fPlaylist;
			int32				fCurrent;
			bool				fPlaying;
			std::map<std::string, SongInfo> fInfo;
			InfoLoader*			fLoader;
			bool				fCacheChanged;
};

#endif	// POWERPLAY_LIBRARY_WINDOW_H

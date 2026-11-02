/*
 * PowerPlay — an audio player with a VU meter in the Deskbar.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#if __has_include(<AboutWindow.h>)
#	include <AboutWindow.h>
#else
	// Haiku R1/beta6 has BAboutWindow in libbe with a private header.
#	include <private/interface/AboutWindow.h>
#endif
#include <Alert.h>
#include <Application.h>
#include <Deskbar.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <MessageRunner.h>
#include <NodeInfo.h>
#include <Path.h>
#include <PropertyInfo.h>
#include <Roster.h>
#include <Screen.h>

#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <string>
#include <vector>

#include "Constants.h"
#include "DeskbarVU.h"
#include "EqualizerWindow.h"
#include "Player.h"
#include "PlayerWindow.h"
#include "Playlist.h"
#include "PlaylistWindow.h"
#include "SkinPanels.h"
#include "SkinWindow.h"
#include "Skins.h"
#include "WinampSkin.h"

static const char* kSettingsName = "PowerPlay_settings";

// Scripting, for tests and for hey.
static property_info sProperties[] = {
	{ "Levels", { B_GET_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 },
		"Returns the needles' deflection and whether PowerPlay is playing, "
		"as \"left=L right=R playing=0|1\" (1.0 = full scale).", 0,
		{ B_STRING_TYPE } },
	{ "Status", { B_GET_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 },
		"Returns what's playing: \"index=I count=N file=NAME speed=S "
		"position=P duration=D playing=0|1 starved=K\".", 0,
		{ B_STRING_TYPE } },
	{ 0 }
};

// Files a folder contributes: what the system says is audio, or has an
// audio file's extension when it has no type yet.
static const char* kAudioExtensions[] = {
	"wav", "aif", "aiff", "aifc", "au", "snd", "mp3", "mp2", "ogg", "oga",
	"opus", "flac", "m4a", "aac", "wma", "mod", "xm", "s3m", "it", "mid",
	"midi", NULL
};


static bool
HasExtension(const char* name, const char* const* extensions)
{
	const char* dot = strrchr(name, '.');
	if (dot == NULL)
		return false;
	for (int32 i = 0; extensions[i] != NULL; i++) {
		if (strcasecmp(dot + 1, extensions[i]) == 0)
			return true;
	}
	return false;
}


static bool
IsPlaylistFile(const BEntry& entry, const char* name)
{
	static const char* kPlaylist[] = {"m3u", "m3u8", NULL};
	if (HasExtension(name, kPlaylist))
		return true;
	BNode node(&entry);
	char type[B_MIME_TYPE_LENGTH];
	return BNodeInfo(&node).GetType(type) == B_OK
		&& (strcasecmp(type, "audio/x-mpegurl") == 0
			|| strcasecmp(type, "audio/mpegurl") == 0);
}


static bool
IsAudioFile(const BEntry& entry, const char* name)
{
	BNode node(&entry);
	char type[B_MIME_TYPE_LENGTH];
	if (BNodeInfo(&node).GetType(type) == B_OK)
		return strncasecmp(type, "audio/", 6) == 0;
	return HasExtension(name, kAudioExtensions);
}


class PowerPlayApp : public BApplication {
public:
								PowerPlayApp();
	virtual						~PowerPlayApp();

	virtual	void				ReadyToRun();
	virtual	void				ArgvReceived(int32 argc, char** argv);
	virtual	void				RefsReceived(BMessage* message);
	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();
	virtual	BHandler*			ResolveSpecifier(BMessage* message,
									int32 index, BMessage* specifier,
									int32 what, const char* property);
	virtual	status_t			GetSupportedSuites(BMessage* data);

private:
			bool				_HandleScripting(BMessage* message);
			void				_Collect(const BMessage* message,
									std::vector<std::string>& paths);
			void				_CollectEntry(BEntry& entry, bool explicitly,
									std::vector<std::string>& paths,
									int depth);
			void				_AddToPlaylist(const BMessage* message,
									bool replace);
			bool				_Load(int32 index, bool atEnd, bool play,
									bool alertOnError = false);
			void				_PlayPause(bool backwards);
			void				_Advance(bool backwards, bool wrap);
			void				_RemoveFromPlaylist(BMessage* message);
			void				_PlaylistChanged();
			void				_ShowPlaylist();
			void				_ShowEqualizer(bool show);
			// PowerPlay's own playlist and equalizer windows, shown as they
			// were left, or hidden (a skin has its own)
			void				_ShowPlainPanels(bool show);
			void				_UpdateWindow();
			void				_UpdateSkin(float left, float right);
			void				_PublishLevels();
			bool				_ApplySkin(const char* path);
			bool				_SkinPathSet() const
									{ return fSkinPath.Length() > 0; }
			void				_ApplyEqualizer();
			static bool			_IsShown(BWindow* window);
			static void			_ShowWindow(BWindow* window, bool show);
			static void			_ToggleWindow(BWindow* window);
			bool				_SkinShown();
			int32				_RandomOther();
			void				_ToggleDeskbar();
			void				_ShowFilePanel(bool add);
			void				_LoadSettings();
			void				_SaveSettings();

			Player				fPlayer;
			audio::Playlist		fPlaylist;
			PlayerWindow*		fWindow;
			SkinWindow*			fSkinWindow;
			SkinEqWindow*		fSkinEq;
			SkinPlaylistWindow*	fSkinPlaylist;
			// The built-in skin, for what other skins lack
			skin::WinampSkin	fBaseSkin;
			bool				fBaseSkinLoaded;
			PlaylistWindow*		fPlaylistWindow;
			EqualizerWindow*	fEqWindow;
			BFilePanel*			fOpenPanel;
			BFilePanel*			fAddPanel;
			BMessageRunner*		fMeterRunner;
			std::vector<BMessenger> fMeters;
			int32				fTicks;

			// Settings
			BRect				fWindowFrame;
			BRect				fPlaylistFrame;
			float				fVolume;	// the knob, 0..1
			bool				fRepeat;
			bool				fWindowMeter;
			bool				fShuffle;
			float				fBalance;
			BString				fSkinPath;
			BPoint				fSkinWhere;
			bool				fSkinDoubleSize;
			BPoint				fSkinEqWhere;
			bool				fSkinEqShown;
			BRect				fSkinPlaylistFrame;
			bool				fSkinPlaylistShown;
			bool				fEqOn;
			float				fEqPreamp;
			float				fEqBands[skin::kEqBands];
			bool				fLively;
			bool				fPlaylistShown;
			BRect				fEqFrame;
			bool				fEqShown;
			double				fResumePosition;
};


PowerPlayApp::PowerPlayApp()
	:
	BApplication(kAppSignature),
	fWindow(NULL),
	fSkinWindow(NULL),
	fSkinEq(NULL),
	fSkinPlaylist(NULL),
	fBaseSkinLoaded(false),
	fPlaylistWindow(NULL),
	fEqWindow(NULL),
	fOpenPanel(NULL),
	fAddPanel(NULL),
	fMeterRunner(NULL),
	fTicks(0),
	fWindowFrame(100, 100, 480, 200),
	fPlaylistFrame(100, 260, 420, 560),
	fVolume(0.8f),
	fRepeat(false),
#ifdef APP_COSMOE
	// No Deskbar to put the meter in
	fWindowMeter(true),
#else
	fWindowMeter(false),
#endif
	fShuffle(false),
	fBalance(0),
	fSkinWhere(100, 100),
	fSkinDoubleSize(false),
	fSkinEqWhere(-1, -1),
	fSkinEqShown(false),
	fSkinPlaylistFrame(0, 0, -1, -1),
	fSkinPlaylistShown(false),
	fEqOn(false),
	fEqPreamp(0),
	fLively(true),
	fPlaylistShown(false),
	fEqFrame(0, 0, -1, -1),
	fEqShown(false),
	fResumePosition(0)
{
	for (float& gain : fEqBands)
		gain = 0;
	_LoadSettings();
	fWindow = new PlayerWindow(fWindowFrame);
	fWindow->SetVolume(fVolume);
	fWindow->SetRepeat(fRepeat);
	fWindow->SetWindowMeter(fWindowMeter);
	fWindow->SetLively(fLively);
	fPlayer.SetVolume(fVolume * fVolume);
	fPlayer.SetLivelyMeter(fLively);
	fPlayer.SetBalance(fBalance);
	_ApplyEqualizer();
	fSkinWindow = new SkinWindow(fSkinWhere);
	// The equalizer and playlist open under it, Winamp's stack
	if (fSkinEqWhere.x < 0)
		fSkinEqWhere = fSkinWhere + BPoint(0, skin::kMainHeight);
	if (!fSkinPlaylistFrame.IsValid()) {
		fSkinPlaylistFrame = BRect(0, 0, skin::kPlaylistMinWidth - 1,
			skin::kPlaylistMinHeight + 2 * skin::kPlaylistHeightStep - 1);
		fSkinPlaylistFrame.OffsetTo(fSkinWhere
			+ BPoint(0, 2 * skin::kMainHeight));
	}
	fSkinEq = new SkinEqWindow(fSkinEqWhere);
	fSkinPlaylist = new SkinPlaylistWindow(fSkinPlaylistFrame);
	if (fSkinDoubleSize)
	{
		fSkinWindow->SetDoubleSize(true);
		fSkinEq->SetDoubleSize(true);
	}
}


PowerPlayApp::~PowerPlayApp()
{
	delete fMeterRunner;
	delete fOpenPanel;
	delete fAddPanel;
}


void
PowerPlayApp::ReadyToRun()
{
	BMessage publish(kMsgPublishLevels);
	fMeterRunner = new BMessageRunner(BMessenger(this), &publish,
		kMeterInterval);

	// The playlist from last time, ready where it was but not playing.
	// (Files given at launch arrive before this and replace it.)
	if (!fPlayer.HasFile() && fPlaylist.Current() != audio::Playlist::kNone
		&& _Load(fPlaylist.Current(), false, false))
		fPlayer.Seek(fResumePosition);
	// The skin from last time, or PowerPlay's own look
	BString skinPath = fSkinPath;
	if (skinPath.Length() == 0 || !_ApplySkin(skinPath.String()))
		fWindow->Show();
	_UpdateWindow();
	if (!_SkinPathSet())
		_ShowPlainPanels(true);
}


void
PowerPlayApp::ArgvReceived(int32 argc, char** argv)
{
	BMessage refs(B_REFS_RECEIVED);
	for (int32 i = 1; i < argc; i++) {
		entry_ref ref;
		if (get_ref_for_path(argv[i], &ref) == B_OK)
			refs.AddRef("refs", &ref);
		else
			fprintf(stderr, "PowerPlay: can't find %s\n", argv[i]);
	}
	if (refs.HasRef("refs"))
		RefsReceived(&refs);
}


void
PowerPlayApp::RefsReceived(BMessage* message)
{
	// A skin is installed and used; the rest become the playlist (opened,
	// dropped on the window, or from Tracker).
	BMessage music(*message);
	music.RemoveName("refs");
	entry_ref ref;
	for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
		if (is_skin_file(ref)) {
			BPath installed;
			if (install_skin(ref, installed) == B_OK)
				_ApplySkin(installed.Path());
		} else
			music.AddRef("refs", &ref);
	}
	if (music.HasRef("refs"))
		_AddToPlaylist(&music, true);
}


BHandler*
PowerPlayApp::ResolveSpecifier(BMessage* message, int32 index,
	BMessage* specifier, int32 what, const char* property)
{
	BPropertyInfo info(sProperties);
	if (info.FindMatch(message, index, specifier, what, property) >= 0)
		return this;
	return BApplication::ResolveSpecifier(message, index, specifier, what,
		property);
}


status_t
PowerPlayApp::GetSupportedSuites(BMessage* data)
{
	data->AddString("suites", "suite/vnd.PowerPlay");
	BPropertyInfo info(sProperties);
	data->AddFlat("messages", &info);
	return BApplication::GetSupportedSuites(data);
}


bool
PowerPlayApp::_HandleScripting(BMessage* message)
{
	BMessage specifier;
	int32 index, what;
	const char* property;
	if (message->GetCurrentSpecifier(&index, &specifier, &what, &property)
			!= B_OK
		|| BPropertyInfo(sProperties).FindMatch(message, index, &specifier,
			what, property) < 0)
		return false;

	BString result;
	if (strcmp(property, "Levels") == 0) {
		float left, right;
		fPlayer.Levels(left, right);
		result.SetToFormat("left=%.4f right=%.4f playing=%d", left, right,
			fPlayer.IsPlaying() ? 1 : 0);
	} else {
		result.SetToFormat("index=%d count=%d file=%s speed=%.2f "
			"position=%.2f duration=%.2f playing=%d starved=%" B_PRId32,
			fPlaylist.Current(), fPlaylist.CountItems(),
			fPlayer.HasFile() ? fPlayer.FileName() : "-", fPlayer.Speed(),
			fPlayer.Position(), fPlayer.Duration(),
			fPlayer.IsPlaying() ? 1 : 0, fPlayer.StarvedBlocks());
	}
	BMessage reply(B_REPLY);
	reply.AddString("result", result);
	reply.AddInt32("error", B_OK);
	message->SendReply(&reply);
	return true;
}


void
PowerPlayApp::MessageReceived(BMessage* message)
{
	if (message->what == B_GET_PROPERTY && _HandleScripting(message))
		return;

	switch (message->what) {
		case kMsgOpen:
			_ShowFilePanel(false);
			break;
		case kMsgAddFiles:
			_ShowFilePanel(true);
			break;

		case kMsgPlayPause:
			_PlayPause(false);
			break;
		case kMsgReverse:
			_PlayPause(true);
			break;

		case kMsgStop:
			fPlayer.Stop();
			_UpdateWindow();
			break;

		case kMsgPlay:
			// As Winamp's Play: forwards, and from the start when playing
			if (fPlayer.HasFile() && fPlayer.IsPlaying()
				&& fPlayer.Speed() > 0)
				fPlayer.Seek(0);
			else
				_PlayPause(false);
			_UpdateWindow();
			break;

		case kMsgPause:
			if (fPlayer.IsPlaying())
				fPlayer.Pause();
			else if (fPlayer.HasFile())
				fPlayer.Play();
			_UpdateWindow();
			break;

		case kMsgSetBalance:
		{
			float balance;
			if (message->FindFloat("balance", &balance) == B_OK) {
				fBalance = balance < -1 ? -1 : balance > 1 ? 1 : balance;
				fPlayer.SetBalance(fBalance);
			}
			break;
		}

		case kMsgToggleShuffle:
			fShuffle = !fShuffle;
			_UpdateWindow();
			break;

		case kMsgSetSkin:
		{
			const char* path;
			if (message->FindString("path", &path) == B_OK)
				_ApplySkin(path);
			break;
		}

		case kMsgOpenSkinsFolder:
		{
			BPath folder;
			entry_ref ref;
			if (skins_folder(folder, true) == B_OK
				&& get_ref_for_path(folder.Path(), &ref) == B_OK)
				be_roster->Launch(&ref);
			break;
		}

		case kMsgNext:
			_Advance(false, fRepeat);
			break;

		case kMsgPrevious:
			// Back to the start of this one first, as CD players do.
			if (fPlayer.HasFile() && fPlayer.Speed() >= 0
				&& fPlayer.Position() > 3.0) {
				fPlayer.Seek(0);
				_UpdateWindow();
			} else
				_Advance(true, fRepeat);
			break;

		case kMsgPlaybackEnded:
		{
			// On to the next file; playing backwards, to the previous one,
			// from its end, so a whole playlist plays backwards.
			bool backwards = message->GetBool("backwards", false);
			int32 next = fShuffle && !backwards ? _RandomOther()
				: backwards ? fPlaylist.Previous(fRepeat)
				: fPlaylist.Next(fRepeat);
			if (next != audio::Playlist::kNone)
				_Load(next, backwards, true);
			_UpdateWindow();
			break;
		}

		case kMsgSetSpeed:
		{
			float percent;
			if (message->FindFloat("speed", &percent) == B_OK) {
				fPlayer.SetSpeed(percent / 100.0);
				_UpdateWindow();
			}
			break;
		}

		case kMsgSeek:
		{
			float value;
			if (message->FindFloat("fraction", &value) == B_OK)
				fPlayer.Seek(value * fPlayer.Duration());
			else if (message->FindFloat("position", &value) == B_OK)
				fPlayer.Seek(value);
			_UpdateWindow();
			break;
		}

		case kMsgSetVolume:
		{
			float volume;
			if (message->FindFloat("volume", &volume) == B_OK) {
				fVolume = volume < 0 ? 0 : volume > 1 ? 1 : volume;
				// The knob turns evenly in loudness, not in gain.
				fPlayer.SetVolume(fVolume * fVolume);
				if (fWindow->Lock()) {
					fWindow->SetVolume(fVolume);
					fWindow->Unlock();
				}
			}
			break;
		}

		case kMsgToggleRepeat:
			fRepeat = !fRepeat;
			if (fWindow->Lock()) {
				fWindow->SetRepeat(fRepeat);
				fWindow->Unlock();
			}
			break;

		case kMsgShowPlaylist:
			if (_SkinShown())
				_ToggleWindow(fSkinPlaylist);
			else
				_ShowPlaylist();
			_UpdateWindow();
			break;

		case kMsgShowEqualizer:
			if (_SkinShown())
				_ToggleWindow(fSkinEq);
			else
				_ShowEqualizer(!_IsShown(fEqWindow));
			_UpdateWindow();
			break;

		case kMsgSetEqualizer:
		{
			bool on;
			if (message->FindBool("on", &on) == B_OK)
				fEqOn = on;
			int32 band;
			float gain;
			if (message->FindInt32("band", &band) == B_OK
				&& message->FindFloat("gain", &gain) == B_OK) {
				if (band < 0)
					fEqPreamp = gain;
				else if (band < skin::kEqBands)
					fEqBands[band] = gain;
			}
			for (int32 i = 0; i < skin::kEqBands
					&& message->FindFloat("bands", i, &gain) == B_OK; i++)
				fEqBands[i] = gain;
			if (message->FindFloat("preamp", &gain) == B_OK)
				fEqPreamp = gain;
			_ApplyEqualizer();
			break;
		}

		case kMsgToggleDoubleSize:
			fSkinWindow->PostMessage(kMsgToggleDoubleSize);
			break;

		case kMsgSkinDoubleSize:
			if (fSkinEq->Lock()) {
				fSkinEq->SetDoubleSize(message->GetBool("double", false));
				fSkinEq->Unlock();
			}
			break;

		case kMsgPlaylistAdd:
			_AddToPlaylist(message, false);
			break;

		case kMsgPlaylistPlay:
		{
			int32 index;
			if (message->FindInt32("index", &index) == B_OK) {
				if (fPlayer.Speed() < 0)
					fPlayer.SetSpeed(-fPlayer.Speed());
				_Load(index, false, true, true);
			}
			break;
		}

		case kMsgPlaylistRemove:
			_RemoveFromPlaylist(message);
			break;

		case kMsgPlaylistClear:
			fPlayer.Close();
			fPlaylist.Clear();
			_PlaylistChanged();
			break;

		case kMsgPlaylistSortName:
			fPlaylist.SortByName();
			_PlaylistChanged();
			break;
		case kMsgPlaylistSortPath:
			fPlaylist.SortByPath();
			_PlaylistChanged();
			break;
		case kMsgPlaylistShuffle:
			fPlaylist.Shuffle((uint32)system_time());
			_PlaylistChanged();
			break;

		case kMsgShowWindow:
			if (fWindow->Lock()) {
				if (fWindow->IsHidden())
					fWindow->Show();
				else
					fWindow->Activate();
				fWindow->Unlock();
			}
			break;

		case kMsgMeterSubscribe:
		{
			BMessenger meter;
			if (message->FindMessenger("meter", &meter) == B_OK
				&& meter.IsValid()) {
				bool known = false;
				for (const BMessenger& existing : fMeters)
					known = known || existing == meter;
				if (!known)
					fMeters.push_back(meter);
			}
			break;
		}

		case kMsgPublishLevels:
			_PublishLevels();
			break;

		case kMsgToggleDeskbar:
			_ToggleDeskbar();
			break;

		case kMsgToggleLively:
			fLively = !fLively;
			fPlayer.SetLivelyMeter(fLively);
			if (fWindow->Lock()) {
				fWindow->SetLively(fLively);
				fWindow->Unlock();
			}
			break;

		case kMsgToggleWindowMeter:
			if (fWindow->Lock()) {
				fWindowMeter = !fWindow->ShowsWindowMeter();
				fWindow->SetWindowMeter(fWindowMeter);
				fWindow->Unlock();
			}
			break;

		case kMsgAbout:
		{
			BAboutWindow* about = new BAboutWindow(kAppName, kAppSignature);
			about->AddDescription(
				"An audio player with a VU meter in the Deskbar.");
			about->AddCopyright(2026, "the PowerPlay authors");
			about->AddExtraInfo("Distributed under the MIT licence.");
			about->Show();
			break;
		}

		default:
			BApplication::MessageReceived(message);
	}
}


bool
PowerPlayApp::QuitRequested()
{
	fResumePosition = fPlayer.Position();
	fPlayer.Close();

	// Leave the Deskbar meter at rest rather than frozen mid-swing.
	BMessage rest(kMsgMeterLevels);
	rest.AddFloat("left", 0);
	rest.AddFloat("right", 0);
	rest.AddBool("playing", false);
	for (BMessenger& meter : fMeters)
		meter.SendMessage(&rest, (BHandler*)NULL, 0);

	if (fPlaylistWindow != NULL && fPlaylistWindow->Lock()) {
		fPlaylistFrame = fPlaylistWindow->Frame();
		if (!_SkinPathSet())
			fPlaylistShown = !fPlaylistWindow->IsHidden();
		fPlaylistWindow->Quit();
		fPlaylistWindow = NULL;
	}
	if (fEqWindow != NULL && fEqWindow->Lock()) {
		fEqFrame = fEqWindow->Frame();
		if (!_SkinPathSet())
			fEqShown = !fEqWindow->IsHidden();
		fEqWindow->Quit();
		fEqWindow = NULL;
	}
	if (fSkinWindow->Lock()) {
		fSkinWhere = fSkinWindow->Frame().LeftTop();
		fSkinDoubleSize = fSkinWindow->DoubleSize();
		fSkinWindow->Quit();
	}
	if (fSkinEq->Lock()) {
		fSkinEqWhere = fSkinEq->Frame().LeftTop();
		if (_SkinPathSet())
			fSkinEqShown = !fSkinEq->IsHidden();
		fSkinEq->Quit();
	}
	if (fSkinPlaylist->Lock()) {
		fSkinPlaylistFrame = fSkinPlaylist->Frame();
		if (_SkinPathSet())
			fSkinPlaylistShown = !fSkinPlaylist->IsHidden();
		fSkinPlaylist->Quit();
	}
	if (fWindow->Lock()) {
		fWindowFrame = fWindow->Frame();
		fWindow->Quit();
	}
	_SaveSettings();
	return true;
}


//	#pragma mark - Playlist


void
PowerPlayApp::_Collect(const BMessage* message,
	std::vector<std::string>& paths)
{
	entry_ref ref;
	for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
		BEntry entry(&ref, true);
		_CollectEntry(entry, true, paths, 0);
	}
}


// Folders are searched (sorted by name) for audio files; playlists are
// read; a file given by itself is taken whatever its type, so the player
// can say if it can't play it.
void
PowerPlayApp::_CollectEntry(BEntry& entry, bool explicitly,
	std::vector<std::string>& paths, int depth)
{
	BPath path;
	if (entry.InitCheck() != B_OK || entry.GetPath(&path) != B_OK)
		return;

	if (entry.IsDirectory()) {
		if (depth > 8)
			return;
		BDirectory directory(&entry);
		std::vector<std::string> children;
		BEntry child;
		while (directory.GetNextEntry(&child, true) == B_OK) {
			BPath childPath;
			if (child.GetPath(&childPath) == B_OK)
				children.push_back(childPath.Path());
		}
		std::sort(children.begin(), children.end(),
			[](const std::string& a, const std::string& b) {
				return strcasecmp(a.c_str(), b.c_str()) < 0;
			});
		for (const std::string& childPath : children) {
			BEntry childEntry(childPath.c_str(), true);
			_CollectEntry(childEntry, false, paths, depth + 1);
		}
		return;
	}

	if (IsPlaylistFile(entry, path.Leaf())) {
		BFile file(&entry, B_READ_ONLY);
		off_t size = 0;
		if (file.GetSize(&size) != B_OK || size > 16 * 1024 * 1024)
			return;
		std::string text(size, '\0');
		file.Read(&text[0], size);
		BPath folder;
		path.GetParent(&folder);
		audio::Playlist list;
		list.Load(text, folder.Path());
		for (int32 i = 0; i < list.CountItems(); i++)
			paths.push_back(list.PathAt(i));
		return;
	}

	if (explicitly || IsAudioFile(entry, path.Leaf()))
		paths.push_back(path.Path());
}


void
PowerPlayApp::_AddToPlaylist(const BMessage* message, bool replace)
{
	std::vector<std::string> paths;
	_Collect(message, paths);
	if (paths.empty())
		return;

	if (replace) {
		fPlaylist.Clear();
		for (const std::string& path : paths)
			fPlaylist.Add(path);
		if (fPlayer.Speed() < 0)
			fPlayer.SetSpeed(-fPlayer.Speed());
		_Load(0, false, true, paths.size() == 1);
	} else {
		int32 index = message->GetInt32("index", -1);
		bool wasEmpty = fPlaylist.CountItems() == 0;
		int32 first = -1;
		for (const std::string& path : paths) {
			int32 at = fPlaylist.Add(path, index);
			if (first < 0)
				first = at;
			if (index >= 0)
				index = at + 1;
		}
		// Nothing loaded yet: start with what was just added.
		if (wasEmpty || !fPlayer.HasFile())
			_Load(first, false, true);
	}
	_PlaylistChanged();
}


// Loads playlist entry index (from its end when atEnd, for playing
// backwards). A file that can't be played is skipped in the direction of
// play, unless the user picked it alone, which gets an alert instead.
bool
PowerPlayApp::_Load(int32 index, bool atEnd, bool play, bool alertOnError)
{
	int32 tries = fPlaylist.CountItems();
	while (index >= 0 && index < fPlaylist.CountItems() && tries-- > 0) {
		fPlaylist.SetCurrent(index);
		entry_ref ref;
		status_t status = get_ref_for_path(fPlaylist.PathAt(index).c_str(),
			&ref);
		if (status == B_OK)
			status = fPlayer.Open(ref, atEnd);
		if (status == B_OK) {
			if (play)
				fPlayer.Play();
			_UpdateWindow();
			_PlaylistChanged();
			return true;
		}

		fprintf(stderr, "PowerPlay: can't play %s: %s\n",
			fPlaylist.PathAt(index).c_str(), strerror(status));
		if (alertOnError) {
			BString text;
			text.SetToFormat("PowerPlay can't play \"%s\".\n\n%s",
				fPlaylist.NameAt(index).c_str(), strerror(status));
			BAlert* alert = new BAlert("Can't play", text.String(), "OK",
				NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go(NULL);
			break;
		}
		index = atEnd ? fPlaylist.Previous(false) : fPlaylist.Next(false);
	}
	_UpdateWindow();
	_PlaylistChanged();
	return false;
}


void
PowerPlayApp::_PlayPause(bool backwards)
{
	if (!fPlayer.HasFile()) {
		if (fPlaylist.CountItems() == 0) {
			_ShowFilePanel(false);
			return;
		}
		int32 index = fPlaylist.Current() != audio::Playlist::kNone
			? fPlaylist.Current() : 0;
		if (!_Load(index, backwards, false))
			return;
	}

	double speed = fPlayer.Speed();
	bool goingBackwards = speed < 0;
	if (fPlayer.IsPlaying() && goingBackwards == backwards) {
		fPlayer.Pause();
	} else {
		// The direction of the button pressed, at the current speed.
		if (speed == 0)
			speed = 1;
		fPlayer.SetSpeed(backwards ? -fabs(speed) : fabs(speed));
		fPlayer.Play();
	}
	_UpdateWindow();
}


void
PowerPlayApp::_Advance(bool backwards, bool wrap)
{
	int32 index = fShuffle && !backwards ? _RandomOther()
		: backwards ? fPlaylist.Previous(wrap) : fPlaylist.Next(wrap);
	if (index == audio::Playlist::kNone)
		return;
	bool playing = fPlayer.IsPlaying();
	_Load(index, fPlayer.Speed() < 0, playing);
}


void
PowerPlayApp::_RemoveFromPlaylist(BMessage* message)
{
	std::vector<int32> indices;
	int32 index;
	for (int32 i = 0; message->FindInt32("index", i, &index) == B_OK; i++)
		indices.push_back(index);
	std::sort(indices.rbegin(), indices.rend());

	int32 current = fPlaylist.Current();
	bool removedCurrent = std::find(indices.begin(), indices.end(), current)
		!= indices.end();
	for (int32 i : indices)
		fPlaylist.Remove(i);

	if (removedCurrent) {
		// What was playing is gone: load what took its place.
		bool playing = fPlayer.IsPlaying();
		fPlayer.Close();
		if (fPlaylist.Current() != audio::Playlist::kNone)
			_Load(fPlaylist.Current(), false, playing);
	}
	_PlaylistChanged();
	_UpdateWindow();
}


void
PowerPlayApp::_PlaylistChanged()
{
	BMessage update(kMsgPlaylistUpdate);
	for (int32 i = 0; i < fPlaylist.CountItems(); i++)
		update.AddString("path", fPlaylist.PathAt(i).c_str());
	update.AddInt32("current", fPlaylist.Current());
	if (fPlaylistWindow != NULL)
		fPlaylistWindow->PostMessage(&update);
	if (fSkinPlaylist != NULL)
		fSkinPlaylist->PostMessage(&update);
}


void
PowerPlayApp::_ShowEqualizer(bool show)
{
	if (fEqWindow == NULL) {
		if (!show)
			return;
		BRect frame = fEqFrame;
		if (!frame.IsValid()) {
			// Under the controller, the first time
			frame = BRect(0, 0, 100, 100).OffsetToCopy(
				fWindowFrame.left, fWindowFrame.bottom + 40);
		}
		fEqWindow = new EqualizerWindow(frame);
		fEqWindow->SetEqualizer(fEqOn, fEqPreamp, fEqBands);
	}
	_ShowWindow(fEqWindow, show);
	if (show && fEqWindow->Lock()) {
		fEqWindow->Activate();
		fEqWindow->Unlock();
	}
}


void
PowerPlayApp::_ShowPlainPanels(bool show)
{
	if (show) {
		if (fPlaylistShown)
			_ShowPlaylist();
		if (fEqShown)
			_ShowEqualizer(true);
		return;
	}
	fPlaylistShown = _IsShown(fPlaylistWindow);
	fEqShown = _IsShown(fEqWindow);
	_ShowWindow(fPlaylistWindow, false);
	_ShowWindow(fEqWindow, false);
}


void
PowerPlayApp::_ShowPlaylist()
{
	if (fPlaylistWindow == NULL) {
		fPlaylistWindow = new PlaylistWindow(fPlaylistFrame);
		_PlaylistChanged();
	}
	if (fPlaylistWindow->Lock()) {
		if (fPlaylistWindow->IsHidden())
			fPlaylistWindow->Show();
		else
			fPlaylistWindow->Activate();
		fPlaylistWindow->Unlock();
	}
}


//	#pragma mark - Window and meters


int32
PowerPlayApp::_RandomOther()
{
	// Shuffle: any other file
	int32 count = fPlaylist.CountItems();
	if (count < 2)
		return count == 1 && fRepeat ? 0 : audio::Playlist::kNone;
	int32 index = (int32)(((uint64)system_time() * 2654435761u) >> 8) % count;
	if (index == fPlaylist.Current())
		index = (index + 1) % count;
	return index;
}


void
PowerPlayApp::_ApplyEqualizer()
{
	fPlayer.SetEqualizer(fEqOn, fEqPreamp, fEqBands);
	if (fEqWindow != NULL && fEqWindow->Lock()) {
		fEqWindow->SetEqualizer(fEqOn, fEqPreamp, fEqBands);
		fEqWindow->Unlock();
	}
	if (fSkinEq == NULL)
		return;
	skin::EqState state;
	state.on = fEqOn;
	state.preamp = fEqPreamp;
	for (int i = 0; i < skin::kEqBands; i++)
		state.bands[i] = fEqBands[i];
	if (fSkinEq->Lock()) {
		fSkinEq->SetEqualizer(state);
		fSkinEq->Unlock();
	}
}


bool
PowerPlayApp::_IsShown(BWindow* window)
{
	if (window == NULL || !window->Lock())
		return false;
	bool shown = !window->IsHidden();
	window->Unlock();
	return shown;
}


void
PowerPlayApp::_ShowWindow(BWindow* window, bool show)
{
	if (window == NULL || !window->Lock())
		return;
	if (show && window->IsHidden())
		window->Show();
	else if (!show && !window->IsHidden())
		window->Hide();
	window->Unlock();
}


void
PowerPlayApp::_ToggleWindow(BWindow* window)
{
	if (window == NULL || !window->Lock())
		return;
	if (window->IsHidden()) {
		window->Show();
		window->Activate();
	} else
		window->Hide();
	window->Unlock();
}


bool
PowerPlayApp::_SkinShown()
{
	if (!fSkinWindow->Lock())
		return false;
	bool shown = !fSkinWindow->IsHidden();
	fSkinWindow->Unlock();
	return shown;
}


// A skin's path, or "" for PowerPlay's own look.
bool
PowerPlayApp::_ApplySkin(const char* path)
{
	if (path == NULL || path[0] == '\0') {
		if (_SkinPathSet()) {
			fSkinEqShown = _IsShown(fSkinEq);
			fSkinPlaylistShown = _IsShown(fSkinPlaylist);
		}
		bool wasSkinned = _SkinPathSet();
		fSkinPath = "";
		_ShowWindow(fSkinEq, false);
		_ShowWindow(fSkinPlaylist, false);
		if (wasSkinned)
			_ShowPlainPanels(true);
		if (fSkinWindow->Lock()) {
			if (!fSkinWindow->IsHidden())
				fSkinWindow->Hide();
			fSkinWindow->Unlock();
		}
		if (fWindow->Lock()) {
			if (fWindow->IsHidden())
				fWindow->Show();
			fWindow->Unlock();
		}
		return true;
	}

	skin::WinampSkin* newSkin = new skin::WinampSkin;
	if (!load_skin(*newSkin, path)) {
		delete newSkin;
		BString text;
		text.SetToFormat("\"%s\" isn't a Winamp skin PowerPlay can use.",
			BPath(path).Leaf());
		BAlert* alert = new BAlert("Skin", text.String(), "OK", NULL, NULL,
			B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->Go(NULL);
		return false;
	}
	// What the skin lacks (often the playlist or equalizer) comes from
	// PowerPlay's own, as Winamp's base skin fills in for its
	if (!fBaseSkinLoaded)
		fBaseSkinLoaded = load_skin(fBaseSkin, kBuiltInSkin);
	if (fBaseSkinLoaded)
		newSkin->FillMissing(fBaseSkin);
	std::shared_ptr<skin::WinampSkin> shared(newSkin);
	bool wasSkinned = _SkinPathSet();
	fSkinPath = path;
	if (fSkinEq->Lock()) {
		fSkinEq->SetSkin(shared);
		fSkinEq->Unlock();
	}
	if (fSkinPlaylist->Lock()) {
		fSkinPlaylist->SetSkin(shared);
		fSkinPlaylist->Unlock();
	}
	if (!wasSkinned) {
		_ShowPlainPanels(false);
		_ShowWindow(fSkinEq, fSkinEqShown);
		_ShowWindow(fSkinPlaylist, fSkinPlaylistShown);
		_ApplyEqualizer();
		_PlaylistChanged();
	}
	if (fSkinWindow->Lock()) {
		fSkinWindow->SetSkin(shared, path);
		if (fSkinWindow->IsHidden())
			fSkinWindow->Show();
		fSkinWindow->Activate();
		fSkinWindow->Unlock();
	}
	if (fWindow->Lock()) {
		if (!fWindow->IsHidden())
			fWindow->Hide();
		fWindow->Unlock();
	}
	_UpdateWindow();
	return true;
}


void
PowerPlayApp::_UpdateSkin(float left, float right)
{
	skin::State state;
	bool playing = fPlayer.IsPlaying();
	double position = fPlayer.Position();
	double duration = fPlayer.Duration();
	state.hasFile = fPlayer.HasFile();
	state.playback = playing ? skin::State::kPlaying
		: state.hasFile && position > 0 ? skin::State::kPaused
		: skin::State::kStopped;
	state.seconds = (int)position;
	if (state.hasFile) {
		// "1. Name (5:16)", as Winamp shows it
		BString name = fPlayer.FileName();
		int32 dot = name.FindLast('.');
		if (dot > 0)
			name.Truncate(dot);
		int length = (int)duration;
		BString title;
		title.SetToFormat("%d. %s (%d:%02d)", fPlaylist.Current() + 1,
			name.String(), length / 60, length % 60);
		state.title = title.String();
	} else
		state.title = kAppName;
	state.kbps = fPlayer.Bitrate();
	state.khz = (fPlayer.SampleRate() + 500) / 1000;
	state.channels = fPlayer.Channels() >= 2 ? 2 : fPlayer.Channels();
	state.position = duration > 0 ? position / duration : 0;
	state.volume = fVolume;
	state.balance = fBalance;
	state.shuffle = fShuffle;
	state.repeat = fRepeat;
	state.playlist = _IsShown(fSkinPlaylist);
	state.equalizer = _IsShown(fSkinEq);
	if (state.playlist && fSkinPlaylist->Lock()) {
		fSkinPlaylist->SetTimes(position, duration);
		fSkinPlaylist->Unlock();
	}
	state.levelLeft = left;
	state.levelRight = right;
	if (fSkinWindow->Lock()) {
		if (!fSkinWindow->IsHidden())
			fSkinWindow->SetState(state);
		fSkinWindow->Unlock();
	}
}


void
PowerPlayApp::_UpdateWindow()
{
	float left, right;
	fPlayer.Levels(left, right);
	_UpdateSkin(left, right);
	if (!fWindow->Lock())
		return;
	fWindow->SetTitle(fPlayer.FileName(), fPlayer.FormatName());
	fWindow->SetState(fPlayer.IsPlaying(), fPlayer.Speed(),
		fPlayer.HasFile());
	fWindow->SetPosition(fPlayer.Position(), fPlayer.Duration());
	fWindow->Unlock();
}


void
PowerPlayApp::_PublishLevels()
{
	float left, right;
	fPlayer.Levels(left, right);

	if (fWindow->Lock()) {
		fWindow->SetLevels(left, right);
		// The time and position bar ten times a second
		if (++fTicks % 3 == 0)
			fWindow->SetPosition(fPlayer.Position(), fPlayer.Duration());
		fWindow->Unlock();
	}
	_UpdateSkin(left, right);

	BMessage levels(kMsgMeterLevels);
	levels.AddFloat("left", left);
	levels.AddFloat("right", right);
	levels.AddBool("playing", fPlayer.IsPlaying());
	// Never wait for a meter: a full port just drops one update.
	for (size_t i = 0; i < fMeters.size();) {
		status_t status = fMeters[i].SendMessage(&levels, (BHandler*)NULL, 0);
		if (status == B_BAD_PORT_ID || !fMeters[i].IsValid())
			fMeters.erase(fMeters.begin() + i);
		else
			i++;
	}
}


void
PowerPlayApp::_ToggleDeskbar()
{
	BDeskbar deskbar;
	if (deskbar.HasItem(kDeskbarItemName)) {
		deskbar.RemoveItem(kDeskbarItemName);
		return;
	}
	DeskbarVU meter;
	status_t status = deskbar.AddItem(&meter);
	if (status != B_OK) {
		BString text;
		text.SetToFormat("PowerPlay couldn't add its meter to the Deskbar."
			"\n\n%s", strerror(status));
		BAlert* alert = new BAlert("Deskbar", text.String(), "OK",
			NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->Go(NULL);
	}
}


void
PowerPlayApp::_ShowFilePanel(bool add)
{
	BFilePanel*& panel = add ? fAddPanel : fOpenPanel;
	if (panel == NULL) {
		BMessage message(add ? (uint32)kMsgPlaylistAdd
			: (uint32)B_REFS_RECEIVED);
		panel = new BFilePanel(B_OPEN_PANEL, NULL, NULL,
			B_FILE_NODE | B_DIRECTORY_NODE, true, &message);
		panel->Window()->SetTitle(add ? "PowerPlay: Add files"
			: "PowerPlay: Open");
	}
	panel->Show();
}


//	#pragma mark - Settings


void
PowerPlayApp::_LoadSettings()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK
		|| path.Append(kSettingsName) != B_OK)
		return;
	BFile file(path.Path(), B_READ_ONLY);
	BMessage settings;
	if (file.InitCheck() != B_OK || settings.Unflatten(&file) != B_OK)
		return;

	BRect screen = BScreen().Frame();
	BRect frame;
	if (settings.FindRect("window frame", &frame) == B_OK
		&& screen.Contains(frame.LeftTop() + BPoint(20, 10)))
		fWindowFrame = frame;
	if (settings.FindRect("playlist frame", &frame) == B_OK
		&& screen.Contains(frame.LeftTop() + BPoint(20, 10)))
		fPlaylistFrame = frame;
	fVolume = settings.GetFloat("volume", fVolume);
	fRepeat = settings.GetBool("repeat", fRepeat);
	fWindowMeter = settings.GetBool("window meter", fWindowMeter);
	fLively = settings.GetBool("lively needles", fLively);
	fShuffle = settings.GetBool("shuffle", fShuffle);
	fBalance = settings.GetFloat("balance", fBalance);
	fSkinPath = settings.GetString("skin", "");
	fSkinDoubleSize = settings.GetBool("skin double size", fSkinDoubleSize);
	BPoint eqWhere;
	if (settings.FindPoint("skin eq where", &eqWhere) == B_OK
		&& screen.Contains(eqWhere + BPoint(20, 10)))
		fSkinEqWhere = eqWhere;
	fSkinEqShown = settings.GetBool("skin eq shown", fSkinEqShown);
	if (settings.FindRect("equalizer frame", &frame) == B_OK
		&& screen.Contains(frame.LeftTop() + BPoint(20, 10)))
		fEqFrame = frame;
	fEqShown = settings.GetBool("equalizer shown", fEqShown);
	if (settings.FindRect("skin playlist frame", &frame) == B_OK
		&& screen.Contains(frame.LeftTop() + BPoint(20, 10)))
		fSkinPlaylistFrame = frame;
	fSkinPlaylistShown = settings.GetBool("skin playlist shown",
		fSkinPlaylistShown);
	fEqOn = settings.GetBool("eq on", fEqOn);
	fEqPreamp = settings.GetFloat("eq preamp", fEqPreamp);
	for (int32 i = 0; i < skin::kEqBands; i++)
		settings.FindFloat("eq bands", i, &fEqBands[i]);
	BPoint where;
	if (settings.FindPoint("skin where", &where) == B_OK
		&& screen.Contains(where + BPoint(20, 10)))
		fSkinWhere = where;
	fPlaylistShown = settings.GetBool("playlist shown", false);

	const char* entry;
	for (int32 i = 0; settings.FindString("playlist", i, &entry) == B_OK; i++)
		fPlaylist.Add(entry);
	fPlaylist.SetCurrent(settings.GetInt32("current", -1));
	fResumePosition = settings.GetDouble("position", 0);
}


void
PowerPlayApp::_SaveSettings()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) != B_OK
		|| path.Append(kSettingsName) != B_OK)
		return;
	BMessage settings;
	settings.AddRect("window frame", fWindowFrame);
	settings.AddRect("playlist frame", fPlaylistFrame);
	settings.AddFloat("volume", fVolume);
	settings.AddBool("repeat", fRepeat);
	settings.AddBool("window meter", fWindowMeter);
	settings.AddBool("lively needles", fLively);
	settings.AddBool("shuffle", fShuffle);
	settings.AddFloat("balance", fBalance);
	settings.AddString("skin", fSkinPath);
	settings.AddPoint("skin where", fSkinWhere);
	settings.AddBool("skin double size", fSkinDoubleSize);
	settings.AddPoint("skin eq where", fSkinEqWhere);
	settings.AddBool("skin eq shown", fSkinEqShown);
	if (fEqFrame.IsValid())
		settings.AddRect("equalizer frame", fEqFrame);
	settings.AddBool("equalizer shown", fEqShown);
	settings.AddRect("skin playlist frame", fSkinPlaylistFrame);
	settings.AddBool("skin playlist shown", fSkinPlaylistShown);
	settings.AddBool("eq on", fEqOn);
	settings.AddFloat("eq preamp", fEqPreamp);
	for (int32 i = 0; i < skin::kEqBands; i++)
		settings.AddFloat("eq bands", fEqBands[i]);
	settings.AddBool("playlist shown", fPlaylistShown);
	for (int32 i = 0; i < fPlaylist.CountItems(); i++)
		settings.AddString("playlist", fPlaylist.PathAt(i).c_str());
	settings.AddInt32("current", fPlaylist.Current());
	settings.AddDouble("position", fResumePosition);
	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() == B_OK)
		settings.Flatten(&file);
}


int
main()
{
	PowerPlayApp app;
	app.Run();
	return 0;
}

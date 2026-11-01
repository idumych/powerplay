/*
 * PowerPlay — an audio player with a VU meter in the Deskbar.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#include <Alert.h>
#include <Application.h>
#include <Deskbar.h>
#include <Directory.h>
#include <File.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <MessageRunner.h>
#include <Path.h>
#include <PropertyInfo.h>
#include <Screen.h>

#include <stdio.h>
#include <string.h>
#include <vector>

#include "Constants.h"
#include "DeskbarVU.h"
#include "Player.h"
#include "PlayerWindow.h"

static const char* kSettingsName = "PowerPlay_settings";

// Scripting, for tests and for hey: the needles' deflection.
static property_info sProperties[] = {
	{ "Levels", { B_GET_PROPERTY, 0 }, { B_DIRECT_SPECIFIER, 0 },
		"Returns the needles' deflection and whether PowerPlay is playing, "
		"as \"left=L right=R playing=0|1\" (1.0 = full scale).", 0,
		{ B_STRING_TYPE } },
	{ 0 }
};


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
			void				_Open(const entry_ref& ref);
			void				_UpdateWindow();
			void				_PublishLevels();
			void				_ToggleDeskbar();
			void				_LoadSettings();
			void				_SaveSettings();

			Player				fPlayer;
			PlayerWindow*		fWindow;
			BFilePanel*			fOpenPanel;
			BMessageRunner*		fMeterRunner;
			std::vector<BMessenger> fMeters;
			BRect				fWindowFrame;
};


PowerPlayApp::PowerPlayApp()
	:
	BApplication(kAppSignature),
	fWindow(NULL),
	fOpenPanel(NULL),
	fMeterRunner(NULL),
	fWindowFrame(100, 100, 420, 200)
{
	_LoadSettings();
	fWindow = new PlayerWindow(fWindowFrame);
}


PowerPlayApp::~PowerPlayApp()
{
	delete fMeterRunner;
	delete fOpenPanel;
}


void
PowerPlayApp::ReadyToRun()
{
	BMessage publish(kMsgPublishLevels);
	fMeterRunner = new BMessageRunner(BMessenger(this), &publish,
		kMeterInterval);
	fWindow->Show();
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
	// One file for now; a playlist comes later.
	entry_ref ref;
	if (message->FindRef("refs", &ref) == B_OK)
		_Open(ref);
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

	float left, right;
	fPlayer.Levels(left, right);
	BString levels;
	levels.SetToFormat("left=%.4f right=%.4f playing=%d", left, right,
		fPlayer.IsPlaying() ? 1 : 0);
	BMessage reply(B_REPLY);
	reply.AddString("result", levels);
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
			if (fOpenPanel == NULL) {
				fOpenPanel = new BFilePanel(B_OPEN_PANEL, NULL, NULL,
					B_FILE_NODE, false);
			}
			fOpenPanel->Show();
			break;

		case kMsgPlayPause:
			if (fPlayer.IsPlaying())
				fPlayer.Pause();
			else
				fPlayer.Play();
			_UpdateWindow();
			break;

		case kMsgStop:
			fPlayer.Stop();
			_UpdateWindow();
			break;

		case kMsgPlaybackEnded:
			_UpdateWindow();
			break;

		case kMsgShowWindow:
			if (fWindow->LockLooper()) {
				if (fWindow->IsHidden())
					fWindow->Show();
				else
					fWindow->Activate();
				fWindow->UnlockLooper();
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

		case kMsgAbout:
		{
			BAlert* alert = new BAlert("About PowerPlay",
				"PowerPlay 0.1.0\n\n"
				"An audio player with a VU meter in the Deskbar.\n\n"
				"Copyright 2026 the PowerPlay authors.\n"
				"Distributed under the MIT licence.",
				"Close");
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go(NULL);
			break;
		}

		default:
			BApplication::MessageReceived(message);
	}
}


bool
PowerPlayApp::QuitRequested()
{
	fPlayer.Close();

	// Leave the Deskbar meter at rest rather than frozen mid-swing.
	BMessage rest(kMsgMeterLevels);
	rest.AddFloat("left", 0);
	rest.AddFloat("right", 0);
	rest.AddBool("playing", false);
	for (BMessenger& meter : fMeters)
		meter.SendMessage(&rest, (BHandler*)NULL, 0);

	if (fWindow->Lock()) {
		fWindowFrame = fWindow->Frame();
		fWindow->Quit();
	}
	_SaveSettings();
	return true;
}


void
PowerPlayApp::_Open(const entry_ref& ref)
{
	status_t status = fPlayer.Open(ref);
	if (status != B_OK) {
		BString text;
		text.SetToFormat("PowerPlay can't play \"%s\".\n\n%s", ref.name,
			strerror(status));
		BAlert* alert = new BAlert("Can't play", text.String(), "OK",
			NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go(NULL);
	} else
		fPlayer.Play();
	_UpdateWindow();
}


void
PowerPlayApp::_UpdateWindow()
{
	if (!fWindow->Lock())
		return;
	fWindow->SetFileName(fPlayer.FileName());
	fWindow->SetPlaying(fPlayer.IsPlaying(), fPlayer.HasFile());
	fWindow->Unlock();
}


void
PowerPlayApp::_PublishLevels()
{
	float left, right;
	fPlayer.Levels(left, right);

	if (fWindow->Lock()) {
		fWindow->SetLevels(left, right);
		fWindow->Unlock();
	}

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
	BRect frame;
	if (settings.FindRect("window frame", &frame) == B_OK
		&& BScreen().Frame().Contains(frame.LeftTop() + BPoint(20, 10)))
		fWindowFrame = frame;
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

/*
 * PowerPlay — the main window: what's playing, the controls and a meter.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#include "PlayerWindow.h"

#include <Application.h>
#include <Button.h>
#include <Deskbar.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <StringView.h>

#include "Constants.h"
#include "VUView.h"


PlayerWindow::PlayerWindow(BRect frame)
	:
	BWindow(frame, kAppName, B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_NOT_V_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_ASYNCHRONOUS_CONTROLS)
{
	BMenuBar* menuBar = new BMenuBar("menu bar");

	BMenu* menu = new BMenu("File");
	menu->AddItem(new BMenuItem("Open" B_UTF8_ELLIPSIS,
		new BMessage(kMsgOpen), 'O'));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("About PowerPlay" B_UTF8_ELLIPSIS,
		new BMessage(kMsgAbout)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED), 'Q'));
	menu->SetTargetForItems(be_app);
	menuBar->AddItem(menu);

	menu = new BMenu("Playback");
	fPlayItem = new BMenuItem("Play", new BMessage(kMsgPlayPause));
	menu->AddItem(fPlayItem);
	fStopItem = new BMenuItem("Stop", new BMessage(kMsgStop));
	menu->AddItem(fStopItem);
	menu->SetTargetForItems(be_app);
	menuBar->AddItem(menu);

	menu = new BMenu("Settings");
	fDeskbarItem = new BMenuItem("Show meter in Deskbar",
		new BMessage(kMsgToggleDeskbar));
	menu->AddItem(fDeskbarItem);
	menu->SetTargetForItems(be_app);
	menuBar->AddItem(menu);

	fFileName = new BStringView("file name", "No file");
	fFileName->SetExplicitMinSize(BSize(240, B_SIZE_UNSET));
	fFileName->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fFileName->SetTruncation(B_TRUNCATE_MIDDLE);

	fPlayButton = new BButton("play", "Play", new BMessage(kMsgPlayPause));
	fPlayButton->SetTarget(be_app);
	fStopButton = new BButton("stop", "Stop", new BMessage(kMsgStop));
	fStopButton->SetTarget(be_app);

	fMeter = new VUView("meter", 2);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL)
			.SetInsets(B_USE_WINDOW_SPACING)
			.Add(fFileName)
			.AddGroup(B_HORIZONTAL)
				.Add(fPlayButton)
				.Add(fStopButton)
				.AddGlue()
				.Add(fMeter)
			.End()
		.End();

	SetPlaying(false, false);
}


bool
PlayerWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return false;
}


void
PlayerWindow::MenusBeginning()
{
	fDeskbarItem->SetMarked(BDeskbar().HasItem(kDeskbarItemName));
}


void
PlayerWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_SIMPLE_DATA:
			// Files dropped on the window
			if (message->HasRef("refs")) {
				message->what = B_REFS_RECEIVED;
				be_app->PostMessage(message);
			}
			break;

		default:
			BWindow::MessageReceived(message);
	}
}


void
PlayerWindow::SetFileName(const char* name)
{
	fFileName->SetText(name != NULL && name[0] != '\0' ? name : "No file");
}


void
PlayerWindow::SetPlaying(bool playing, bool hasFile)
{
	const char* label = playing ? "Pause" : "Play";
	fPlayButton->SetLabel(label);
	fPlayItem->SetLabel(label);
	fPlayButton->SetEnabled(hasFile);
	fStopButton->SetEnabled(hasFile);
	fPlayItem->SetEnabled(hasFile);
	fStopItem->SetEnabled(hasFile);
}


void
PlayerWindow::SetLevels(float left, float right)
{
	fMeter->SetLevels(left, right);
}

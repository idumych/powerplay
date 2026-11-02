/*
 * PowerPlay — the controller: what's playing, the transport buttons, speed,
 * position, volume and time, laid out like SoundPlay's.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#include "PlayerWindow.h"

#include <Application.h>
#include <Box.h>
#include <Deskbar.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <StringView.h>

#include <math.h>

#include "Constants.h"
#include "Skins.h"
#include "Controls.h"
#include "PlayHead.h"
#include "VUView.h"


static BMessage*
Command(uint32 what)
{
	return new BMessage(what);
}


PlayerWindow::PlayerWindow(BRect frame)
	:
	BWindow(frame, kAppName, B_TITLED_WINDOW,
		B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_ASYNCHRONOUS_CONTROLS),
	fShowRemaining(false),
	fLastPosition(0),
	fLastDuration(0)
{
	BMenuBar* menuBar = new BMenuBar("menu bar");

	BMenu* menu = new BMenu("File");
	menu->AddItem(new BMenuItem("Open" B_UTF8_ELLIPSIS, Command(kMsgOpen),
		'O'));
	menu->AddItem(new BMenuItem("Add files" B_UTF8_ELLIPSIS,
		Command(kMsgAddFiles), 'O', B_SHIFT_KEY));
	menu->AddItem(new BMenuItem("Playlist", Command(kMsgShowPlaylist), 'L'));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("About PowerPlay" B_UTF8_ELLIPSIS,
		Command(kMsgAbout)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Quit", Command(B_QUIT_REQUESTED), 'Q'));
	menu->SetTargetForItems(be_app);
	menuBar->AddItem(menu);

	menu = new BMenu("Playback");
	fPlayItem = new BMenuItem("Play", Command(kMsgPlayPause));
	menu->AddItem(fPlayItem);
	fReverseItem = new BMenuItem("Play backwards", Command(kMsgReverse));
	menu->AddItem(fReverseItem);
	menu->AddItem(new BMenuItem("Stop", Command(kMsgStop)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Previous", Command(kMsgPrevious),
		B_LEFT_ARROW));
	menu->AddItem(new BMenuItem("Next", Command(kMsgNext), B_RIGHT_ARROW));
	menu->AddSeparatorItem();
	BMessage* normal = Command(kMsgSetSpeed);
	normal->AddFloat("speed", 100);
	menu->AddItem(new BMenuItem("Normal speed", normal));
	fRepeatItem = new BMenuItem("Repeat playlist", Command(kMsgToggleRepeat));
	menu->AddItem(fRepeatItem);
	menu->SetTargetForItems(be_app);
	menuBar->AddItem(menu);

	menu = new BMenu("Settings");
	fDeskbarItem = new BMenuItem("Show meter in Deskbar",
		Command(kMsgToggleDeskbar));
	menu->AddItem(fDeskbarItem);
	fWindowMeterItem = new BMenuItem("Show meter in window",
		Command(kMsgToggleWindowMeter));
	menu->AddItem(fWindowMeterItem);
	menu->AddSeparatorItem();
	fLivelyItem = new BMenuItem("Lively needles", Command(kMsgToggleLively));
	menu->AddItem(fLivelyItem);
	menu->SetTargetForItems(be_app);
	menuBar->AddItem(menu);

	// This window only shows while no skin is used
	fSkinsMenu = new_skins_menu("");
	menuBar->AddItem(fSkinsMenu);

	fPreviousButton = new SymbolButton("previous", SymbolButton::kPrevious,
		Command(kMsgPrevious));
	fReverseButton = new SymbolButton("reverse", SymbolButton::kReverse,
		Command(kMsgReverse));
	fStopButton = new SymbolButton("stop", SymbolButton::kStop,
		Command(kMsgStop));
	fPlayButton = new SymbolButton("play", SymbolButton::kPlay,
		Command(kMsgPlayPause));
	fNextButton = new SymbolButton("next", SymbolButton::kNext,
		Command(kMsgNext));
	SymbolButton* eject = new SymbolButton("eject", SymbolButton::kEject,
		Command(kMsgOpen));
	SymbolButton* buttons[] = {fPreviousButton, fReverseButton, fStopButton,
		fPlayButton, fNextButton, eject};
	for (SymbolButton* button : buttons)
		button->SetTarget(be_app);

	fSpeed = new SpeedSlider("speed", Command(kMsgSetSpeed));
	fSpeed->SetTarget(this);
	fPosition = new PositionBar("position", Command(kMsgSeek));
	fPosition->SetTarget(this);
	fVolume = new VolumeKnob("volume", Command(kMsgSetVolume));
	fVolume->SetTarget(this);

	// In the plain font, as in the original
	BFont small(be_plain_font);
	fSpeedText = new BStringView("speed text", "Speed: 100%");
	fTimeText = new BStringView("time text", "Time: 00:00");
	// Room for the widest readings, so the layout doesn't jump
	float textWidth = fmaxf(small.StringWidth("Speed: -400%"),
		small.StringWidth("Time: -0:00:00"));
	fSpeedText->SetExplicitMinSize(BSize(textWidth, B_SIZE_UNSET));
	fTimeText->SetExplicitMinSize(BSize(textWidth, B_SIZE_UNSET));

	fBox = new TitleBox("box");
	fBox->SetTitle(kAppName);

	float unit = ControlUnit();
	// Spacing measured from the original: 2 px between buttons, the
	// controls just under the box's title.
	float gap = roundf(unit / 11);
	BLayoutBuilder::Group<>(fBox, B_HORIZONTAL, roundf(unit / 3))
		.SetInsets(roundf(unit / 3), fBox->TitleHeight() + 2,
			roundf(unit / 3), roundf(unit / 3))
		.AddGroup(B_HORIZONTAL, gap)
			.Add(fPreviousButton)
			.Add(fReverseButton)
			.Add(fStopButton)
			.Add(fPlayButton)
			.Add(fNextButton)
		.End()
		.AddGroup(B_VERTICAL, gap)
			.Add(fSpeed)
			.Add(fPosition)
		.End()
		.Add(fVolume)
		.AddGroup(B_VERTICAL, 0)
			.Add(fSpeedText)
			.Add(fTimeText)
		.End()
		.Add(eject);

	// The optional meter has a row of its own, hidden as a whole so it
	// leaves no gap.
	fMeter = new VUView("meter", 2);
	fMeterRow = new BGroupView("meter row", B_HORIZONTAL, 0);
	BLayoutBuilder::Group<>((BGroupView*)fMeterRow)
		.SetInsets(0, 0, 0, roundf(unit / 3))
		.AddGlue()
		.Add(fMeter)
		.AddGlue();

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL, 0)
			.SetInsets(roundf(unit / 3))
			.Add(fBox)
		.End()
		.Add(fMeterRow);

	SetState(false, 1.0, false);
	SetPosition(0, 0);
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
	fWindowMeterItem->SetMarked(ShowsWindowMeter());
	fill_skins_menu(fSkinsMenu, "");
}


void
PlayerWindow::DispatchMessage(BMessage* message, BHandler* handler)
{
	if (message->what == B_KEY_DOWN && _HandleShortcut(message))
		return;
	BWindow::DispatchMessage(message, handler);
}


// The keys SoundPlay took from WinAmp: z previous, x play, c pause,
// v stop, b next; and space to play or pause.
bool
PlayerWindow::_HandleShortcut(BMessage* message)
{
	int32 modifiers = message->GetInt32("modifiers", 0);
	if ((modifiers & (B_COMMAND_KEY | B_CONTROL_KEY | B_OPTION_KEY)) != 0)
		return false;
	const char* bytes;
	if (message->FindString("bytes", &bytes) != B_OK)
		return false;

	uint32 command = 0;
	switch (bytes[0]) {
		case 'z': case 'Z': command = kMsgPrevious; break;
		case 'x': case 'X':
			// Play: forwards, from a pause or a stop
			if (fPlayButton->GetSymbol() == SymbolButton::kPause)
				return true;
			command = kMsgPlayPause;
			break;
		case 'c': case 'C':
			if (fPlayButton->GetSymbol() != SymbolButton::kPause
				&& fReverseButton->GetSymbol() != SymbolButton::kPause)
				return true;
			command = fPlayButton->GetSymbol() == SymbolButton::kPause
				? kMsgPlayPause : kMsgReverse;
			break;
		case 'v': case 'V': command = kMsgStop; break;
		case 'b': case 'B': command = kMsgNext; break;
		case ' ':
			// Space presses the focused button if there is one.
			if (dynamic_cast<BControl*>(CurrentFocus()) != NULL)
				return false;
			command = kMsgPlayPause;
			break;
		default:
			return false;
	}
	be_app->PostMessage(command);
	return true;
}


void
PlayerWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSetSpeed:
		{
			BMessage speed(kMsgSetSpeed);
			speed.AddFloat("speed", fSpeed->Value());
			be_app->PostMessage(&speed);
			break;
		}

		case kMsgSeek:
			if (fLastDuration > 0) {
				BMessage seek(kMsgSeek);
				seek.AddFloat("fraction", fPosition->Value() / 1000.0f);
				be_app->PostMessage(&seek);
			}
			break;

		case kMsgSetVolume:
		{
			BMessage volume(kMsgSetVolume);
			volume.AddFloat("volume", fVolume->Value() / 100.0f);
			be_app->PostMessage(&volume);
			break;
		}

		case B_SIMPLE_DATA:
			// Files dropped on the window replace the playlist.
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
PlayerWindow::SetTitle(const char* fileName, const char* formatName)
{
	BString label;
	if (fileName == NULL || fileName[0] == '\0')
		label = kAppName;
	else if (formatName != NULL && formatName[0] != '\0')
		label.SetToFormat("%s (%s)", fileName, formatName);
	else
		label = fileName;
	fBox->SetTitle(label);
}


void
PlayerWindow::SetState(bool playing, double speed, bool hasFile)
{
	bool forwards = playing && speed > 0;
	bool backwards = playing && speed < 0;
	fPlayButton->SetSymbol(forwards ? SymbolButton::kPause
		: SymbolButton::kPlay);
	fReverseButton->SetSymbol(backwards ? SymbolButton::kPause
		: SymbolButton::kReverse);
	fPlayItem->SetLabel(forwards ? "Pause" : "Play");
	fReverseItem->SetLabel(backwards ? "Pause" : "Play backwards");

	SymbolButton* buttons[] = {fPlayButton, fReverseButton, fStopButton,
		fPreviousButton, fNextButton};
	for (SymbolButton* button : buttons)
		button->SetEnabled(hasFile);
	fPlayItem->SetEnabled(hasFile);
	fReverseItem->SetEnabled(hasFile);
	fPosition->SetEnabled(hasFile);

	int32 percent = (int32)lround(speed * 100);
	if (fSpeed->Value() != percent && !fSpeed->IsTracking())
		fSpeed->SetValue(percent);
	BString text;
	text.SetToFormat("Speed: %" B_PRId32 "%%", percent);
	fSpeedText->SetText(text);
}


void
PlayerWindow::SetPosition(double position, double duration)
{
	fLastPosition = position;
	fLastDuration = duration;
	if (!fPosition->IsTracking()) {
		int32 value = duration > 0
			? (int32)lround(position * 1000 / duration) : 0;
		if (value != fPosition->Value())
			fPosition->SetValue(value);
	}
	char time[32];
	if (fShowRemaining && duration > 0) {
		audio::FormatTime(duration - position, time, sizeof(time));
		BString text;
		text.SetToFormat("Time: -%s", time);
		fTimeText->SetText(text);
	} else {
		audio::FormatTime(position, time, sizeof(time));
		BString text;
		text.SetToFormat("Time: %s", time);
		fTimeText->SetText(text);
	}
}


void
PlayerWindow::SetVolume(float volume)
{
	int32 value = (int32)lroundf(volume * 100);
	if (value != fVolume->Value())
		fVolume->SetValue(value);
}


void
PlayerWindow::SetRepeat(bool repeat)
{
	fRepeatItem->SetMarked(repeat);
}


void
PlayerWindow::SetLively(bool lively)
{
	fLivelyItem->SetMarked(lively);
}


void
PlayerWindow::SetLevels(float left, float right)
{
	if (!fMeterRow->IsHidden(fMeterRow))
		fMeter->SetLevels(left, right);
}


void
PlayerWindow::SetWindowMeter(bool show)
{
	if (show && fMeterRow->IsHidden(fMeterRow))
		fMeterRow->Show();
	else if (!show && !fMeterRow->IsHidden(fMeterRow))
		fMeterRow->Hide();
	// Not resizable: fit the window to what it shows now.
	ResizeToPreferred();
}


bool
PlayerWindow::ShowsWindowMeter() const
{
	return !fMeterRow->IsHidden(fMeterRow);
}

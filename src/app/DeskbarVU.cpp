/*
 * PowerPlay — the VU meter as a Deskbar replicant.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#include "DeskbarVU.h"

#include <Deskbar.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <PopUpMenu.h>
#include <Roster.h>

#include "Constants.h"

static const uint32 kMsgCheckSubscription = 'Chck';
static const uint32 kMsgRemoveReplicant = 'Rmve';
static const bigtime_t kCheckInterval = 1000000;


DeskbarVU::DeskbarVU()
	:
	VUView(kDeskbarItemName),
	fRunner(NULL),
	fLastLevels(0),
	fPlaying(false)
{
}


DeskbarVU::DeskbarVU(BMessage* archive)
	:
	VUView(archive),
	fRunner(NULL),
	fLastLevels(0),
	fPlaying(false)
{
}


DeskbarVU::~DeskbarVU()
{
	delete fRunner;
}


/*static*/ DeskbarVU*
DeskbarVU::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "DeskbarVU"))
		return NULL;
	return new DeskbarVU(archive);
}


status_t
DeskbarVU::Archive(BMessage* archive, bool deep) const
{
	status_t status = VUView::Archive(archive, deep);
	if (status == B_OK)
		status = archive->AddString("add_on", kAppSignature);
	if (status == B_OK)
		status = archive->AddString("class", "DeskbarVU");
	return status;
}


void
DeskbarVU::AttachedToWindow()
{
	VUView::AttachedToWindow();
	BMessage check(kMsgCheckSubscription);
	fRunner = new BMessageRunner(BMessenger(this), &check, kCheckInterval);
	_Subscribe();
}


void
DeskbarVU::DetachedFromWindow()
{
	delete fRunner;
	fRunner = NULL;
	VUView::DetachedFromWindow();
}


void
DeskbarVU::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgMeterLevels:
		{
			float left = 0, right = 0;
			message->FindFloat("left", &left);
			message->FindFloat("right", &right);
			fPlaying = message->GetBool("playing", false);
			fLastLevels = system_time();
			SetLevels(left, right);
			break;
		}

		case kMsgCheckSubscription:
			if (system_time() - fLastLevels > kCheckInterval) {
				// PowerPlay quit or isn't running yet: rest the needles
				// and look for it again.
				fPlaying = false;
				SetLevels(0, 0);
				_Subscribe();
			}
			break;

		case kMsgPlayPause:
		case kMsgStop:
			BMessenger(kAppSignature).SendMessage(message);
			break;

		case kMsgShowWindow:
			if (be_roster->IsRunning(kAppSignature))
				BMessenger(kAppSignature).SendMessage(message);
			else
				be_roster->Launch(kAppSignature);
			break;

		case kMsgRemoveReplicant:
			BDeskbar().RemoveItem(kDeskbarItemName);
			break;

		default:
			VUView::MessageReceived(message);
	}
}


void
DeskbarVU::MouseDown(BPoint where)
{
	_ShowMenu(ConvertToScreen(where));
}


void
DeskbarVU::_Subscribe()
{
	BMessenger app(kAppSignature);
	if (!app.IsValid())
		return;
	BMessage subscribe(kMsgMeterSubscribe);
	subscribe.AddMessenger("meter", BMessenger(this));
	app.SendMessage(&subscribe, (BHandler*)NULL, 0);
}


void
DeskbarVU::_ShowMenu(BPoint where)
{
	bool running = be_roster->IsRunning(kAppSignature);

	BPopUpMenu* menu = new BPopUpMenu("PowerPlay", false, false);
	menu->SetFont(be_plain_font);

	BMenuItem* item = new BMenuItem(fPlaying ? "Pause" : "Play",
		new BMessage(kMsgPlayPause));
	item->SetEnabled(running);
	menu->AddItem(item);
	item = new BMenuItem("Stop", new BMessage(kMsgStop));
	item->SetEnabled(running);
	menu->AddItem(item);
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Show PowerPlay",
		new BMessage(kMsgShowWindow)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Remove from Deskbar",
		new BMessage(kMsgRemoveReplicant)));

	menu->SetTargetForItems(this);
	menu->SetAsyncAutoDestruct(true);
	menu->Go(where, true, true, true);
}

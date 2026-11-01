/*
 * PowerPlay — the playlist editor.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#include "PlaylistWindow.h"

#include <Application.h>
#include <Button.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <ScrollView.h>
#include <StringView.h>

#include <stdio.h>

#include "Constants.h"

static const uint32 kMsgSelectionChanged = 'PlSl';
static const uint32 kMsgInvoked = 'PlIv';


// One entry: the file name, bold while it's the one playing.
class PlaylistItem : public BStringItem {
public:
	PlaylistItem(const char* name, bool current)
		:
		BStringItem(name),
		fCurrent(current)
	{
	}

	virtual void DrawItem(BView* owner, BRect frame, bool complete)
	{
		if (fCurrent) {
			owner->PushState();
			owner->SetFont(be_bold_font);
			BStringItem::DrawItem(owner, frame, complete);
			owner->PopState();
		} else
			BStringItem::DrawItem(owner, frame, complete);
	}

	virtual void Update(BView* owner, const BFont* font)
	{
		BStringItem::Update(owner, fCurrent ? be_bold_font : font);
	}

private:
	bool fCurrent;
};


// The list: Delete removes, and files dropped on it are added where they
// land.
class PlaylistView : public BListView {
public:
	PlaylistView()
		:
		BListView("playlist", B_MULTIPLE_SELECTION_LIST)
	{
	}

	virtual void KeyDown(const char* bytes, int32 count)
	{
		if (bytes[0] == B_DELETE || bytes[0] == B_BACKSPACE) {
			Window()->PostMessage(kMsgPlaylistRemove);
			return;
		}
		BListView::KeyDown(bytes, count);
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->WasDropped() && message->HasRef("refs")) {
			BPoint where = ConvertFromScreen(message->DropPoint());
			int32 index = IndexOf(where);
			BMessage add(*message);
			add.what = kMsgPlaylistAdd;
			add.AddInt32("index", index);
			be_app->PostMessage(&add);
			return;
		}
		BListView::MessageReceived(message);
	}
};


PlaylistWindow::PlaylistWindow(BRect frame)
	:
	BWindow(frame, "Playlist", B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS)
{
	fList = new PlaylistView();
	fList->SetSelectionMessage(new BMessage(kMsgSelectionChanged));
	fList->SetInvocationMessage(new BMessage(kMsgInvoked));
	BScrollView* scroll = new BScrollView("scroll", fList, 0, false, true);

	BButton* add = new BButton("add", "Add" B_UTF8_ELLIPSIS,
		new BMessage(kMsgAddFiles));
	add->SetTarget(be_app);
	fRemoveButton = new BButton("remove", "Remove",
		new BMessage(kMsgPlaylistRemove));
	BButton* sortName = new BButton("sort name", "Sort by name",
		new BMessage(kMsgPlaylistSortName));
	sortName->SetTarget(be_app);
	BButton* sortPath = new BButton("sort path", "Sort by path",
		new BMessage(kMsgPlaylistSortPath));
	sortPath->SetTarget(be_app);
	BButton* shuffle = new BButton("shuffle", "Shuffle",
		new BMessage(kMsgPlaylistShuffle));
	shuffle->SetTarget(be_app);
	fCount = new BStringView("count", "");

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(scroll)
		.AddGroup(B_HORIZONTAL)
			.Add(add)
			.Add(fRemoveButton)
			.AddGlue()
			.Add(fCount)
		.End()
		.AddGroup(B_HORIZONTAL)
			.Add(sortName)
			.Add(sortPath)
			.Add(shuffle)
			.AddGlue()
		.End();

	scroll->SetExplicitMinSize(BSize(260, 160));
	_UpdateButtons();
}


PlaylistWindow::~PlaylistWindow()
{
	// The list doesn't own its items.
	for (int32 i = fList->CountItems() - 1; i >= 0; i--)
		delete fList->RemoveItem(i);
}


bool
PlaylistWindow::QuitRequested()
{
	// Closing only hides it; the app keeps it for next time.
	if (!IsHidden())
		Hide();
	return false;
}


void
PlaylistWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgPlaylistUpdate:
			_Update(message);
			break;

		case kMsgSelectionChanged:
			_UpdateButtons();
			break;

		case kMsgInvoked:
		{
			int32 index = fList->CurrentSelection();
			if (index >= 0) {
				BMessage play(kMsgPlaylistPlay);
				play.AddInt32("index", index);
				be_app->PostMessage(&play);
			}
			break;
		}

		case kMsgPlaylistRemove:
			_RemoveSelected();
			break;

		case B_SIMPLE_DATA:
			if (message->HasRef("refs")) {
				message->what = kMsgPlaylistAdd;
				be_app->PostMessage(message);
			}
			break;

		default:
			BWindow::MessageReceived(message);
	}
}


void
PlaylistWindow::_Update(BMessage* update)
{
	int32 current = update->GetInt32("current", -1);
	// Keep the selection by index; edits come from here mostly.
	int32 selected = fList->CurrentSelection();

	// The list doesn't own its items.
	for (int32 i = fList->CountItems() - 1; i >= 0; i--)
		delete fList->RemoveItem(i);
	const char* path;
	for (int32 i = 0; update->FindString("path", i, &path) == B_OK; i++) {
		const char* slash = strrchr(path, '/');
		fList->AddItem(new PlaylistItem(slash != NULL ? slash + 1 : path,
			i == current));
	}
	if (selected >= 0 && selected < fList->CountItems())
		fList->Select(selected);

	int32 count = fList->CountItems();
	BString text;
	text.SetToFormat(count == 1 ? "%" B_PRId32 " file" : "%" B_PRId32
		" files", count);
	fCount->SetText(text);
	_UpdateButtons();
}


void
PlaylistWindow::_RemoveSelected()
{
	BMessage remove(kMsgPlaylistRemove);
	int32 index;
	for (int32 i = 0; (index = fList->CurrentSelection(i)) >= 0; i++)
		remove.AddInt32("index", index);
	if (remove.HasInt32("index"))
		be_app->PostMessage(&remove);
}


void
PlaylistWindow::_UpdateButtons()
{
	fRemoveButton->SetEnabled(fList->CurrentSelection() >= 0);
}

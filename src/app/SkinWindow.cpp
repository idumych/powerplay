/*
 * PowerPlay — the main window drawn by a classic Winamp skin.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#include "SkinWindow.h"

#include <Application.h>
#include <Bitmap.h>
#include <Deskbar.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Screen.h>
#include <View.h>

#include <math.h>
#include <memory>
#include <string>

#include "Constants.h"
#include "Skins.h"

using skin::Control;


class SkinView : public BView {
public:
								SkinView();
	virtual						~SkinView();

	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual	void				MouseUp(BPoint where);
	virtual	void				MessageReceived(BMessage* message);

			void				SetSkin(std::shared_ptr<skin::WinampSkin> skin);
			void				SetState(const skin::State& state);
			void				SetActive(bool active);
			void				SetScale(int scale);

private:
			// A point in the view, in the skin's 275 x 116 pixels
			BPoint				_SkinPoint(BPoint where) const
									{ return BPoint(floorf(where.x / fScale),
										floorf(where.y / fScale)); }
			void				_Redraw();
			void				_SliderTo(Control slider, BPoint where);
			void				_Command(Control control);
			void				_ShowMenu(BPoint screenWhere);

			std::shared_ptr<skin::WinampSkin> fSkin;
			BBitmap*			fBitmap;
			skin::State			fState;
			bool				fShowRemaining;
			Control				fTracking;		// the control held down
			bool				fInside;		// pointer still over it
			BPoint				fDragOffset;	// moving the window
			std::string			fRawTitle;
			int					fScroll;
			bigtime_t			fLastScroll;
			int					fScale;
};


static const bigtime_t kScrollInterval = 50000;	// 20 px a second


SkinView::SkinView()
	:
	BView(BRect(0, 0, skin::kMainWidth - 1, skin::kMainHeight - 1), "skin",
		B_FOLLOW_ALL, B_WILL_DRAW),
	fBitmap(new BBitmap(BRect(0, 0, skin::kMainWidth - 1,
		skin::kMainHeight - 1), B_RGB32)),
	fShowRemaining(false),
	fTracking(skin::kNoControl),
	fInside(false),
	fScroll(0),
	fLastScroll(0),
	fScale(1)
{
	SetViewColor(B_TRANSPARENT_COLOR);
}


SkinView::~SkinView()
{
	delete fBitmap;
}


void
SkinView::SetSkin(std::shared_ptr<skin::WinampSkin> newSkin)
{
	fSkin = newSkin;
	_Redraw();
}


void
SkinView::SetState(const skin::State& state)
{
	// Keep what's ours: the pressed control, the scroll offset, focus.
	skin::State next = state;
	next.pressed = fTracking != skin::kNoControl && fInside ? fTracking
		: skin::kNoControl;
	next.active = fState.active;
	next.doubleSize = fState.doubleSize;
	if (fShowRemaining && next.hasFile)
		next.seconds = -next.seconds;
	// While dragging a slider, it shows where it is, not the player.
	if (fTracking == skin::kVolume)
		next.volume = fState.volume;
	if (fTracking == skin::kBalance)
		next.balance = fState.balance;
	if (fTracking == skin::kPosition)
		next.position = fState.position;

	// A title that doesn't fit scrolls, looping with a separator.
	std::string raw = state.title;
	bool titleChanged = raw != fRawTitle;
	fRawTitle = raw;
	int space = skin::ControlRect(skin::kTitleText).width;
	next.titleOffset = 0;
	if (fSkin && fSkin->TextWidth(raw) > space) {
		std::string loop = raw + "  ***  ";
		bigtime_t now = system_time();
		if (titleChanged)
			fScroll = 0;
		else if (now - fLastScroll >= kScrollInterval) {
			fLastScroll = now;
			fScroll = (fScroll + 1) % fSkin->TextWidth(loop);
		}
		next.title = loop + loop;	// so the end runs into the start
		next.titleOffset = fScroll;
	} else
		fScroll = 0;

	fState = next;
	_Redraw();
}


void
SkinView::SetActive(bool active)
{
	fState.active = active;
	_Redraw();
}


void
SkinView::SetScale(int scale)
{
	fScale = scale;
	fState.doubleSize = scale == 2;
	_Redraw();
}


void
SkinView::_Redraw()
{
	if (!fSkin || fBitmap->InitCheck() != B_OK)
		return;
	fSkin->Render(fState, (uint32_t*)fBitmap->Bits(), fBitmap->BytesPerRow());
	Invalidate();
}


void
SkinView::Draw(BRect updateRect)
{
	DrawBitmap(fBitmap, fBitmap->Bounds(), Bounds());
}


void
SkinView::MouseDown(BPoint where)
{
	int32 buttons = 0;
	if (Window()->CurrentMessage() != NULL)
		Window()->CurrentMessage()->FindInt32("buttons", &buttons);
	if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
		_ShowMenu(ConvertToScreen(where));
		return;
	}

	BPoint point = _SkinPoint(where);
	Control control = skin::HitTest((int)point.x, (int)point.y);
	fTracking = control;
	fInside = true;
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);

	switch (control) {
		case skin::kTitleBar:
		case skin::kNoControl:
		case skin::kVisualizer:
		case skin::kTitleText:
			// Drag the window by anything that isn't a control
			fTracking = skin::kTitleBar;
			fDragOffset = where;
			return;
		case skin::kMenuButton:
			_ShowMenu(ConvertToScreen(BPoint(6 * fScale, 12 * fScale)));
			fTracking = skin::kNoControl;
			return;
		case skin::kTime:
			fShowRemaining = !fShowRemaining;
			fState.seconds = -fState.seconds;
			fTracking = skin::kNoControl;
			_Redraw();
			return;
		case skin::kPosition:
		case skin::kVolume:
		case skin::kBalance:
			if (control == skin::kPosition && !fState.hasFile) {
				fTracking = skin::kNoControl;
				return;
			}
			_SliderTo(control, point);
			break;
		default:
			break;
	}
	fState.pressed = fTracking;
	_Redraw();
}


void
SkinView::MouseMoved(BPoint where, uint32 transit, const BMessage* dragMessage)
{
	if (fTracking == skin::kNoControl)
		return;
	uint32 buttons = 0;
	if (Window()->CurrentMessage() != NULL)
		Window()->CurrentMessage()->FindInt32("buttons", (int32*)&buttons);
	if (buttons == 0 && fTracking == skin::kTitleBar) {
		// The release went elsewhere (on Wayland the compositor moves the
		// window and keeps it)
		fTracking = skin::kNoControl;
		return;
	}
	if (fTracking == skin::kTitleBar) {
		Window()->MoveTo(ConvertToScreen(where) - fDragOffset);
		return;
	}
	if (fTracking == skin::kPosition || fTracking == skin::kVolume
		|| fTracking == skin::kBalance) {
		_SliderTo(fTracking, _SkinPoint(where));
		return;
	}
	BPoint point = _SkinPoint(where);
	bool inside = skin::ControlRect(fTracking).Contains((int)point.x,
		(int)point.y);
	if (inside != fInside) {
		fInside = inside;
		fState.pressed = inside ? fTracking : skin::kNoControl;
		_Redraw();
	}
}


void
SkinView::MouseUp(BPoint viewWhere)
{
	BPoint where = _SkinPoint(viewWhere);
	Control control = fTracking;
	fTracking = skin::kNoControl;
	fState.pressed = skin::kNoControl;
	if (control == skin::kPosition) {
		// Seek once, where it was let go
		BMessage seek(kMsgSeek);
		seek.AddFloat("fraction", (float)fState.position);
		be_app->PostMessage(&seek);
	} else if (control != skin::kTitleBar && control != skin::kVolume
		&& control != skin::kBalance && control != skin::kNoControl
		&& skin::ControlRect(control).Contains((int)where.x, (int)where.y))
		_Command(control);
	_Redraw();
}


void
SkinView::_SliderTo(Control slider, BPoint where)
{
	double value = skin::SliderValue(slider, (int)where.x);
	if (slider == skin::kPosition) {
		fState.position = value;
	} else if (slider == skin::kVolume) {
		fState.volume = value;
		BMessage volume(kMsgSetVolume);
		volume.AddFloat("volume", (float)value);
		be_app->PostMessage(&volume);
	} else {
		fState.balance = value;
		BMessage balance(kMsgSetBalance);
		balance.AddFloat("balance", (float)value);
		be_app->PostMessage(&balance);
	}
	fState.pressed = slider;
	_Redraw();
}


void
SkinView::_Command(Control control)
{
	switch (control) {
		case skin::kPrevious:	be_app->PostMessage(kMsgPrevious); break;
		case skin::kPlay:		be_app->PostMessage(kMsgPlay); break;
		case skin::kPause:		be_app->PostMessage(kMsgPause); break;
		case skin::kStop:		be_app->PostMessage(kMsgStop); break;
		case skin::kNext:		be_app->PostMessage(kMsgNext); break;
		case skin::kEject:		be_app->PostMessage(kMsgOpen); break;
		case skin::kShuffle:	be_app->PostMessage(kMsgToggleShuffle); break;
		case skin::kRepeat:		be_app->PostMessage(kMsgToggleRepeat); break;
		case skin::kPlaylist:	be_app->PostMessage(kMsgShowPlaylist); break;
		case skin::kEqualizer:	be_app->PostMessage(kMsgShowEqualizer); break;
		case skin::kMinimizeButton:
			Window()->Minimize(true);
			break;
		case skin::kCloseButton:
			be_app->PostMessage(B_QUIT_REQUESTED);
			break;
		case skin::kDoubleSize:
			Window()->PostMessage(kMsgToggleDoubleSize);
			break;
		default:
			break;
	}
}


static BMessage*
Command(uint32 what)
{
	return new BMessage(what);
}


void
SkinView::_ShowMenu(BPoint screenWhere)
{
	// PowerPlay's menus, as a pop-up: a skin has no menu bar.
	BPopUpMenu* menu = new BPopUpMenu("PowerPlay", false, false);
	menu->AddItem(new BMenuItem("Open" B_UTF8_ELLIPSIS, Command(kMsgOpen)));
	menu->AddItem(new BMenuItem("Add files" B_UTF8_ELLIPSIS,
		Command(kMsgAddFiles)));
	menu->AddItem(new BMenuItem("Playlist", Command(kMsgShowPlaylist)));
	menu->AddItem(new BMenuItem("Equalizer", Command(kMsgShowEqualizer)));
	menu->AddSeparatorItem();

	BMenu* playback = new BMenu("Playback");
	playback->AddItem(new BMenuItem(fState.playback == skin::State::kPlaying
		? "Pause" : "Play", Command(kMsgPlayPause)));
	playback->AddItem(new BMenuItem("Play backwards", Command(kMsgReverse)));
	playback->AddItem(new BMenuItem("Stop", Command(kMsgStop)));
	playback->AddSeparatorItem();
	playback->AddItem(new BMenuItem("Previous", Command(kMsgPrevious)));
	playback->AddItem(new BMenuItem("Next", Command(kMsgNext)));
	playback->AddSeparatorItem();
	BMessage* normal = Command(kMsgSetSpeed);
	normal->AddFloat("speed", 100);
	playback->AddItem(new BMenuItem("Normal speed", normal));
	BMenuItem* item = new BMenuItem("Repeat playlist",
		Command(kMsgToggleRepeat));
	item->SetMarked(fState.repeat);
	playback->AddItem(item);
	item = new BMenuItem("Shuffle", Command(kMsgToggleShuffle));
	item->SetMarked(fState.shuffle);
	playback->AddItem(item);
	playback->SetTargetForItems(be_app);
	menu->AddItem(playback);

	SkinWindow* window = dynamic_cast<SkinWindow*>(Window());
	menu->AddItem(new_skins_menu(window != NULL ? window->SkinPath() : NULL));
	BMenuItem* doubleSize = new BMenuItem("Double size",
		Command(kMsgToggleDoubleSize), 'D');
	doubleSize->SetMarked(fScale == 2);
	menu->AddItem(doubleSize);

	BMenu* settings = new BMenu("Settings");
	item = new BMenuItem("Show meter in Deskbar", Command(kMsgToggleDeskbar));
	item->SetMarked(BDeskbar().HasItem(kDeskbarItemName));
	settings->AddItem(item);
	settings->AddItem(new BMenuItem("Lively needles",
		Command(kMsgToggleLively)));
	settings->SetTargetForItems(be_app);
	menu->AddItem(settings);

	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("About PowerPlay" B_UTF8_ELLIPSIS,
		Command(kMsgAbout)));
	menu->AddItem(new BMenuItem("Quit", Command(B_QUIT_REQUESTED)));
	for (int32 i = 0; i < menu->CountItems(); i++) {
		if (menu->ItemAt(i)->Submenu() == NULL)
			menu->ItemAt(i)->SetTarget(be_app);
	}
	doubleSize->SetTarget(Window());
	menu->SetAsyncAutoDestruct(true);
	menu->Go(screenWhere, true, true, true);
}


void
SkinView::MessageReceived(BMessage* message)
{
	if (message->what == B_SIMPLE_DATA && message->HasRef("refs")) {
		// Files dropped on the skin: a skin applies, music plays.
		message->what = B_REFS_RECEIVED;
		be_app->PostMessage(message);
		return;
	}
	BView::MessageReceived(message);
}


//	#pragma mark - SkinWindow


SkinWindow::SkinWindow(BPoint where)
	:
	BWindow(BRect(where, where + BPoint(skin::kMainWidth - 1,
		skin::kMainHeight - 1)), kAppName, B_NO_BORDER_WINDOW_LOOK,
		B_NORMAL_WINDOW_FEEL, B_NOT_RESIZABLE | B_NOT_ZOOMABLE
			| B_ASYNCHRONOUS_CONTROLS),
	fDoubleSize(false)
{
	fView = new SkinView();
	AddChild(fView);
	// Command+Q is every window's; these are the menu bar's elsewhere
	AddShortcut('O', B_COMMAND_KEY, new BMessage(kMsgOpen), be_app);
	AddShortcut('D', B_COMMAND_KEY, new BMessage(kMsgToggleDoubleSize));
}


bool
SkinWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return false;
}


void
SkinWindow::WindowActivated(bool active)
{
	fView->SetActive(active);
}


void
SkinWindow::DispatchMessage(BMessage* message, BHandler* handler)
{
	if (message->what == B_KEY_DOWN && _HandleShortcut(message))
		return;
	BWindow::DispatchMessage(message, handler);
}


// Winamp's keys, which SoundPlay took too: z previous, x play, c pause,
// v stop, b next; space plays or pauses. (Command+O opens and Command+D
// doubles the size: shortcuts, in the constructor.)
bool
SkinWindow::_HandleShortcut(BMessage* message)
{
	int32 modifiers = message->GetInt32("modifiers", 0);
	const char* bytes;
	if (message->FindString("bytes", &bytes) != B_OK)
		return false;
	if ((modifiers & B_COMMAND_KEY) != 0)
		return false;	// the window's shortcuts
	if ((modifiers & (B_CONTROL_KEY | B_OPTION_KEY)) != 0)
		return false;
	uint32 command = 0;
	switch (bytes[0]) {
		case 'z': case 'Z': command = kMsgPrevious; break;
		case 'x': case 'X': command = kMsgPlay; break;
		case 'c': case 'C': command = kMsgPause; break;
		case 'v': case 'V': command = kMsgStop; break;
		case 'b': case 'B': command = kMsgNext; break;
		case ' ': command = kMsgPlayPause; break;
		default:
			return false;
	}
	be_app->PostMessage(command);
	return true;
}


void
SkinWindow::SetSkin(std::shared_ptr<skin::WinampSkin> newSkin,
	const char* path)
{
	fSkinPath = path;
	if (newSkin != NULL)
		SetTitle(BString(kAppName) << ": " << newSkin->Name().c_str());
	fView->SetSkin(newSkin);
}


void
SkinWindow::MessageReceived(BMessage* message)
{
	if (message->what == kMsgToggleDoubleSize) {
		SetDoubleSize(!fDoubleSize);
		return;
	}
	BWindow::MessageReceived(message);
}


void
SkinWindow::SetDoubleSize(bool doubleSize)
{
	fDoubleSize = doubleSize;
	int scale = doubleSize ? 2 : 1;
	ResizeTo(skin::kMainWidth * scale - 1, skin::kMainHeight * scale - 1);
	fView->SetScale(scale);
	// The equalizer doubles with it
	BMessage notice(kMsgSkinDoubleSize);
	notice.AddBool("double", doubleSize);
	be_app->PostMessage(&notice);
}


void
SkinWindow::SetState(const skin::State& state)
{
	fView->SetState(state);
}

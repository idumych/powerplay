/*
 * PowerPlay — a Winamp skin's equalizer and playlist windows.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#include "SkinPanels.h"

#include <Application.h>
#include <Bitmap.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Region.h>
#include <String.h>
#include <View.h>

#include <math.h>
#include <string.h>
#include <string>
#include <vector>

#include "Constants.h"
#include "PlayHead.h"


static const uint32 kMsgSelectNone = 'SlNo';
static const uint32 kMsgInvertSelection = 'SlIn';
static const uint32 kMsgRemoveSelected = 'RmSe';
static const uint32 kMsgCropSelected = 'Crop';


static rgb_color
Color(uint32_t pixel)
{
	return make_color((pixel >> 16) & 0xff, (pixel >> 8) & 0xff, pixel & 0xff);
}


// Lets a borderless window be dragged by its background (see SkinWindow).
static void
DragWindow(BView* view, BPoint where, BPoint offset)
{
	view->Window()->MoveTo(view->ConvertToScreen(where) - offset);
}


static uint32
CurrentButtons(BView* view)
{
	int32 buttons = 0;
	if (view->Window()->CurrentMessage() != NULL)
		view->Window()->CurrentMessage()->FindInt32("buttons", &buttons);
	return (uint32)buttons;
}


//	#pragma mark - Equalizer


// Winamp's presets, in dB from 60 Hz to 16 kHz
struct Preset {
	const char* name;
	float bands[skin::kEqBands];
};

static const Preset kPresets[] = {
	{"Flat", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
	{"Classical", {0, 0, 0, 0, 0, 0, -7, -7, -7, -9}},
	{"Club", {0, 0, 8, 5, 5, 5, 3, 0, 0, 0}},
	{"Dance", {9, 7, 2, 0, 0, -5, -7, -7, 0, 0}},
	{"Full bass", {9, 9, 9, 5, 1, -4, -8, -10, -11, -11}},
	{"Full bass and treble", {7, 5, 0, -7, -5, 1, 8, 11, 12, 12}},
	{"Full treble", {-9, -9, -9, -4, 2, 11, 12, 12, 12, 12}},
	{"Large hall", {10, 10, 5, 5, 0, -4, -4, -4, 0, 0}},
	{"Live", {-4, 0, 4, 5, 5, 5, 4, 2, 2, 2}},
	{"Party", {7, 7, 0, 0, 0, 0, 0, 0, 7, 7}},
	{"Pop", {-1, 4, 7, 8, 5, 0, -2, -2, -1, -1}},
	{"Reggae", {0, 0, 0, -5, 0, 6, 6, 0, 0, 0}},
	{"Rock", {8, 4, -5, -8, -3, 4, 8, 11, 11, 11}},
	{"Soft", {4, 1, 0, -2, 0, 4, 8, 9, 11, 12}},
	{"Techno", {8, 5, 0, -5, -4, 0, 8, 9, 9, 8}},
};


class SkinEqView : public BView {
public:
								SkinEqView();
	virtual						~SkinEqView();

	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual	void				MouseUp(BPoint where);

			void				SetSkin(std::shared_ptr<skin::WinampSkin> skin);
			void				SetEqualizer(const skin::EqState& state);
			void				SetActive(bool active);
			void				SetScale(int scale);

private:
			BPoint				_SkinPoint(BPoint where) const
									{ return BPoint(floorf(where.x / fScale),
										floorf(where.y / fScale)); }
			void				_Redraw();
			void				_SetGain(int control, BPoint point);
			void				_ShowPresets();

			std::shared_ptr<skin::WinampSkin> fSkin;
			BBitmap*			fBitmap;
			skin::EqState		fState;
			int					fScale;
			int					fTracking;
			BPoint				fDragOffset;
};


SkinEqView::SkinEqView()
	:
	BView(BRect(0, 0, skin::kMainWidth - 1, skin::kMainHeight - 1), "eq",
		B_FOLLOW_ALL, B_WILL_DRAW),
	fBitmap(new BBitmap(BRect(0, 0, skin::kMainWidth - 1,
		skin::kMainHeight - 1), B_RGB32)),
	fScale(1),
	fTracking(skin::kEqNone)
{
	SetViewColor(B_TRANSPARENT_COLOR);
}


SkinEqView::~SkinEqView()
{
	delete fBitmap;
}


void
SkinEqView::SetSkin(std::shared_ptr<skin::WinampSkin> skin)
{
	fSkin = skin;
	_Redraw();
}


void
SkinEqView::SetEqualizer(const skin::EqState& state)
{
	skin::EqState next = state;
	next.active = fState.active;
	next.pressed = fState.pressed;
	// A slider being dragged shows where it is
	if (fTracking == skin::kEqPreamp)
		next.preamp = fState.preamp;
	else if (fTracking >= skin::kEqBand)
		next.bands[fTracking - skin::kEqBand]
			= fState.bands[fTracking - skin::kEqBand];
	fState = next;
	_Redraw();
}


void
SkinEqView::SetActive(bool active)
{
	fState.active = active;
	_Redraw();
}


void
SkinEqView::SetScale(int scale)
{
	fScale = scale;
	Invalidate();
}


void
SkinEqView::_Redraw()
{
	if (!fSkin || fBitmap->InitCheck() != B_OK)
		return;
	fSkin->RenderEqualizer(fState, (uint32_t*)fBitmap->Bits(),
		fBitmap->BytesPerRow());
	Invalidate();
}


void
SkinEqView::Draw(BRect updateRect)
{
	DrawBitmap(fBitmap, fBitmap->Bounds(), Bounds());
}


void
SkinEqView::MouseDown(BPoint where)
{
	BPoint point = _SkinPoint(where);
	int control = skin::EqHitTest((int)point.x, (int)point.y);
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	if (control == skin::kEqNone || control == skin::kEqTitleBar) {
		fTracking = skin::kEqTitleBar;
		fDragOffset = where;
		return;
	}
	fTracking = control;
	fState.pressed = control;
	if (control == skin::kEqPreamp || control >= skin::kEqBand)
		_SetGain(control, point);
	_Redraw();
}


void
SkinEqView::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	if (fTracking == skin::kEqNone)
		return;
	if (CurrentButtons(this) == 0 && fTracking == skin::kEqTitleBar) {
		fTracking = skin::kEqNone;
		return;
	}
	if (fTracking == skin::kEqTitleBar) {
		DragWindow(this, where, fDragOffset);
		return;
	}
	BPoint point = _SkinPoint(where);
	if (fTracking == skin::kEqPreamp || fTracking >= skin::kEqBand) {
		_SetGain(fTracking, point);
		return;
	}
	bool inside = skin::EqControlRect(fTracking).Contains((int)point.x,
		(int)point.y);
	int pressed = inside ? fTracking : skin::kEqNone;
	if (pressed != fState.pressed) {
		fState.pressed = pressed;
		_Redraw();
	}
}


void
SkinEqView::MouseUp(BPoint where)
{
	int control = fTracking;
	fTracking = skin::kEqNone;
	fState.pressed = skin::kEqNone;
	BPoint point = _SkinPoint(where);
	bool inside = skin::EqControlRect(control).Contains((int)point.x,
		(int)point.y);
	if (inside && control == skin::kEqOn) {
		BMessage message(kMsgSetEqualizer);
		message.AddBool("on", !fState.on);
		be_app->PostMessage(&message);
	} else if (inside && control == skin::kEqPresets) {
		_ShowPresets();
	} else if (inside && control == skin::kEqClose)
		Window()->Hide();
	_Redraw();
}


void
SkinEqView::_SetGain(int control, BPoint point)
{
	float gain = skin::EqSliderGain((int)point.y);
	BMessage message(kMsgSetEqualizer);
	if (control == skin::kEqPreamp) {
		fState.preamp = gain;
		message.AddInt32("band", -1);
	} else {
		fState.bands[control - skin::kEqBand] = gain;
		message.AddInt32("band", control - skin::kEqBand);
	}
	message.AddFloat("gain", gain);
	be_app->PostMessage(&message);
	_Redraw();
}


void
SkinEqView::_ShowPresets()
{
	BPopUpMenu* menu = new BPopUpMenu("presets", false, false);
	for (const Preset& preset : kPresets) {
		BMessage* message = new BMessage(kMsgSetEqualizer);
		for (float gain : preset.bands)
			message->AddFloat("bands", gain);
		message->AddFloat("preamp", 0);
		menu->AddItem(new BMenuItem(preset.name, message));
	}
	menu->SetTargetForItems(be_app);
	menu->SetAsyncAutoDestruct(true);
	skin::Rect button = skin::EqControlRect(skin::kEqPresets);
	menu->Go(ConvertToScreen(BPoint(button.x * fScale,
		(button.y + button.height) * fScale)), true, true, true);
}


SkinEqWindow::SkinEqWindow(BPoint where)
	:
	BWindow(BRect(where, where + BPoint(skin::kMainWidth - 1,
		skin::kMainHeight - 1)), "Equalizer", B_NO_BORDER_WINDOW_LOOK,
		B_NORMAL_WINDOW_FEEL, B_NOT_ZOOMABLE | B_NOT_RESIZABLE
			| B_ASYNCHRONOUS_CONTROLS)
{
	fView = new SkinEqView();
	AddChild(fView);
	AddShortcut('D', B_COMMAND_KEY, new BMessage(kMsgToggleDoubleSize),
		be_app);
}


bool
SkinEqWindow::QuitRequested()
{
	Hide();
	return false;
}


void
SkinEqWindow::WindowActivated(bool active)
{
	fView->SetActive(active);
}


void
SkinEqWindow::SetSkin(std::shared_ptr<skin::WinampSkin> skin)
{
	fView->SetSkin(skin);
}


void
SkinEqWindow::SetEqualizer(const skin::EqState& state)
{
	fView->SetEqualizer(state);
}


void
SkinEqWindow::SetDoubleSize(bool doubleSize)
{
	int scale = doubleSize ? 2 : 1;
	ResizeTo(skin::kMainWidth * scale - 1, skin::kMainHeight * scale - 1);
	fView->SetScale(scale);
}


//	#pragma mark - Playlist


static const int kRowHeight = 13;


class SkinPlaylistView : public BView {
public:
								SkinPlaylistView(BRect frame);
	virtual						~SkinPlaylistView();

	virtual	void				AttachedToWindow();
	virtual	void				Draw(BRect updateRect);
	virtual	void				FrameResized(float width, float height);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual	void				MouseUp(BPoint where);
	virtual	void				KeyDown(const char* bytes, int32 count);
	virtual	void				MessageReceived(BMessage* message);

			void				SetSkin(std::shared_ptr<skin::WinampSkin> skin);
			void				SetActive(bool active);
			void				SetTimes(double position, double duration);
			void				Update(BMessage* update);

			void				SelectAll(bool select);
			void				InvertSelection();
			// The selected entries' indexes, or the others'
			void				AddSelected(BMessage* message, bool selected);

private:
			int					_Width() const
									{ return int(Bounds().Width()) + 1; }
			int					_Height() const
									{ return int(Bounds().Height()) + 1; }
			int					_VisibleRows() const;
			int					_RowAt(BPoint where) const;
			void				_ScrollTo(int top);
			void				_Redraw();
			void				_ShowMenu(skin::PlaylistControl button);
			void				_Command(skin::PlaylistControl control);
			void				_Play(int index);

			std::shared_ptr<skin::WinampSkin> fSkin;
			BBitmap*			fBitmap;
			skin::PlaylistState	fState;
			std::vector<std::string> fNames;
			std::vector<bool>	fSelected;
			int					fCurrent;
			int					fAnchor;	// where a shift-click range starts
			int					fTop;		// first row shown
			skin::PlaylistControl fTracking;
			BPoint				fDragOffset;
			BPoint				fResizeStart;	// on screen
			BPoint				fResizeSize;
};


SkinPlaylistView::SkinPlaylistView(BRect frame)
	:
	BView(frame, "playlist", B_FOLLOW_ALL, B_WILL_DRAW | B_FRAME_EVENTS
		| B_NAVIGABLE),
	fBitmap(NULL),
	fCurrent(-1),
	fAnchor(-1),
	fTop(0),
	fTracking(skin::kPlNone)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	BFont font(be_plain_font);
	font.SetSize(10);
	SetFont(&font);
}


SkinPlaylistView::~SkinPlaylistView()
{
	delete fBitmap;
}


void
SkinPlaylistView::AttachedToWindow()
{
	FrameResized(Bounds().Width(), Bounds().Height());
}


void
SkinPlaylistView::SetSkin(std::shared_ptr<skin::WinampSkin> skin)
{
	fSkin = skin;
	_Redraw();
}


void
SkinPlaylistView::SetActive(bool active)
{
	fState.active = active;
	_Redraw();
}


void
SkinPlaylistView::SetTimes(double position, double duration)
{
	char now[32], length[32];
	audio::FormatTime(position, now, sizeof(now));
	audio::FormatTime(duration, length, sizeof(length));
	std::string running = fCurrent >= 0 ? std::string(now) + "/" + length : "";
	std::string mini = fCurrent >= 0 ? now : "";
	if (running == fState.runningTime && mini == fState.miniTime)
		return;
	fState.runningTime = running;
	fState.miniTime = mini;
	_Redraw();
}


void
SkinPlaylistView::Update(BMessage* update)
{
	size_t oldCount = fNames.size();
	fNames.clear();
	const char* path;
	for (int32 i = 0; update->FindString("path", i, &path) == B_OK; i++) {
		const char* slash = strrchr(path, '/');
		std::string name = slash != NULL ? slash + 1 : path;
		size_t dot = name.find_last_of('.');
		if (dot != std::string::npos && dot > 0)
			name.erase(dot);
		fNames.push_back(name);
	}
	fCurrent = update->GetInt32("current", -1);
	// Keep the selection by index while the list is the same length
	if (fNames.size() != oldCount)
		fSelected.assign(fNames.size(), false);
	_ScrollTo(fTop);
	_Redraw();
}


int
SkinPlaylistView::_VisibleRows() const
{
	skin::Rect items = skin::PlaylistControlRect(skin::kPlItems, _Width(),
		_Height());
	return std::max(1, items.height / kRowHeight);
}


int
SkinPlaylistView::_RowAt(BPoint where) const
{
	skin::Rect items = skin::PlaylistControlRect(skin::kPlItems, _Width(),
		_Height());
	int row = fTop + int(where.y - items.y) / kRowHeight;
	return row >= 0 && row < int(fNames.size()) ? row : -1;
}


void
SkinPlaylistView::_ScrollTo(int top)
{
	int maxTop = std::max(0, int(fNames.size()) - _VisibleRows());
	fTop = std::max(0, std::min(top, maxTop));
	fState.scroll = maxTop > 0 ? double(fTop) / maxTop : 0;
	_Redraw();
}


void
SkinPlaylistView::FrameResized(float width, float height)
{
	delete fBitmap;
	fBitmap = new BBitmap(BRect(0, 0, width, height), B_RGB32);
	_ScrollTo(fTop);
}


void
SkinPlaylistView::_Redraw()
{
	if (!fSkin || fBitmap == NULL || fBitmap->InitCheck() != B_OK)
		return;
	fSkin->RenderPlaylist(fState, _Width(), _Height(),
		(uint32_t*)fBitmap->Bits(), fBitmap->BytesPerRow());
	Invalidate();
}


void
SkinPlaylistView::Draw(BRect updateRect)
{
	if (fBitmap == NULL || !fSkin)
		return;
	DrawBitmap(fBitmap, B_ORIGIN);

	// The entries, in the skin's colours and the system font
	const skin::PlaylistColors& colors = fSkin->Colors();
	skin::Rect items = skin::PlaylistControlRect(skin::kPlItems, _Width(),
		_Height());
	BRect area(items.x, items.y, items.x + items.width - 1,
		items.y + items.height - 1);
	BRegion clip(area);
	ConstrainClippingRegion(&clip);
	font_height metrics;
	GetFontHeight(&metrics);
	float baseline = floorf((kRowHeight + metrics.ascent - metrics.descent)
		/ 2);
	int rows = _VisibleRows() + 1;
	for (int i = 0; i < rows; i++) {
		int index = fTop + i;
		if (index >= int(fNames.size()))
			break;
		float top = items.y + i * kRowHeight;
		if (fSelected[index]) {
			SetHighColor(Color(colors.selectedBackground));
			FillRect(BRect(area.left, top, area.right, top + kRowHeight - 1));
		}
		BString text;
		text.SetToFormat("%d. %s", index + 1, fNames[index].c_str());
		TruncateString(&text, B_TRUNCATE_END, area.Width() - 4);
		SetHighColor(Color(index == fCurrent ? colors.current : colors.normal));
		SetLowColor(Color(fSelected[index] ? colors.selectedBackground
			: colors.normalBackground));
		DrawString(text.String(), BPoint(area.left + 2, top + baseline));
	}
	ConstrainClippingRegion(NULL);
}


void
SkinPlaylistView::MouseDown(BPoint where)
{
	MakeFocus();
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	skin::PlaylistControl control = skin::PlaylistHitTest((int)where.x,
		(int)where.y, _Width(), _Height());
	fTracking = control;
	switch (control) {
		case skin::kPlNone:
		case skin::kPlTitleBar:
			fTracking = skin::kPlTitleBar;
			fDragOffset = where;
			return;
		case skin::kPlResize:
			fResizeStart = ConvertToScreen(where);
			fResizeSize = BPoint(_Width(), _Height());
			return;
		case skin::kPlItems:
		{
			fTracking = skin::kPlNone;
			int row = _RowAt(where);
			if (row < 0) {
				SelectAll(false);
				return;
			}
			int32 modifiers = 0, clicks = 1;
			if (Window()->CurrentMessage() != NULL) {
				Window()->CurrentMessage()->FindInt32("modifiers", &modifiers);
				Window()->CurrentMessage()->FindInt32("clicks", &clicks);
			}
			if (clicks >= 2) {
				_Play(row);
				return;
			}
			if ((modifiers & B_SHIFT_KEY) != 0 && fAnchor >= 0) {
				fSelected.assign(fNames.size(), false);
				for (int i = std::min(fAnchor, row);
						i <= std::max(fAnchor, row); i++)
					fSelected[i] = true;
			} else if ((modifiers & B_COMMAND_KEY) != 0) {
				fSelected[row] = !fSelected[row];
				fAnchor = row;
			} else {
				fSelected.assign(fNames.size(), false);
				fSelected[row] = true;
				fAnchor = row;
			}
			Invalidate();
			return;
		}
		case skin::kPlScrollBar:
			fState.pressed = control;
			MouseMoved(where, B_INSIDE_VIEW, NULL);
			return;
		default:
			fState.pressed = control;
			_Redraw();
			return;
	}
}


void
SkinPlaylistView::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	if (fTracking == skin::kPlNone)
		return;
	if (CurrentButtons(this) == 0 && (fTracking == skin::kPlTitleBar
			|| fTracking == skin::kPlResize)) {
		fTracking = skin::kPlNone;
		return;
	}
	switch (fTracking) {
		case skin::kPlTitleBar:
			DragWindow(this, where, fDragOffset);
			return;
		case skin::kPlResize:
		{
			// Grows in Winamp's steps, 25 x 29
			BPoint delta = ConvertToScreen(where) - fResizeStart;
			int width = int(fResizeSize.x + delta.x);
			int height = int(fResizeSize.y + delta.y);
			width = skin::kPlaylistMinWidth + std::max(0, int(lroundf(
				float(width - skin::kPlaylistMinWidth)
					/ skin::kPlaylistWidthStep))) * skin::kPlaylistWidthStep;
			height = skin::kPlaylistMinHeight + std::max(0, int(lroundf(
				float(height - skin::kPlaylistMinHeight)
					/ skin::kPlaylistHeightStep))) * skin::kPlaylistHeightStep;
			if (width != _Width() || height != _Height())
				Window()->ResizeTo(width - 1, height - 1);
			return;
		}
		case skin::kPlScrollBar:
		{
			skin::Rect bar = skin::PlaylistControlRect(skin::kPlScrollBar,
				_Width(), _Height());
			double t = (where.y - bar.y - 9) / std::max(1, bar.height - 18);
			int maxTop = std::max(0, int(fNames.size()) - _VisibleRows());
			_ScrollTo(int(lround(std::max(0.0, std::min(1.0, t)) * maxTop)));
			return;
		}
		default:
		{
			bool inside = skin::PlaylistControlRect(fTracking, _Width(),
				_Height()).Contains((int)where.x, (int)where.y);
			skin::PlaylistControl pressed = inside ? fTracking : skin::kPlNone;
			if (pressed != fState.pressed) {
				fState.pressed = pressed;
				_Redraw();
			}
		}
	}
}


void
SkinPlaylistView::MouseUp(BPoint where)
{
	skin::PlaylistControl control = fTracking;
	fTracking = skin::kPlNone;
	fState.pressed = skin::kPlNone;
	_Redraw();
	if (control == skin::kPlNone || control == skin::kPlTitleBar
		|| control == skin::kPlResize || control == skin::kPlScrollBar)
		return;
	if (skin::PlaylistControlRect(control, _Width(), _Height()).Contains(
			(int)where.x, (int)where.y))
		_Command(control);
}


void
SkinPlaylistView::_Command(skin::PlaylistControl control)
{
	switch (control) {
		case skin::kPlClose:	Window()->Hide(); break;
		case skin::kPlPrevious:	be_app->PostMessage(kMsgPrevious); break;
		case skin::kPlPlay:		be_app->PostMessage(kMsgPlay); break;
		case skin::kPlPause:	be_app->PostMessage(kMsgPause); break;
		case skin::kPlStop:		be_app->PostMessage(kMsgStop); break;
		case skin::kPlNext:		be_app->PostMessage(kMsgNext); break;
		case skin::kPlEject:	be_app->PostMessage(kMsgOpen); break;
		case skin::kPlAdd:
		case skin::kPlRemove:
		case skin::kPlSelect:
		case skin::kPlMisc:
		case skin::kPlList:
			_ShowMenu(control);
			break;
		default:
			break;
	}
}


static BMenuItem*
Item(const char* label, uint32 what, BHandler* target)
{
	BMenuItem* item = new BMenuItem(label, new BMessage(what));
	item->SetTarget(target);
	return item;
}


void
SkinPlaylistView::_ShowMenu(skin::PlaylistControl button)
{
	// What Winamp's buttons pop up, as menus
	BPopUpMenu* menu = new BPopUpMenu("playlist", false, false);
	BHandler* window = Window();
	switch (button) {
		case skin::kPlAdd:
			menu->AddItem(Item("Add files" B_UTF8_ELLIPSIS, kMsgAddFiles,
				be_app));
			break;
		case skin::kPlRemove:
			menu->AddItem(Item("Remove selected", kMsgRemoveSelected, window));
			menu->AddItem(Item("Crop", kMsgCropSelected, window));
			menu->AddItem(Item("Remove all", kMsgPlaylistClear, be_app));
			break;
		case skin::kPlSelect:
			menu->AddItem(Item("Select all", B_SELECT_ALL, window));
			menu->AddItem(Item("Select none", kMsgSelectNone, window));
			menu->AddItem(Item("Invert selection", kMsgInvertSelection,
				window));
			break;
		case skin::kPlMisc:
			menu->AddItem(Item("Sort by name", kMsgPlaylistSortName, be_app));
			menu->AddItem(Item("Sort by path", kMsgPlaylistSortPath, be_app));
			menu->AddItem(Item("Shuffle", kMsgPlaylistShuffle, be_app));
			break;
		case skin::kPlList:
			menu->AddItem(Item("New list", kMsgPlaylistClear, be_app));
			break;
		default:
			break;
	}
	menu->SetAsyncAutoDestruct(true);
	skin::Rect rect = skin::PlaylistControlRect(button, _Width(), _Height());
	menu->Go(ConvertToScreen(BPoint(rect.x, rect.y)), true, true, true);
}


void
SkinPlaylistView::_Play(int index)
{
	BMessage play(kMsgPlaylistPlay);
	play.AddInt32("index", index);
	be_app->PostMessage(&play);
}


void
SkinPlaylistView::SelectAll(bool select)
{
	fSelected.assign(fNames.size(), select);
	Invalidate();
}


void
SkinPlaylistView::InvertSelection()
{
	for (size_t i = 0; i < fSelected.size(); i++)
		fSelected[i] = !fSelected[i];
	Invalidate();
}


void
SkinPlaylistView::AddSelected(BMessage* message, bool selected)
{
	for (size_t i = 0; i < fSelected.size(); i++) {
		if (fSelected[i] == selected)
			message->AddInt32("index", (int32)i);
	}
}


void
SkinPlaylistView::KeyDown(const char* bytes, int32 count)
{
	int first = -1;
	for (size_t i = 0; i < fSelected.size(); i++) {
		if (fSelected[i]) {
			first = (int)i;
			break;
		}
	}
	switch (bytes[0]) {
		case B_DELETE:
			Window()->PostMessage(kMsgRemoveSelected);
			break;
		case B_ENTER:
			if (first >= 0)
				_Play(first);
			break;
		case B_UP_ARROW:
		case B_DOWN_ARROW:
		{
			if (fNames.empty())
				break;
			int row = first < 0 ? 0 : first + (bytes[0] == B_UP_ARROW ? -1 : 1);
			row = std::max(0, std::min(row, int(fNames.size()) - 1));
			fSelected.assign(fNames.size(), false);
			fSelected[row] = true;
			fAnchor = row;
			if (row < fTop)
				_ScrollTo(row);
			else if (row >= fTop + _VisibleRows())
				_ScrollTo(row - _VisibleRows() + 1);
			Invalidate();
			break;
		}
		default:
			BView::KeyDown(bytes, count);
	}
}


void
SkinPlaylistView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_MOUSE_WHEEL_CHANGED:
		{
			float delta = message->GetFloat("be:wheel_delta_y", 0);
			_ScrollTo(fTop + int(delta * 3));
			break;
		}
		case B_SIMPLE_DATA:
			if (message->HasRef("refs")) {
				message->what = kMsgPlaylistAdd;
				be_app->PostMessage(message);
			}
			break;
		default:
			BView::MessageReceived(message);
	}
}


SkinPlaylistWindow::SkinPlaylistWindow(BRect frame)
	:
	BWindow(frame, "Playlist", B_NO_BORDER_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS)
{
	SetSizeLimits(skin::kPlaylistMinWidth - 1, 32767,
		skin::kPlaylistMinHeight - 1, 32767);
	fView = new SkinPlaylistView(Bounds());
	AddChild(fView);
	fView->MakeFocus();
}


bool
SkinPlaylistWindow::QuitRequested()
{
	Hide();
	return false;
}


void
SkinPlaylistWindow::WindowActivated(bool active)
{
	fView->SetActive(active);
}


void
SkinPlaylistWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgPlaylistUpdate:
			fView->Update(message);
			break;
		case B_SELECT_ALL:
			fView->SelectAll(true);
			break;
		case kMsgSelectNone:
			fView->SelectAll(false);
			break;
		case kMsgInvertSelection:
			fView->InvertSelection();
			break;
		case kMsgRemoveSelected:
		case kMsgCropSelected:
		{
			BMessage remove(kMsgPlaylistRemove);
			fView->AddSelected(&remove,
				message->what == kMsgRemoveSelected);
			if (remove.HasInt32("index"))
				be_app->PostMessage(&remove);
			break;
		}
		default:
			BWindow::MessageReceived(message);
	}
}


void
SkinPlaylistWindow::SetSkin(std::shared_ptr<skin::WinampSkin> skin)
{
	fView->SetSkin(skin);
}


void
SkinPlaylistWindow::SetTimes(double position, double duration)
{
	fView->SetTimes(position, duration);
}

/*
 * PowerPlay — the controller's small controls: symbol buttons, the speed
 * slider, the position bar and the volume knob.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#include "Controls.h"

#include <ControlLook.h>
#include <GradientLinear.h>
#include <Window.h>

#include <math.h>


float
ControlUnit()
{
	// 22 px, the original's buttons, growing with a larger system font
	// but never smaller than the original.
	return fmaxf(22.0f, roundf(22.0f * be_plain_font->Size() / 12.0f));
}


//	#pragma mark - SymbolButton


SymbolButton::SymbolButton(const char* name, Symbol symbol,
	BMessage* message)
	:
	BButton(name, NULL, message),
	fSymbol(symbol)
{
}


void
SymbolButton::SetSymbol(Symbol symbol)
{
	if (symbol == fSymbol)
		return;
	fSymbol = symbol;
	Invalidate();
}


// SoundPlay's button look, measured from the original: a dark grey outline
// with its corners cut, a white highlight top and left inside a light grey
// edge, two greys of shading bottom and right, and crisp black symbols on a
// 23 px grid.
static const rgb_color kOutline = {0x60, 0x60, 0x60, 255};
static const rgb_color kFace = {0xe8, 0xe8, 0xe8, 255};
static const rgb_color kHighlight = {0xf8, 0xfc, 0xf8, 255};
static const rgb_color kShade = {0xd8, 0xd8, 0xd8, 255};
static const rgb_color kShadow = {0x98, 0x98, 0x98, 255};
static const rgb_color kSymbol = {0, 0, 0, 255};
static const rgb_color kDarkEdge = {0x58, 0x58, 0x58, 255};
static const rgb_color kGroove = {0x70, 0x70, 0x70, 255};
static const rgb_color kKnobRim = {0x60, 0x64, 0x60, 255};
static const rgb_color kKnobFace = {0xf0, 0xf0, 0xf0, 255};
static const rgb_color kKnobLow = {0xb8, 0xbc, 0xb8, 255};
static const rgb_color kKnobDot = {0xf8, 0x00, 0x00, 255};
static const int32 kGrid = 23;


// A run of symbol cells on the 23 px grid: row, first and last column.
static void
Cells(BView* view, BPoint origin, int32 k, int32 row, int32 first,
	int32 last)
{
	view->FillRect(BRect(origin.x + first * k, origin.y + row * k,
		origin.x + (last + 1) * k - 1, origin.y + (row + 1) * k - 1));
}


// The width of row r (0..10) of the big triangle: 1, 3, 5, 6, 8, 10 to the
// middle and back, a slope of 0.9.
static int32
BigWidth(int32 r)
{
	int32 d = 5 - abs(r - 5);
	return (int32)lroundf(1 + 1.8f * d);
}


void
SymbolButton::Draw(BRect updateRect)
{
	BRect r = Bounds();
	bool pressed = Value() == B_CONTROL_ON;
	bool enabled = IsEnabled();
	// Whole pixels: the bevel and symbol grow in steps with the font.
	int32 k = (int32)fmaxf(1, floorf((r.Width() + 1) / kGrid));

	rgb_color panel = Parent() != NULL ? Parent()->ViewColor() : ViewColor();
	SetDrawingMode(B_OP_COPY);
	SetHighColor(pressed ? kShade : kFace);
	FillRect(r);

	// The outline, its corners cut to the panel
	SetHighColor(kOutline);
	FillRect(BRect(r.left + k, r.top, r.right - k, r.top + k - 1));
	FillRect(BRect(r.left + k, r.bottom - k + 1, r.right - k, r.bottom));
	FillRect(BRect(r.left, r.top + k, r.left + k - 1, r.bottom - k));
	FillRect(BRect(r.right - k + 1, r.top + k, r.right, r.bottom - k));
	SetHighColor(panel);
	FillRect(BRect(r.left, r.top, r.left + k - 1, r.top + k - 1));
	FillRect(BRect(r.right - k + 1, r.top, r.right, r.top + k - 1));
	FillRect(BRect(r.left, r.bottom - k + 1, r.left + k - 1, r.bottom));
	FillRect(BRect(r.right - k + 1, r.bottom - k + 1, r.right, r.bottom));

	// Inside the outline: a light edge, then two pixels of highlight top
	// and left, and two of shading bottom and right; pressed, the other
	// way round.
	BRect in = r.InsetByCopy(k, k);
	rgb_color light1 = pressed ? kShadow : kFace;
	rgb_color light2 = pressed ? kShade : kHighlight;
	rgb_color dark1 = pressed ? kHighlight : kShadow;
	rgb_color dark2 = pressed ? kFace : kShade;
	SetHighColor(light1);
	FillRect(BRect(in.left, in.top, in.right, in.top + k - 1));
	FillRect(BRect(in.left, in.top, in.left + k - 1, in.bottom));
	SetHighColor(dark1);
	FillRect(BRect(in.left + k, in.bottom - k + 1, in.right, in.bottom));
	FillRect(BRect(in.right - k + 1, in.top + k, in.right, in.bottom));
	SetHighColor(light2);
	FillRect(BRect(in.left + k, in.top + k, in.right - k, in.top + 3 * k - 1));
	FillRect(BRect(in.left + k, in.top + k, in.left + 3 * k - 1,
		in.bottom - k));
	SetHighColor(dark2);
	FillRect(BRect(in.left + 3 * k, in.bottom - 2 * k + 1, in.right - k,
		in.bottom - k));
	FillRect(BRect(in.right - 2 * k + 1, in.top + 2 * k, in.right - k,
		in.bottom - k));

	// The symbol, on the 23 px grid centred in the button
	BPoint o(floorf(r.left + (r.Width() + 1 - kGrid * k) / 2),
		floorf(r.top + (r.Height() + 1 - kGrid * k) / 2));
	if (pressed)
		o += BPoint(k, k);
	SetHighColor(enabled ? kSymbol : kShadow);
	switch (fSymbol) {
		case kPlay:
			for (int32 row = 0; row <= 10; row++)
				Cells(this, o, k, 6 + row, 7, 7 + BigWidth(row) - 1);
			break;
		case kReverse:
			for (int32 row = 0; row <= 10; row++)
				Cells(this, o, k, 6 + row, 15 - BigWidth(row) + 1, 15);
			break;
		case kStop:
			for (int32 row = 7; row <= 15; row++)
				Cells(this, o, k, row, 7, 15);
			break;
		case kPause:
			for (int32 row = 6; row <= 16; row++) {
				Cells(this, o, k, row, 7, 9);
				Cells(this, o, k, row, 13, 15);
			}
			break;
		case kPrevious:
			for (int32 row = 0; row <= 10; row++) {
				int32 w = 1 + 5 - abs(row - 5);
				Cells(this, o, k, 6 + row, 4, 5);
				Cells(this, o, k, 6 + row, 12 - w + 1, 12);
				Cells(this, o, k, 6 + row, 18 - w + 1, 18);
			}
			break;
		case kNext:
			for (int32 row = 0; row <= 10; row++) {
				int32 w = 1 + 5 - abs(row - 5);
				Cells(this, o, k, 6 + row, 4, 4 + w - 1);
				Cells(this, o, k, 6 + row, 10, 10 + w - 1);
				Cells(this, o, k, 6 + row, 17, 18);
			}
			break;
		case kEject:
			for (int32 row = 0; row <= 5; row++)
				Cells(this, o, k, 6 + row, 11 - row, 11 + row);
			for (int32 row = 13; row <= 15; row++)
				Cells(this, o, k, row, 6, 16);
			break;
	}

	if (IsFocus() && Window()->IsActive()) {
		SetHighColor(ui_color(B_KEYBOARD_NAVIGATION_COLOR));
		StrokeRect(in.InsetByCopy(3 * k, 3 * k));
	}
}


BSize
SymbolButton::MinSize()
{
	// 23 x 23 at the default size, as in the original
	float unit = ControlUnit() + 1;
	return BSize(unit - 1, unit - 1);
}


BSize
SymbolButton::MaxSize()
{
	return MinSize();
}


BSize
SymbolButton::PreferredSize()
{
	return MinSize();
}


//	#pragma mark - SpeedSlider


static const int32 kMinSpeed = -400;
static const int32 kMaxSpeed = 400;


SpeedSlider::SpeedSlider(const char* name, BMessage* message)
	:
	BControl(name, NULL, message, B_WILL_DRAW | B_NAVIGABLE),
	fTracking(false)
{
	SetValueNoUpdate(100);
}


BRect
SpeedSlider::_Track() const
{
	float thumb = roundf(ControlUnit() * 0.6f);
	BRect bounds = Bounds();
	return BRect(bounds.left + thumb / 2 + 2, bounds.top,
		bounds.right - thumb / 2 - 2, bounds.bottom);
}


float
SpeedSlider::_XFor(int32 value) const
{
	BRect track = _Track();
	return roundf(track.left + (value - kMinSpeed) * track.Width()
		/ (kMaxSpeed - kMinSpeed));
}


int32
SpeedSlider::_ValueFor(float x) const
{
	BRect track = _Track();
	int32 value = (int32)roundf(kMinSpeed + (x - track.left)
		* (kMaxSpeed - kMinSpeed) / track.Width());
	// Detents at normal speed, forwards and backwards
	if (abs(value - 100) <= 12)
		value = 100;
	else if (abs(value + 100) <= 12)
		value = -100;
	return value < kMinSpeed ? kMinSpeed : value > kMaxSpeed ? kMaxSpeed
		: value;
}


void
SpeedSlider::_SetFromUser(int32 value)
{
	value = value < kMinSpeed ? kMinSpeed : value > kMaxSpeed ? kMaxSpeed
		: value;
	if (value == Value())
		return;
	SetValue(value);
	Invoke();
}


void
SpeedSlider::Draw(BRect updateRect)
{
	BRect bounds = Bounds();
	rgb_color panel = ViewColor();
	SetHighColor(panel);
	FillRect(bounds);

	bool enabled = IsEnabled();
	rgb_color ink = enabled ? kSymbol : kShadow;
	float mid = floorf((bounds.top + bounds.bottom) / 2);
	BRect track = _Track();

	// The line, and a tick every 100%
	SetHighColor(ink);
	StrokeLine(BPoint(track.left, mid), BPoint(track.right, mid));
	for (int32 v = kMinSpeed; v <= kMaxSpeed; v += 100) {
		float x = _XFor(v);
		float tick = v == 0 || v == 100 ? 4 : 3;
		StrokeLine(BPoint(x, mid - tick), BPoint(x, mid + tick));
	}

	// The thumb: a small bevelled block in SoundPlay's greys, white top
	// and left, dark below, with two grooves to grip.
	float w = roundf(ControlUnit() * 0.6f);
	float x = _XFor(Value());
	BRect thumb(x - floorf(w / 2), bounds.top + 1, x - floorf(w / 2) + w - 1,
		bounds.bottom - 1);
	SetHighColor(kFace);
	FillRect(thumb);
	SetHighColor(kHighlight);
	StrokeLine(thumb.LeftTop(), thumb.RightTop());
	StrokeLine(thumb.LeftTop(), thumb.LeftBottom());
	SetHighColor(kDarkEdge);
	StrokeLine(thumb.LeftBottom(), thumb.RightBottom());
	StrokeLine(thumb.RightTop(), thumb.RightBottom());
	SetHighColor(kShadow);
	StrokeLine(thumb.LeftBottom() + BPoint(1, -1),
		thumb.RightBottom() + BPoint(-1, -1));
	for (int32 i = 1; i <= 2; i++) {
		float gx = roundf(thumb.left + thumb.Width() * i / 3);
		SetHighColor(enabled ? kGroove : kShadow);
		StrokeLine(BPoint(gx, thumb.top + 2), BPoint(gx, thumb.bottom - 2));
		SetHighColor(kHighlight);
		StrokeLine(BPoint(gx + 1, thumb.top + 2),
			BPoint(gx + 1, thumb.bottom - 2));
	}
	if (IsFocus() && Window()->IsActive()) {
		SetHighColor(ui_color(B_KEYBOARD_NAVIGATION_COLOR));
		StrokeLine(BPoint(thumb.left, bounds.bottom),
			BPoint(thumb.right, bounds.bottom));
	}
}


void
SpeedSlider::MouseDown(BPoint where)
{
	if (!IsEnabled())
		return;
	int32 clicks = 1;
	if (Window()->CurrentMessage() != NULL)
		Window()->CurrentMessage()->FindInt32("clicks", &clicks);
	if (clicks == 2) {
		_SetFromUser(100);
		return;
	}
	fTracking = true;
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	_SetFromUser(_ValueFor(where.x));
}


void
SpeedSlider::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	if (fTracking)
		_SetFromUser(_ValueFor(where.x));
}


void
SpeedSlider::MouseUp(BPoint where)
{
	fTracking = false;
}


void
SpeedSlider::KeyDown(const char* bytes, int32 count)
{
	switch (bytes[0]) {
		case B_LEFT_ARROW:
			_SetFromUser(Value() - 10);
			break;
		case B_RIGHT_ARROW:
			_SetFromUser(Value() + 10);
			break;
		case B_HOME:
			_SetFromUser(100);
			break;
		default:
			BControl::KeyDown(bytes, count);
	}
}


BSize
SpeedSlider::MinSize()
{
	float unit = ControlUnit();
	return BSize(roundf(unit * 4.5f), roundf(unit * 0.6f));
}


BSize
SpeedSlider::MaxSize()
{
	return BSize(B_SIZE_UNLIMITED, MinSize().height);
}


BSize
SpeedSlider::PreferredSize()
{
	return MinSize();
}


//	#pragma mark - PositionBar


PositionBar::PositionBar(const char* name, BMessage* message)
	:
	BControl(name, NULL, message, B_WILL_DRAW),
	fTracking(false)
{
}


void
PositionBar::Draw(BRect updateRect)
{
	BRect rect = Bounds();
	rgb_color panel = ViewColor();
	be_control_look->DrawTextControlBorder(this, rect, updateRect, panel,
		be_control_look->Flags(this));
	SetHighColor(tint_color(panel, B_LIGHTEN_1_TINT));
	FillRect(rect);
	if (Value() > 0) {
		BRect done = rect;
		done.right = rect.left + roundf(rect.Width() * Value() / 1000.0f);
		SetHighColor(IsEnabled() ? ui_color(B_CONTROL_HIGHLIGHT_COLOR)
			: tint_color(panel, B_DARKEN_2_TINT));
		FillRect(done);
	}
}


void
PositionBar::_SeekTo(float x)
{
	BRect bounds = Bounds();
	int32 value = (int32)roundf((x - bounds.left) * 1000.0f
		/ bounds.Width());
	value = value < 0 ? 0 : value > 1000 ? 1000 : value;
	SetValue(value);
	Invoke();
}


void
PositionBar::MouseDown(BPoint where)
{
	if (!IsEnabled())
		return;
	fTracking = true;
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	_SeekTo(where.x);
}


void
PositionBar::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	if (fTracking)
		_SeekTo(where.x);
}


void
PositionBar::MouseUp(BPoint where)
{
	fTracking = false;
}


BSize
PositionBar::MinSize()
{
	float unit = ControlUnit();
	return BSize(roundf(unit * 4.5f), roundf(unit * 0.45f));
}


BSize
PositionBar::MaxSize()
{
	return BSize(B_SIZE_UNLIMITED, MinSize().height);
}


BSize
PositionBar::PreferredSize()
{
	return MinSize();
}


//	#pragma mark - VolumeKnob


VolumeKnob::VolumeKnob(const char* name, BMessage* message)
	:
	BControl(name, NULL, message, B_WILL_DRAW | B_NAVIGABLE),
	fTracking(false),
	fStartY(0),
	fStartValue(0)
{
	SetValueNoUpdate(80);
}


void
VolumeKnob::_SetFromUser(int32 value)
{
	value = value < 0 ? 0 : value > 100 ? 100 : value;
	if (value == Value())
		return;
	SetValue(value);
	Invoke();
}


void
VolumeKnob::Draw(BRect updateRect)
{
	BRect bounds = Bounds();
	SetHighColor(ViewColor());
	FillRect(bounds);

	float size = fminf(bounds.Width(), bounds.Height()) - 1;
	BPoint c((bounds.left + bounds.right) / 2,
		(bounds.top + bounds.bottom) / 2);
	float r = size / 2;
	SetFlags(Flags() | B_SUBPIXEL_PRECISE);

	// SoundPlay's flat knob: a dark rim, a bevel ring white at the top
	// left and grey at the bottom right, a light face and a red dot.
	SetHighColor(kKnobRim);
	FillEllipse(c, r, r);
	BGradientLinear bevel(BPoint(c.x - r, c.y - r), BPoint(c.x + r, c.y + r));
	bevel.AddColor(kHighlight, 0);
	bevel.AddColor(kKnobFace, 110);
	bevel.AddColor(kKnobLow, 255);
	FillEllipse(c, r - 1, r - 1, bevel);
	SetHighColor(kKnobFace);
	FillEllipse(c, r - 3, r - 3);

	// The dot: from -135 degrees (silent) to 135 (full)
	float angle = (-135.0f + 270.0f * Value() / 100.0f) * M_PI / 180.0f;
	BPoint dot(c.x + sinf(angle) * r * 0.6f, c.y - cosf(angle) * r * 0.6f);
	float dotSize = fmaxf(2, roundf(r * 0.2f));
	SetHighColor(IsEnabled() ? kKnobDot : kShadow);
	FillRect(BRect(floorf(dot.x - dotSize / 2), floorf(dot.y - dotSize / 2),
		floorf(dot.x - dotSize / 2) + dotSize - 1,
		floorf(dot.y - dotSize / 2) + dotSize - 1));

	if (IsFocus() && Window()->IsActive()) {
		SetHighColor(ui_color(B_KEYBOARD_NAVIGATION_COLOR));
		StrokeEllipse(c, r + 1, r + 1);
	}
}


void
VolumeKnob::MouseDown(BPoint where)
{
	if (!IsEnabled())
		return;
	fTracking = true;
	fStartY = where.y;
	fStartValue = Value();
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
}


void
VolumeKnob::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	// Up turns it up: 100 steps over about 100 pixels.
	if (fTracking)
		_SetFromUser(fStartValue + (int32)roundf(fStartY - where.y));
}


void
VolumeKnob::MouseUp(BPoint where)
{
	fTracking = false;
}


void
VolumeKnob::MessageReceived(BMessage* message)
{
	if (message->what == B_MOUSE_WHEEL_CHANGED) {
		float delta = 0;
		if (message->FindFloat("be:wheel_delta_y", &delta) == B_OK) {
			_SetFromUser(Value() - (int32)roundf(delta * 5));
			return;
		}
	}
	BControl::MessageReceived(message);
}


void
VolumeKnob::KeyDown(const char* bytes, int32 count)
{
	switch (bytes[0]) {
		case B_UP_ARROW:
		case B_RIGHT_ARROW:
			_SetFromUser(Value() + 5);
			break;
		case B_DOWN_ARROW:
		case B_LEFT_ARROW:
			_SetFromUser(Value() - 5);
			break;
		default:
			BControl::KeyDown(bytes, count);
	}
}


BSize
VolumeKnob::MinSize()
{
	float unit = ControlUnit();
	return BSize(unit - 1, unit - 1);
}


BSize
VolumeKnob::MaxSize()
{
	return MinSize();
}


BSize
VolumeKnob::PreferredSize()
{
	return MinSize();
}

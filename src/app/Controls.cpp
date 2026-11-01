/*
 * PowerPlay — the controller's small controls: symbol buttons, the speed
 * slider, the position bar and the volume knob.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#include "Controls.h"

#include <ControlLook.h>
#include <GradientRadialFocus.h>
#include <Window.h>

#include <math.h>


float
ControlUnit()
{
	return roundf(22.0f * be_plain_font->Size() / 12.0f);
}


static rgb_color
Base()
{
	return ui_color(B_CONTROL_BACKGROUND_COLOR);
}


static rgb_color
Ink(bool enabled)
{
	rgb_color text = ui_color(B_CONTROL_TEXT_COLOR);
	if (!enabled)
		text = tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DISABLED_LABEL_TINT);
	return text;
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


void
SymbolButton::Draw(BRect updateRect)
{
	BRect rect = Bounds();
	rgb_color base = Base();
	uint32 flags = be_control_look->Flags(this);
	if (Value() == B_CONTROL_ON)
		flags |= BControlLook::B_ACTIVATED;
	be_control_look->DrawButtonFrame(this, rect, updateRect, base,
		ViewColor(), flags);
	be_control_look->DrawButtonBackground(this, rect, updateRect, base,
		flags);

	// The symbol, in a square about half the button's size
	float size = floorf(Bounds().Height() * 0.45f);
	if (size < 7)
		size = 7;
	BPoint c(floorf((Bounds().left + Bounds().right) / 2) + 0.5f,
		floorf((Bounds().top + Bounds().bottom) / 2) + 0.5f);
	if ((flags & BControlLook::B_ACTIVATED) != 0)
		c += BPoint(1, 1);
	float h = size / 2;
	SetHighColor(Ink(IsEnabled()));
	SetDrawingMode(B_OP_OVER);
	float bar = roundf(size / 5);
	if (bar < 2)
		bar = 2;

	switch (fSymbol) {
		case kPlay:
		{
			BPoint t[3] = {BPoint(c.x - h * 0.7f, c.y - h),
				BPoint(c.x + h * 0.8f, c.y), BPoint(c.x - h * 0.7f, c.y + h)};
			FillPolygon(t, 3);
			break;
		}
		case kReverse:
		{
			BPoint t[3] = {BPoint(c.x + h * 0.7f, c.y - h),
				BPoint(c.x - h * 0.8f, c.y), BPoint(c.x + h * 0.7f, c.y + h)};
			FillPolygon(t, 3);
			break;
		}
		case kStop:
			FillRect(BRect(c.x - h * 0.8f, c.y - h * 0.8f, c.x + h * 0.8f,
				c.y + h * 0.8f));
			break;
		case kPause:
			FillRect(BRect(c.x - h * 0.8f, c.y - h, c.x - h * 0.8f + bar,
				c.y + h));
			FillRect(BRect(c.x + h * 0.8f - bar, c.y - h, c.x + h * 0.8f,
				c.y + h));
			break;
		case kNext:
		case kPrevious:
		{
			// Two triangles and a bar, mirrored for previous
			float d = fSymbol == kNext ? 1 : -1;
			BPoint a[3] = {BPoint(c.x - d * h, c.y - h),
				BPoint(c.x, c.y), BPoint(c.x - d * h, c.y + h)};
			BPoint b[3] = {BPoint(c.x, c.y - h),
				BPoint(c.x + d * h, c.y), BPoint(c.x, c.y + h)};
			FillPolygon(a, 3);
			FillPolygon(b, 3);
			float x = c.x + d * h;
			FillRect(BRect(fminf(x, x + d * bar), c.y - h,
				fmaxf(x, x + d * bar), c.y + h));
			break;
		}
		case kEject:
		{
			BPoint t[3] = {BPoint(c.x - h, c.y + h * 0.2f),
				BPoint(c.x, c.y - h), BPoint(c.x + h, c.y + h * 0.2f)};
			FillPolygon(t, 3);
			FillRect(BRect(c.x - h, c.y + h * 0.5f, c.x + h,
				c.y + h * 0.5f + bar));
			break;
		}
	}
	SetDrawingMode(B_OP_COPY);
	if (IsFocus() && Window()->IsActive()) {
		SetHighColor(ui_color(B_KEYBOARD_NAVIGATION_COLOR));
		StrokeRect(Bounds().InsetByCopy(3, 3));
	}
}


BSize
SymbolButton::MinSize()
{
	float unit = ControlUnit();
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
	float thumb = roundf(ControlUnit() * 0.45f);
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
	rgb_color ink = Ink(enabled);
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

	// The thumb: a small raised block
	float w = roundf(ControlUnit() * 0.45f);
	float x = _XFor(Value());
	BRect thumb(x - floorf(w / 2), bounds.top + 1, x - floorf(w / 2) + w - 1,
		bounds.bottom - 1);
	uint32 flags = be_control_look->Flags(this);
	be_control_look->DrawButtonFrame(this, thumb, updateRect, Base(), panel,
		flags);
	be_control_look->DrawButtonBackground(this, thumb, updateRect, Base(),
		flags);
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

	// A domed knob, lit from the top left
	rgb_color base = Base();
	BGradientRadialFocus dome(BPoint(c.x - r * 0.35f, c.y - r * 0.4f),
		r * 1.5f, BPoint(c.x - r * 0.35f, c.y - r * 0.4f));
	dome.AddColor(tint_color(base, B_LIGHTEN_MAX_TINT), 0);
	dome.AddColor(base, 140);
	dome.AddColor(tint_color(base, B_DARKEN_1_TINT), 255);
	FillEllipse(c, r, r, dome);
	SetHighColor(tint_color(ViewColor(), B_DARKEN_3_TINT));
	StrokeEllipse(c, r, r);

	// The dot: from -135 degrees (silent) to 135 (full)
	float angle = (-135.0f + 270.0f * Value() / 100.0f) * M_PI / 180.0f;
	BPoint dot(c.x + sinf(angle) * r * 0.62f, c.y - cosf(angle) * r * 0.62f);
	float dotSize = fmaxf(1.2f, r * 0.14f);
	SetHighColor(IsEnabled() ? (rgb_color){220, 30, 30, 255}
		: tint_color(base, B_DARKEN_2_TINT));
	FillEllipse(dot, dotSize, dotSize);

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

/*
 * PowerPlay — the controller's small controls: symbol buttons, the speed
 * slider, the position bar and the volume knob.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#ifndef POWERPLAY_CONTROLS_H
#define POWERPLAY_CONTROLS_H

#include <Box.h>
#include <Bitmap.h>
#include <Button.h>
#include <Control.h>
#include <String.h>


// The size of the original's controls (22 px), larger with a system font
// over 12 pt, never smaller.
float ControlUnit();


// A box with SoundPlay's etched frame and its title centred on the top
// edge, in bold.
class TitleBox : public BBox {
public:
								TitleBox(const char* name);

			void				SetTitle(const char* title);
			// How far below the top the frame's contents should start.
			float				TitleHeight() const;

	virtual	void				Draw(BRect updateRect);

private:
			BString				fTitle;
};


class SymbolButton : public BButton {
public:
	enum Symbol {
		kPrevious, kReverse, kStop, kPlay, kPause, kNext, kEject
	};

								SymbolButton(const char* name, Symbol symbol,
									BMessage* message);

			void				SetSymbol(Symbol symbol);
			Symbol				GetSymbol() const { return fSymbol; }

	virtual	void				Draw(BRect updateRect);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

private:
			Symbol				fSymbol;
};


// Speed in percent, -400 to 400; snaps to 100 and -100, and double-click
// goes back to 100. Sends its message while dragging.
class SpeedSlider : public BControl {
public:
								SpeedSlider(const char* name,
									BMessage* message);

	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual	void				MouseUp(BPoint where);
	virtual	void				KeyDown(const char* bytes, int32 count);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

			bool				IsTracking() const { return fTracking; }

private:
			BRect				_Track() const;
			float				_XFor(int32 value) const;
			int32				_ValueFor(float x) const;
			void				_SetFromUser(int32 value);

			bool				fTracking;
};


// How far through the file, 0 to 1000; click or drag to seek. Sends its
// message (with the new Value()) while dragging.
class PositionBar : public BControl {
public:
								PositionBar(const char* name,
									BMessage* message);

	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual	void				MouseUp(BPoint where);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

			bool				IsTracking() const { return fTracking; }

private:
			void				_SeekTo(float x);

			bool				fTracking;
};


// Volume, 0 to 100: a knob with a red dot, turned by dragging up and down
// or with the scroll wheel.
class VolumeKnob : public BControl {
public:
								VolumeKnob(const char* name,
									BMessage* message);
	virtual						~VolumeKnob();

	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual	void				MouseUp(BPoint where);
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				KeyDown(const char* bytes, int32 count);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

private:
			void				_SetFromUser(int32 value);
			void				_RenderDisc(int32 size);

			BBitmap*			fDisc;
			bool				fTracking;
			float				fStartY;
			int32				fStartValue;
};

#endif	// POWERPLAY_CONTROLS_H

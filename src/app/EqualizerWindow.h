/*
 * PowerPlay — the equalizer, in PowerPlay's own look.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#ifndef POWERPLAY_EQUALIZER_WINDOW_H
#define POWERPLAY_EQUALIZER_WINDOW_H

#include <Window.h>

#include "Equalizer.h"

class BCheckBox;
class BMenuField;
class BSlider;


// What it changes goes to the app as kMsgSetEqualizer; the app sends back
// what to show with SetEqualizer(), as it does to a skin's equalizer.
class EqualizerWindow : public BWindow {
public:
								EqualizerWindow(BRect frame);

	virtual	bool				QuitRequested();
	virtual	void				MessageReceived(BMessage* message);

			// With the window locked
			void				SetEqualizer(bool on, float preamp,
									const float* bands);

private:
			void				_MarkPreset(const float* bands);

			BCheckBox*			fOn;
			BMenuField*			fPresets;
			BSlider*			fPreamp;
			BSlider*			fBands[audio::Equalizer::kBands];
};

#endif	// POWERPLAY_EQUALIZER_WINDOW_H

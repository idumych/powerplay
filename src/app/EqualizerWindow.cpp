/*
 * PowerPlay — the equalizer, in PowerPlay's own look.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#include "EqualizerWindow.h"

#include <Application.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Slider.h>
#include <StringView.h>

#include <math.h>
#include <stdio.h>

#include "Constants.h"


static const uint32 kMsgOnChanged = 'EqOn';
static const uint32 kMsgGainChanged = 'EqGn';	// "band": int32, -1 = preamp
static const uint32 kMsgPresetChosen = 'EqPr';	// "index": int32

// Sliders count tenths of a dB
static const int32 kScale = 10;
static const char* kBandLabels[audio::Equalizer::kBands] = {
	"60", "170", "310", "600", "1k", "3k", "6k", "12k", "14k", "16k"
};


static BSlider*
NewSlider(const char* name, const char* label, int32 band)
{
	BMessage* message = new BMessage(kMsgGainChanged);
	message->AddInt32("band", band);
	BSlider* slider = new BSlider(name, label, message,
		-12 * kScale, 12 * kScale, B_VERTICAL);
	slider->SetModificationMessage(new BMessage(*message));
	slider->SetHashMarks(B_HASH_MARKS_BOTH);
	slider->SetHashMarkCount(5);
	slider->SetValue(0);
	return slider;
}


EqualizerWindow::EqualizerWindow(BRect frame)
	:
	BWindow(frame, "Equalizer", B_TITLED_WINDOW, B_NOT_RESIZABLE
		| B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_ASYNCHRONOUS_CONTROLS)
{
	fOn = new BCheckBox("on", "On", new BMessage(kMsgOnChanged));

	BPopUpMenu* menu = new BPopUpMenu("Custom");
	menu->SetRadioMode(true);
	menu->SetLabelFromMarked(true);
	for (int i = 0; i < audio::kEqualizerPresetCount; i++) {
		BMessage* message = new BMessage(kMsgPresetChosen);
		message->AddInt32("index", i);
		menu->AddItem(new BMenuItem(audio::kEqualizerPresets[i].name,
			message));
	}
	fPresets = new BMenuField("presets", "Preset:", menu);

	fPreamp = NewSlider("preamp", "Preamp", -1);
	fPreamp->SetLimitLabels("-12 dB", "+12 dB");

	BGroupLayout* bands;
	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddGroup(B_HORIZONTAL)
			.Add(fOn)
			.AddGlue()
			.Add(fPresets)
		.End()
		.AddGroup(B_HORIZONTAL)
			.Add(fPreamp)
			.AddStrut(be_control_look->DefaultItemSpacing())
			.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
				.GetLayout(&bands)
			.End()
		.End();
	for (int i = 0; i < audio::Equalizer::kBands; i++) {
		char name[16];
		snprintf(name, sizeof(name), "band %d", i);
		fBands[i] = NewSlider(name, kBandLabels[i], i);
		bands->AddView(fBands[i]);
	}

	// Tall enough to set a band finely, wide enough for the widest label
	float height = roundf(be_plain_font->Size() * 13);
	float width = roundf(be_plain_font->StringWidth("16k") + 10);
	for (BSlider* slider : fBands)
		slider->SetExplicitMinSize(BSize(width, height));
	fPreamp->SetExplicitMinSize(BSize(
		roundf(be_plain_font->StringWidth("+12 dB") + 10), height));
}


bool
EqualizerWindow::QuitRequested()
{
	Hide();
	return false;
}


void
EqualizerWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgOnChanged:
		{
			BMessage set(kMsgSetEqualizer);
			set.AddBool("on", fOn->Value() == B_CONTROL_ON);
			be_app->PostMessage(&set);
			break;
		}
		case kMsgGainChanged:
		{
			int32 band = message->GetInt32("band", -1);
			BSlider* slider = band < 0 ? fPreamp : fBands[band];
			BMessage set(kMsgSetEqualizer);
			set.AddInt32("band", band);
			set.AddFloat("gain", (float)slider->Value() / kScale);
			be_app->PostMessage(&set);
			break;
		}
		case kMsgPresetChosen:
		{
			int32 index = message->GetInt32("index", 0);
			if (index < 0 || index >= audio::kEqualizerPresetCount)
				break;
			BMessage set(kMsgSetEqualizer);
			for (float gain : audio::kEqualizerPresets[index].bands)
				set.AddFloat("bands", gain);
			set.AddFloat("preamp", 0);
			be_app->PostMessage(&set);
			break;
		}
		default:
			BWindow::MessageReceived(message);
	}
}


void
EqualizerWindow::SetEqualizer(bool on, float preamp, const float* bands)
{
	fOn->SetValue(on ? B_CONTROL_ON : B_CONTROL_OFF);
	// A slider being dragged gets back the value it sent
	fPreamp->SetValue((int32)lroundf(preamp * kScale));
	for (int i = 0; i < audio::Equalizer::kBands; i++)
		fBands[i]->SetValue((int32)lroundf(bands[i] * kScale));
	_MarkPreset(bands);
}


// The preset the bands match, if any; "Custom" otherwise.
void
EqualizerWindow::_MarkPreset(const float* bands)
{
	BMenu* menu = fPresets->Menu();
	for (int i = 0; i < audio::kEqualizerPresetCount; i++) {
		bool same = true;
		for (int b = 0; b < audio::Equalizer::kBands; b++) {
			if (fabsf(audio::kEqualizerPresets[i].bands[b] - bands[b]) > 0.05f)
				same = false;
		}
		if (same) {
			menu->ItemAt(i)->SetMarked(true);
			return;
		}
	}
	BMenuItem* marked = menu->FindMarked();
	if (marked != NULL)
		marked->SetMarked(false);
	// With nothing marked the field shows the menu's own name
	fPresets->MenuItem()->SetLabel("Custom");
}

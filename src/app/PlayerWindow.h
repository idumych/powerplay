/*
 * PowerPlay — the controller: what's playing, the transport buttons, speed,
 * position, volume and time, laid out like SoundPlay's.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#ifndef POWERPLAY_PLAYER_WINDOW_H
#define POWERPLAY_PLAYER_WINDOW_H

#include <Window.h>

class BMenu;
class BMenuItem;
class BStringView;
class PositionBar;
class SpeedSlider;
class SymbolButton;
class TitleBox;
class VolumeKnob;
class VUView;


class PlayerWindow : public BWindow {
public:
								PlayerWindow(BRect frame);

	virtual	bool				QuitRequested();
	virtual	void				MenusBeginning();
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				DispatchMessage(BMessage* message,
									BHandler* handler);

			// Called by the app, with the window locked
			void				SetTitle(const char* fileName,
									const char* formatName);
			void				SetState(bool playing, double speed,
									bool hasFile);
			void				SetPosition(double position, double duration);
			void				SetVolume(float volume);
			void				SetRepeat(bool repeat);
			void				SetLively(bool lively);
			void				SetLevels(float left, float right);
			void				SetWindowMeter(bool show);
			bool				ShowsWindowMeter() const;

private:
			bool				_HandleShortcut(BMessage* message);

			TitleBox*			fBox;
			SymbolButton*		fPlayButton;
			SymbolButton*		fReverseButton;
			SymbolButton*		fStopButton;
			SymbolButton*		fPreviousButton;
			SymbolButton*		fNextButton;
			SpeedSlider*		fSpeed;
			PositionBar*		fPosition;
			VolumeKnob*			fVolume;
			BStringView*		fSpeedText;
			BStringView*		fTimeText;
			BMenuItem*			fPlayItem;
			BMenuItem*			fReverseItem;
			BMenuItem*			fRepeatItem;
			BMenuItem*			fDeskbarItem;
			BMenuItem*			fWindowMeterItem;
			BMenuItem*			fLivelyItem;
			BMenu*				fSkinsMenu;
			BView*				fMeterRow;
			VUView*				fMeter;
			bool				fShowRemaining;
			double				fLastPosition;
			double				fLastDuration;
};

#endif	// POWERPLAY_PLAYER_WINDOW_H

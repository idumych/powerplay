/*
 * PowerPlay — plays one audio file through the system mixer and meters it.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#ifndef POWERPLAY_PLAYER_H
#define POWERPLAY_PLAYER_H

#include <Entry.h>
#include <Locker.h>
#include <MediaDefs.h>
#include <String.h>

#include "VUMeter.h"

class BMediaFile;
class BMediaTrack;
class BSoundPlayer;


class Player {
public:
								Player();
								~Player();

			// Opens the file and gets it ready to play from the start.
			status_t			Open(const entry_ref& ref);
			void				Close();
			bool				HasFile() const { return fTrack != NULL; }
			const char*			FileName() const { return fName.String(); }

			void				Play();
			void				Pause();
			// Stops and goes back to the start.
			void				Stop();
			bool				IsPlaying() const;

			// The needles' current deflection. While nothing is being
			// played the needles fall back to rest in real time.
			void				Levels(float& left, float& right);

private:
	static	void				_PlayBuffer(void* cookie, void* buffer,
									size_t size,
									const media_raw_audio_format& format);
			void				_Fill(float* out, size_t frames);
			bool				_Decode();

			BMediaFile*			fFile;
			BMediaTrack*		fTrack;
			BSoundPlayer*		fSoundPlayer;
			media_raw_audio_format fFormat;
			BString				fName;

	mutable	BLocker				fLock;
			bool				fRunning;	// the sound player is started
			bool				fPlaying;
			bool				fAtEnd;
			bool				fWarnedFormat;

			// Decoded audio not yet handed to the sound player
			float*				fDecoded;
			int64				fDecodedFrames;		// in fDecoded
			int64				fDecodedPosition;	// next frame to hand on
			int64				fDecodedCapacity;

			vu::StereoMeter		fMeter;
			bigtime_t			fLastMetered;
};

#endif	// POWERPLAY_PLAYER_H

/*
 * PowerPlay — plays one audio file through the system mixer at any speed
 * from -400% to 400%, and meters it.
 *
 * A decoder thread fills a cache of decoded chunks ahead of the play head
 * (behind it when playing backwards); the sound player's thread only reads
 * the cache, so it never waits for the decoder.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#ifndef POWERPLAY_PLAYER_H
#define POWERPLAY_PLAYER_H

#include <Entry.h>
#include <Locker.h>
#include <MediaDefs.h>
#include <OS.h>
#include <String.h>

#include "PlayHead.h"
#include "VUMeter.h"

class BMediaFile;
class BMediaTrack;
class BSoundPlayer;


class Player {
public:
								Player();
								~Player();

			// Opens the file, ready to play from the start (or from the
			// end, for playing backwards).
			status_t			Open(const entry_ref& ref, bool atEnd = false);
			void				Close();
			bool				HasFile() const { return fTrack != NULL; }
			const char*			FileName() const { return fName.String(); }
			// The file format's short name, e.g. "wave", "mp3".
			const char*			FormatName() const
									{ return fFormatName.String(); }
			const entry_ref&	Ref() const { return fRef; }

			void				Play();
			void				Pause();
			// Stops and goes back to the start.
			void				Stop();
			bool				IsPlaying() const;

			// -4 to 4; negative plays backwards.
			void				SetSpeed(double speed);
			double				Speed() const;
			// 0 to 1, as a gain.
			void				SetVolume(float volume);
			float				Volume() const;

			double				Duration() const;	// seconds, 0 if unknown
			double				Position() const;	// seconds
			void				Seek(double seconds);

			// The needles' current deflection. While nothing is being
			// played the needles fall back to rest in real time.
			void				Levels(float& left, float& right);

			// Blocks where the decoder wasn't ready (for tests).
			int32				StarvedBlocks() const { return fStarved; }

private:
	static	void				_PlayBuffer(void* cookie, void* buffer,
									size_t size,
									const media_raw_audio_format& format);
			void				_Render(float* out, size_t frames,
									uint32 channels);
	static	status_t			_DecoderEntry(void* cookie);
			void				_DecoderLoop();
			bool				_DecodeChunk(int64 chunk, float* into);
			void				_StopDecoder();

			BMediaFile*			fFile;
			BMediaTrack*		fTrack;
			BSoundPlayer*		fSoundPlayer;
			media_raw_audio_format fFormat;		// decoded
			entry_ref			fRef;
			BString				fName;
			BString				fFormatName;

	mutable	BLocker				fLock;
			bool				fRunning;	// the sound player is started
			bool				fPlaying;
			bool				fEnded;
			bool				fWarnedFormat;

			audio::ChunkCache*	fCache;
			audio::PlayHead		fHead;
			double				fOutputRate;

			// The decoder thread's own state
			thread_id			fDecoder;
			sem_id				fWake;
	volatile bool				fQuitDecoder;
			int64				fStreamFrame;	// next frame ReadFrames gives
			float*				fReadBuffer;	// one ReadFrames' worth
			int64				fReadCapacity;
			int64				fReadFrames;	// left in fReadBuffer
			int64				fReadOffset;

			vu::StereoMeter		fMeter;
			bigtime_t			fLastMetered;
			int32				fStarved;
};

#endif	// POWERPLAY_PLAYER_H

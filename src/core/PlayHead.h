/*
 * PowerPlay — decoded audio cache and the play head that reads it at any
 * speed, forwards or backwards.
 *
 * Portable: no Haiku headers, so the host tests can use it.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#ifndef POWERPLAY_PLAY_HEAD_H
#define POWERPLAY_PLAY_HEAD_H

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace audio {

// Decoded audio in fixed-size chunks of interleaved float frames.
//
// A decoder thread fills chunks the play head will need next (Wanted()),
// decoding outside the lock into a slot it has claimed; the audio thread
// reads ready chunks under the lock and never waits for decoding. Playing
// backwards just means wanting the chunks before the play head instead of
// after it, so any format that can seek can play in reverse.
class ChunkCache {
public:
	static const int64_t kNoChunk = -1;

								ChunkCache(int channels, int64_t chunkFrames,
									int slots);

			int					Channels() const { return fChannels; }
			int64_t				ChunkFrames() const { return fChunkFrames; }

			// The length, when known. A decoder that runs out early sets
			// the real end with SetEnd().
			void				SetLength(int64_t frames);
			int64_t				Length() const;
			void				SetEnd(int64_t frames);
			// Forgets everything (a new file, or a seek far away).
			void				Clear();

	// Decoder side
			// The next chunk to decode for a play head at position moving
			// in the direction of speed, within lookahead chunks; kNoChunk
			// when everything wanted is ready.
			int64_t				Wanted(double position, double speed,
									int lookahead) const;
			// Claims a slot for a chunk and returns where to decode it
			// (ChunkFrames() frames), or NULL if every slot is needed. The
			// slot evicted is the one farthest from the play head.
			float*				BeginFill(int64_t chunk, double position);
			// The chunk is ready with this many frames (fewer at the end).
			void				EndFill(int64_t chunk, int64_t frames);

	// Audio side, with the cache locked (see Lock())
			// A ready chunk's frames, or NULL.
			const float*		Chunk(int64_t chunk, int64_t& frames) const;
			int64_t				LengthLocked() const { return fLength; }
			std::mutex&			Lock() const { return fLock; }

private:
	enum State { kEmpty, kFilling, kReady };
	struct Slot {
		int64_t				chunk = kNoChunk;
		int64_t				frames = 0;
		State				state = kEmpty;
		bool				stale = false;	// cleared while filling
		std::vector<float>	data;
	};

			const Slot*			_Find(int64_t chunk) const;

			int					fChannels;
			int64_t				fChunkFrames;
			int64_t				fLength;		// -1 = not known
			std::vector<Slot>	fSlots;
	mutable	std::mutex			fLock;
};


// Reads a ChunkCache at a speed from -4 to 4 (1 = normal, negative =
// backwards) with cubic interpolation, writing stereo float.
class PlayHead {
public:
	enum Result {
		kPlaying,		// the whole block was written
		kStarved,		// the decoder fell behind: the rest is silence and
						// the position waits for it
		kEnded			// reached the end (or the start, backwards)
	};

	static constexpr double kMinSpeed = -4.0;
	static constexpr double kMaxSpeed = 4.0;

								PlayHead();

			// Source frames per output frame at speed 1.
			void				SetRates(double sourceRate, double outputRate);
			void				SetSpeed(double speed);
			double				Speed() const { return fSpeed; }
			void				SetVolume(float volume) { fVolume = volume; }
			float				Volume() const { return fVolume; }
			// -1 (left only) to 1 (right only): the other side is turned
			// down as it moves away from the middle.
			void				SetBalance(float balance);
			float				Balance() const { return fBalance; }

			void				Seek(double frame) { fPosition = frame; }
			double				Position() const { return fPosition; }

			// Writes frames of interleaved stereo into out. A block where
			// the speed is 0 is silence and doesn't move.
			Result				Render(const ChunkCache& cache, float* out,
									size_t frames);

private:
			double				fStep;		// source frames per output frame
			double				fSpeed;
			float				fVolume;
			float				fBalance;
			double				fPosition;
};


// Seconds as "mm:ss", or "h:mm:ss" from an hour on.
void FormatTime(double seconds, char* buffer, size_t size);

}	// namespace audio

#endif	// POWERPLAY_PLAY_HEAD_H

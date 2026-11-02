/*
 * PowerPlay — decoded audio cache and the play head that reads it at any
 * speed, forwards or backwards.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#include "PlayHead.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace audio {


ChunkCache::ChunkCache(int channels, int64_t chunkFrames, int slots)
	:
	fChannels(std::max(channels, 1)),
	fChunkFrames(std::max<int64_t>(chunkFrames, 1)),
	fLength(-1),
	fSlots(std::max(slots, 2))
{
	for (Slot& slot : fSlots)
		slot.data.resize(size_t(fChunkFrames * fChannels));
}


void
ChunkCache::SetLength(int64_t frames)
{
	std::lock_guard<std::mutex> _(fLock);
	fLength = frames;
}


int64_t
ChunkCache::Length() const
{
	std::lock_guard<std::mutex> _(fLock);
	return fLength;
}


void
ChunkCache::SetEnd(int64_t frames)
{
	std::lock_guard<std::mutex> _(fLock);
	if (fLength < 0 || frames < fLength)
		fLength = frames;
}


void
ChunkCache::Clear()
{
	std::lock_guard<std::mutex> _(fLock);
	for (Slot& slot : fSlots) {
		if (slot.state == kFilling)
			slot.stale = true;
		else {
			slot.state = kEmpty;
			slot.chunk = kNoChunk;
		}
	}
	fLength = -1;
}


int64_t
ChunkCache::Wanted(double position, double speed, int lookahead) const
{
	std::lock_guard<std::mutex> _(fLock);
	int64_t chunkCount = fLength >= 0
		? (fLength + fChunkFrames - 1) / fChunkFrames : INT64_MAX;
	int64_t current = int64_t(std::floor(std::max(position, 0.0)
		/ fChunkFrames));
	if (chunkCount != INT64_MAX && current >= chunkCount)
		current = chunkCount - 1;
	int direction = speed < 0 ? -1 : 1;

	// Ahead in the direction of play, then the one just behind (for the
	// interpolation across a chunk boundary after a seek).
	for (int k = 0; k <= lookahead + 1; k++) {
		int64_t chunk = k <= lookahead ? current + direction * k
			: current - direction;
		if (chunk < 0 || chunk >= chunkCount)
			continue;
		const Slot* slot = _Find(chunk);
		if (slot == NULL)
			return chunk;
	}
	return kNoChunk;
}


float*
ChunkCache::BeginFill(int64_t chunk, double position)
{
	std::lock_guard<std::mutex> _(fLock);
	if (_Find(chunk) != NULL)
		return NULL;

	int64_t current = int64_t(std::floor(std::max(position, 0.0)
		/ fChunkFrames));
	Slot* victim = NULL;
	for (Slot& slot : fSlots) {
		if (slot.state == kEmpty) {
			victim = &slot;
			break;
		}
	}
	if (victim == NULL) {
		// The ready chunk farthest from the play head goes, but only if
		// it's farther than the one we want.
		int64_t farthest = -1;
		for (Slot& slot : fSlots) {
			if (slot.state != kReady)
				continue;
			int64_t distance = std::llabs(slot.chunk - current);
			if (distance > farthest) {
				farthest = distance;
				victim = &slot;
			}
		}
		if (victim == NULL || farthest <= std::llabs(chunk - current))
			return NULL;
	}
	victim->chunk = chunk;
	victim->frames = 0;
	victim->state = kFilling;
	victim->stale = false;
	return victim->data.data();
}


void
ChunkCache::EndFill(int64_t chunk, int64_t frames)
{
	std::lock_guard<std::mutex> _(fLock);
	for (Slot& slot : fSlots) {
		if (slot.state != kFilling || slot.chunk != chunk)
			continue;
		if (slot.stale || frames <= 0) {
			slot.state = kEmpty;
			slot.chunk = kNoChunk;
			slot.stale = false;
		} else {
			slot.frames = std::min(frames, fChunkFrames);
			slot.state = kReady;
		}
		return;
	}
}


const float*
ChunkCache::Chunk(int64_t chunk, int64_t& frames) const
{
	for (const Slot& slot : fSlots) {
		if (slot.chunk == chunk && slot.state == kReady) {
			frames = slot.frames;
			return slot.data.data();
		}
	}
	return NULL;
}


const ChunkCache::Slot*
ChunkCache::_Find(int64_t chunk) const
{
	for (const Slot& slot : fSlots) {
		if (slot.chunk == chunk && slot.state != kEmpty)
			return &slot;
	}
	return NULL;
}


//	#pragma mark - PlayHead


namespace {

// Fetches frames from the cache, remembering the last chunk it used.
class FrameReader {
public:
	FrameReader(const ChunkCache& cache)
		:
		fCache(cache),
		fChannels(cache.Channels()),
		fChunkFrames(cache.ChunkFrames()),
		fChunk(ChunkCache::kNoChunk),
		fData(NULL),
		fFrames(0)
	{
	}

	// Left and right of one source frame (mono feeds both); false if its
	// chunk isn't ready.
	bool Get(int64_t frame, float& left, float& right)
	{
		int64_t chunk = frame / fChunkFrames;
		if (chunk != fChunk) {
			fChunk = chunk;
			fData = fCache.Chunk(chunk, fFrames);
		}
		int64_t offset = frame - chunk * fChunkFrames;
		if (fData == NULL || offset >= fFrames)
			return false;
		const float* sample = fData + offset * fChannels;
		left = sample[0];
		right = fChannels > 1 ? sample[1] : sample[0];
		return true;
	}

private:
	const ChunkCache&	fCache;
	int					fChannels;
	int64_t				fChunkFrames;
	int64_t				fChunk;
	const float*		fData;
	int64_t				fFrames;
};


// Catmull-Rom through four points, at t in [0, 1) between p1 and p2.
inline float
Cubic(float p0, float p1, float p2, float p3, float t)
{
	return p1 + 0.5f * t * (p2 - p0 + t * (2.0f * p0 - 5.0f * p1
		+ 4.0f * p2 - p3 + t * (3.0f * (p1 - p2) + p3 - p0)));
}

}	// namespace


PlayHead::PlayHead()
	:
	fStep(1.0),
	fSpeed(1.0),
	fVolume(1.0f),
	fBalance(0.0f),
	fPosition(0)
{
}


void
PlayHead::SetRates(double sourceRate, double outputRate)
{
	fStep = sourceRate > 0 && outputRate > 0 ? sourceRate / outputRate : 1.0;
}


void
PlayHead::SetBalance(float balance)
{
	fBalance = std::clamp(balance, -1.0f, 1.0f);
}


void
PlayHead::SetSpeed(double speed)
{
	fSpeed = std::clamp(speed, kMinSpeed, kMaxSpeed);
}


PlayHead::Result
PlayHead::Render(const ChunkCache& cache, float* out, size_t frames)
{
	std::memset(out, 0, frames * 2 * sizeof(float));
	if (fSpeed == 0)
		return kPlaying;

	std::lock_guard<std::mutex> _(cache.Lock());
	FrameReader reader(cache);
	const int64_t length = cache.LengthLocked();	// -1 = not known yet
	const double step = fSpeed * fStep;
	const float leftGain = fVolume * (fBalance > 0 ? 1.0f - fBalance : 1.0f);
	const float rightGain = fVolume * (fBalance < 0 ? 1.0f + fBalance : 1.0f);

	for (size_t i = 0; i < frames; i++) {
		if (step < 0 && fPosition < 0) {
			fPosition = 0;
			return kEnded;
		}
		if (length >= 0 && fPosition > length - 1) {
			// Past the end: done going forwards. Going backwards (from
			// an estimated length the file turned out shorter than),
			// start from the real end.
			fPosition = std::max<double>(length - 1, 0);
			if (step > 0)
				return kEnded;
		}

		int64_t index = int64_t(fPosition);
		float t = float(fPosition - index);
		float l1, r1, l2, r2;
		if (!reader.Get(index, l1, r1)) {
			// Not decoded yet: wait here rather than skip.
			return kStarved;
		}
		if (!reader.Get(index + 1, l2, r2)) {
			if (length >= 0 && index + 1 >= length) {
				l2 = l1;
				r2 = r1;
			} else
				return kStarved;
		}
		// The outer points only shape the curve: repeat the inner ones
		// at the edges or where a neighbour chunk isn't there.
		float l0, r0, l3, r3;
		if (index == 0 || !reader.Get(index - 1, l0, r0)) {
			l0 = l1;
			r0 = r1;
		}
		if (!reader.Get(index + 2, l3, r3)) {
			l3 = l2;
			r3 = r2;
		}
		out[i * 2] = Cubic(l0, l1, l2, l3, t) * leftGain;
		out[i * 2 + 1] = Cubic(r0, r1, r2, r3, t) * rightGain;
		fPosition += step;
	}
	return kPlaying;
}


void
FormatTime(double seconds, char* buffer, size_t size)
{
	if (!(seconds >= 0))
		seconds = 0;
	long total = long(seconds);
	long hours = total / 3600;
	long minutes = (total / 60) % 60;
	long secs = total % 60;
	if (hours > 0)
		snprintf(buffer, size, "%ld:%02ld:%02ld", hours, minutes, secs);
	else
		snprintf(buffer, size, "%02ld:%02ld", minutes, secs);
}

}	// namespace audio

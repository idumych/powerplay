/*
 * PowerPlay — plays one audio file through the system mixer at any speed
 * from -400% to 400%, and meters it.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#include "Player.h"

#include <Application.h>
#include <Autolock.h>
#include <MediaFile.h>
#include <MediaTrack.h>
#include <Path.h>
#include <SoundPlayer.h>

#include <stdio.h>
#include <string.h>

#include "Constants.h"

// About 0.37 s per chunk at 44.1 kHz; 32 of them cache 12 s, enough for a
// second of look-ahead at 4x in either direction.
static const int64 kChunkFrames = 16384;
static const int32 kCacheSlots = 32;
static const int32 kLookahead = 12;
static const size_t kOutputFrames = 1024;


Player::Player()
	:
	fFile(NULL),
	fTrack(NULL),
	fSoundPlayer(NULL),
	fLock("player"),
	fRunning(false),
	fPlaying(false),
	fEnded(false),
	fWarnedFormat(false),
	fCache(NULL),
	fOutputRate(0),
	fDecoder(-1),
	fWake(create_sem(0, "PowerPlay decoder wake")),
	fQuitDecoder(false),
	fStreamFrame(0),
	fReadBuffer(NULL),
	fReadCapacity(0),
	fReadFrames(0),
	fReadOffset(0),
	fLastMetered(system_time()),
	fStarved(0)
{
	memset(&fFormat, 0, sizeof(fFormat));
}


Player::~Player()
{
	Close();
	if (fSoundPlayer != NULL) {
		fSoundPlayer->Stop();
		delete fSoundPlayer;
	}
	delete_sem(fWake);
}


status_t
Player::Open(const entry_ref& ref, bool atEnd)
{
	Close();

	BMediaFile* file = new BMediaFile(&ref);
	status_t status = file->InitCheck();
	if (status != B_OK) {
		delete file;
		return status;
	}

	// The first audio track, decoded to float at the file's own rate
	BMediaTrack* track = NULL;
	media_format format;
	for (int32 i = 0; i < file->CountTracks(); i++) {
		BMediaTrack* candidate = file->TrackAt(i);
		if (candidate == NULL)
			continue;
		format.Clear();
		format.type = B_MEDIA_RAW_AUDIO;
		format.u.raw_audio.format = media_raw_audio_format::B_AUDIO_FLOAT;
		if (candidate->DecodedFormat(&format) == B_OK
			&& format.type == B_MEDIA_RAW_AUDIO
			&& format.u.raw_audio.format
				== media_raw_audio_format::B_AUDIO_FLOAT) {
			track = candidate;
			break;
		}
		file->ReleaseTrack(candidate);
	}
	if (track == NULL) {
		delete file;
		return B_MEDIA_BAD_FORMAT;
	}

	media_raw_audio_format decoded = format.u.raw_audio;
	if (decoded.channel_count < 1)
		decoded.channel_count = 2;
	if (decoded.frame_rate <= 0)
		decoded.frame_rate = 44100;

	// One stereo float output at the file's rate; the mixer converts it.
	if (fSoundPlayer == NULL || fOutputRate != decoded.frame_rate) {
		if (fSoundPlayer != NULL) {
			fSoundPlayer->Stop();
			delete fSoundPlayer;
			fSoundPlayer = NULL;
			fRunning = false;
		}
		media_raw_audio_format output = media_raw_audio_format::wildcard;
		output.frame_rate = decoded.frame_rate;
		output.channel_count = 2;
		output.format = media_raw_audio_format::B_AUDIO_FLOAT;
		output.byte_order = B_MEDIA_HOST_ENDIAN;
		output.buffer_size = kOutputFrames * 2 * sizeof(float);
		BSoundPlayer* soundPlayer = new BSoundPlayer(&output, kAppName,
			&_PlayBuffer, NULL, this);
		status = soundPlayer->InitCheck();
		if (status != B_OK) {
			delete soundPlayer;
			file->ReleaseTrack(track);
			delete file;
			return status;
		}
		fSoundPlayer = soundPlayer;
		fOutputRate = decoded.frame_rate;
	}

	media_file_format fileFormat;
	BString formatName;
	if (file->GetFileFormatInfo(&fileFormat) == B_OK)
		formatName = fileFormat.short_name;

	size_t frameSize = sizeof(float) * decoded.channel_count;
	int64 readCapacity = 2 * (decoded.buffer_size / frameSize);
	if (readCapacity < 8192)
		readCapacity = 8192;

	audio::ChunkCache* cache = new audio::ChunkCache(decoded.channel_count,
		kChunkFrames, kCacheSlots);
	int64 length = track->CountFrames();
	if (length > 0)
		cache->SetLength(length);

	{
		BAutolock _(fLock);
		fFile = file;
		fTrack = track;
		fFormat = decoded;
		fRef = ref;
		// The leaf name (on Linux an entry_ref's name is the whole path)
		fName = BPath(&ref).Leaf();
		fFormatName = formatName;
		fCache = cache;
		fHead.SetRates(decoded.frame_rate, fOutputRate);
		fHead.Seek(atEnd && length > 0 ? length - 1 : 0);
		fEnded = false;
		fPlaying = false;
		fMeter.SetSampleRate(fOutputRate);
	}

	fReadBuffer = new float[readCapacity * decoded.channel_count];
	fReadCapacity = readCapacity;
	fReadFrames = fReadOffset = 0;
	fStreamFrame = 0;
	fQuitDecoder = false;
	fDecoder = spawn_thread(&_DecoderEntry, "PowerPlay decoder",
		B_NORMAL_PRIORITY + 2, this);
	if (fDecoder >= 0)
		resume_thread(fDecoder);
	return B_OK;
}


void
Player::Close()
{
	{
		// The sound player keeps running (silence) so the next file
		// starts without a gap; it just stops reading this one.
		BAutolock _(fLock);
		fPlaying = false;
	}
	_StopDecoder();

	BAutolock _(fLock);
	if (fFile != NULL) {
		if (fTrack != NULL)
			fFile->ReleaseTrack(fTrack);
		delete fFile;
	}
	fFile = NULL;
	fTrack = NULL;
	delete fCache;
	fCache = NULL;
	delete[] fReadBuffer;
	fReadBuffer = NULL;
	fReadCapacity = fReadFrames = fReadOffset = 0;
	fName = "";
	fFormatName = "";
	fEnded = false;
}


void
Player::Play()
{
	if (fSoundPlayer == NULL)
		return;
	{
		BAutolock _(fLock);
		if (fCache == NULL)
			return;
		int64 length = fCache->Length();
		if (fEnded) {
			// Again from the start, or from the end going backwards.
			fHead.Seek(fHead.Speed() < 0 && length > 0 ? length - 1 : 0);
			fEnded = false;
		} else if (fHead.Speed() < 0 && fHead.Position() <= 0 && length > 0) {
			// Backwards from the very start (after Stop, say) means from
			// the end.
			fHead.Seek(length - 1);
		}
		fPlaying = true;
	}
	release_sem(fWake);
	if (!fRunning) {
		// The sound player then keeps running while paused, playing
		// silence, so the needles fall the way they do when music stops.
		fSoundPlayer->Start();
		fSoundPlayer->SetHasData(true);
		BAutolock _(fLock);
		fRunning = true;
	}
}


void
Player::Pause()
{
	BAutolock _(fLock);
	fPlaying = false;
}


void
Player::Stop()
{
	if (fSoundPlayer != NULL && fRunning)
		fSoundPlayer->Stop();

	BAutolock _(fLock);
	fRunning = false;
	fPlaying = false;
	fEnded = false;
	fHead.Seek(0);
	fLastMetered = system_time();
	release_sem(fWake);
}


bool
Player::IsPlaying() const
{
	BAutolock _(fLock);
	return fPlaying;
}


void
Player::SetSpeed(double speed)
{
	{
		BAutolock _(fLock);
		fHead.SetSpeed(speed);
	}
	release_sem(fWake);
}


double
Player::Speed() const
{
	BAutolock _(fLock);
	return fHead.Speed();
}


void
Player::SetVolume(float volume)
{
	BAutolock _(fLock);
	fHead.SetVolume(volume < 0 ? 0 : volume > 1 ? 1 : volume);
}


float
Player::Volume() const
{
	BAutolock _(fLock);
	return fHead.Volume();
}


double
Player::Duration() const
{
	BAutolock _(fLock);
	if (fCache == NULL || fFormat.frame_rate <= 0)
		return 0;
	int64 length = fCache->Length();
	return length > 0 ? length / fFormat.frame_rate : 0;
}


double
Player::Position() const
{
	BAutolock _(fLock);
	if (fFormat.frame_rate <= 0)
		return 0;
	return fHead.Position() / fFormat.frame_rate;
}


void
Player::Seek(double seconds)
{
	{
		BAutolock _(fLock);
		if (fCache == NULL)
			return;
		double frame = seconds * fFormat.frame_rate;
		int64 length = fCache->Length();
		if (frame < 0)
			frame = 0;
		if (length > 0 && frame > length - 1)
			frame = length - 1;
		fHead.Seek(frame);
		fEnded = false;
	}
	release_sem(fWake);
}


void
Player::Levels(float& left, float& right)
{
	BAutolock _(fLock);
	// When the sound player isn't running, nothing feeds the meter: let
	// the needles fall for the time that has passed.
	bigtime_t now = system_time();
	if (!fRunning) {
		fMeter.Advance((now - fLastMetered) / 1000000.0);
		fLastMetered = now;
	}
	left = fMeter.Left();
	right = fMeter.Right();
}


//	#pragma mark - Audio thread


/*static*/ void
Player::_PlayBuffer(void* cookie, void* buffer, size_t size,
	const media_raw_audio_format& format)
{
	Player* player = (Player*)cookie;
	if (format.format != media_raw_audio_format::B_AUDIO_FLOAT
		|| format.channel_count != 2) {
		// We asked for stereo float; anything else is a bug.
		memset(buffer, 0, size);
		if (!player->fWarnedFormat) {
			fprintf(stderr, "PowerPlay: sound player gave format %#x with "
				"%" B_PRIu32 " channels\n", (unsigned)format.format,
				format.channel_count);
			player->fWarnedFormat = true;
		}
		return;
	}
	player->_Render((float*)buffer, size / (2 * sizeof(float)), 2);
}


void
Player::_Render(float* out, size_t frames, uint32 channels)
{
	BAutolock _(fLock);
	bool wake = false;
	if (fPlaying && fCache != NULL) {
		audio::PlayHead::Result result = fHead.Render(*fCache, out, frames);
		if (result == audio::PlayHead::kStarved)
			fStarved++;
		else if (result == audio::PlayHead::kEnded) {
			fEnded = true;
			fPlaying = false;
			BMessage ended(kMsgPlaybackEnded);
			ended.AddBool("backwards", fHead.Speed() < 0);
			be_app->PostMessage(&ended);
		}
		wake = true;
	} else
		memset(out, 0, frames * channels * sizeof(float));

	// The meter sees exactly what goes to the mixer, silence included.
	fMeter.ProcessInterleaved(out, frames, channels);
	fLastMetered = system_time();

	if (wake)
		release_sem_etc(fWake, 1, B_DO_NOT_RESCHEDULE);
}


//	#pragma mark - Decoder thread


/*static*/ status_t
Player::_DecoderEntry(void* cookie)
{
	((Player*)cookie)->_DecoderLoop();
	return B_OK;
}


void
Player::_DecoderLoop()
{
	while (!fQuitDecoder) {
		double position, speed;
		{
			BAutolock _(fLock);
			position = fHead.Position();
			speed = fHead.Speed();
		}
		if (speed == 0)
			speed = 1;

		int64 chunk = fCache->Wanted(position, speed, kLookahead);
		float* into = chunk != audio::ChunkCache::kNoChunk
			? fCache->BeginFill(chunk, position) : NULL;
		if (into == NULL) {
			// Everything wanted is ready: wait for the play head to move.
			acquire_sem_etc(fWake, 1, B_RELATIVE_TIMEOUT, 50000);
			continue;
		}
		_DecodeChunk(chunk, into);
	}
}


// Decodes chunk into its slot and hands it to the cache. Returns false at
// the end of the file.
bool
Player::_DecodeChunk(int64 chunk, float* into)
{
	const int32 channels = fFormat.channel_count;
	const int64 target = chunk * kChunkFrames;

	if (fStreamFrame != target) {
		// Not where the stream is: seek (to the closest frame before,
		// then read forward to the one we want).
		int64 frame = target;
		if (fTrack->SeekToFrame(&frame, B_MEDIA_SEEK_CLOSEST_BACKWARD)
				!= B_OK) {
			frame = target;
			fTrack->SeekToFrame(&frame);
		}
		fStreamFrame = frame;
		fReadFrames = fReadOffset = 0;
	}

	int64 written = 0;
	bool more = true;
	while (written < kChunkFrames) {
		if (fReadFrames == 0) {
			int64 frames = fReadCapacity;
			media_header header;
			if (fTrack->ReadFrames(fReadBuffer, &frames, &header) != B_OK
				|| frames <= 0) {
				// The real end, which may be before CountFrames() said.
				fCache->SetEnd(fStreamFrame);
				more = false;
				break;
			}
			fReadFrames = frames;
			fReadOffset = 0;
		}

		int64 wanted = target + written;
		if (fStreamFrame < wanted) {
			// Before the chunk (after a seek): skip.
			int64 skip = wanted - fStreamFrame;
			if (skip > fReadFrames)
				skip = fReadFrames;
			fReadOffset += skip;
			fReadFrames -= skip;
			fStreamFrame += skip;
			continue;
		}
		if (fStreamFrame > wanted) {
			// The seek landed past the chunk's start: silence for the gap
			// rather than shifting everything.
			int64 gap = fStreamFrame - wanted;
			if (gap > kChunkFrames - written)
				gap = kChunkFrames - written;
			memset(into + written * channels, 0,
				gap * channels * sizeof(float));
			written += gap;
			continue;
		}

		int64 count = fReadFrames;
		if (count > kChunkFrames - written)
			count = kChunkFrames - written;
		memcpy(into + written * channels,
			fReadBuffer + fReadOffset * channels,
			count * channels * sizeof(float));
		written += count;
		fReadOffset += count;
		fReadFrames -= count;
		fStreamFrame += count;
	}

	fCache->EndFill(chunk, written);
	return more;
}


void
Player::_StopDecoder()
{
	if (fDecoder < 0)
		return;
	fQuitDecoder = true;
	release_sem(fWake);
	status_t result;
	wait_for_thread(fDecoder, &result);
	fDecoder = -1;
}

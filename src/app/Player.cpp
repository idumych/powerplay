/*
 * PowerPlay — plays one audio file through the system mixer and meters it.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#include "Player.h"

#include <Application.h>
#include <Autolock.h>
#include <MediaFile.h>
#include <MediaTrack.h>
#include <OS.h>
#include <SoundPlayer.h>

#include <stdio.h>
#include <string.h>

#include "Constants.h"


Player::Player()
	:
	fFile(NULL),
	fTrack(NULL),
	fSoundPlayer(NULL),
	fLock("player"),
	fRunning(false),
	fPlaying(false),
	fAtEnd(false),
	fWarnedFormat(false),
	fDecoded(NULL),
	fDecodedFrames(0),
	fDecodedPosition(0),
	fDecodedCapacity(0),
	fLastMetered(system_time())
{
	memset(&fFormat, 0, sizeof(fFormat));
}


Player::~Player()
{
	Close();
}


status_t
Player::Open(const entry_ref& ref)
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

	fFormat = format.u.raw_audio;
	if (fFormat.channel_count < 1)
		fFormat.channel_count = 2;
	size_t frameSize = sizeof(float) * fFormat.channel_count;
	int64 capacity = fFormat.buffer_size / frameSize;
	if (capacity < 1024)
		capacity = 1024;

	BSoundPlayer* soundPlayer = new BSoundPlayer(&fFormat, kAppName,
		&_PlayBuffer, NULL, this);
	status = soundPlayer->InitCheck();
	if (status != B_OK) {
		delete soundPlayer;
		file->ReleaseTrack(track);
		delete file;
		return status;
	}

	BAutolock _(fLock);
	fFile = file;
	fTrack = track;
	fSoundPlayer = soundPlayer;
	fName = ref.name;
	fDecoded = new float[capacity * fFormat.channel_count];
	fDecodedCapacity = capacity;
	fDecodedFrames = fDecodedPosition = 0;
	fAtEnd = false;
	fPlaying = false;
	fMeter.SetSampleRate(fFormat.frame_rate);
	return B_OK;
}


void
Player::Close()
{
	// Stop the sound player first: its thread uses everything below.
	if (fSoundPlayer != NULL) {
		fSoundPlayer->Stop();
		delete fSoundPlayer;
	}

	BAutolock _(fLock);
	fSoundPlayer = NULL;
	fRunning = false;
	if (fFile != NULL) {
		if (fTrack != NULL)
			fFile->ReleaseTrack(fTrack);
		delete fFile;
	}
	fFile = NULL;
	fTrack = NULL;
	delete[] fDecoded;
	fDecoded = NULL;
	fDecodedCapacity = fDecodedFrames = fDecodedPosition = 0;
	fPlaying = false;
	fName = "";
}


void
Player::Play()
{
	if (fSoundPlayer == NULL)
		return;

	{
		BAutolock _(fLock);
		if (fAtEnd) {
			int64 frame = 0;
			fTrack->SeekToFrame(&frame);
			fDecodedFrames = fDecodedPosition = 0;
			fAtEnd = false;
		}
		fPlaying = true;
	}
	// The sound player keeps running while paused, playing silence, so the
	// needles fall the way they do when the music stops.
	fSoundPlayer->Start();
	fSoundPlayer->SetHasData(true);
	BAutolock _(fLock);
	fRunning = true;
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
	if (fSoundPlayer == NULL)
		return;
	fSoundPlayer->Stop();

	BAutolock _(fLock);
	fRunning = false;
	fPlaying = false;
	fAtEnd = false;
	int64 frame = 0;
	fTrack->SeekToFrame(&frame);
	fDecodedFrames = fDecodedPosition = 0;
	fLastMetered = system_time();
}


bool
Player::IsPlaying() const
{
	BAutolock _(fLock);
	return fPlaying;
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


/*static*/ void
Player::_PlayBuffer(void* cookie, void* buffer, size_t size,
	const media_raw_audio_format& format)
{
	Player* player = (Player*)cookie;
	if (format.format != media_raw_audio_format::B_AUDIO_FLOAT
		|| format.channel_count != player->fFormat.channel_count) {
		// We asked for the file's own format; anything else is a bug.
		memset(buffer, 0, size);
		if (!player->fWarnedFormat) {
			fprintf(stderr, "PowerPlay: sound player gave format %#x with "
				"%" B_PRIu32 " channels\n", (unsigned)format.format,
				format.channel_count);
			player->fWarnedFormat = true;
		}
		return;
	}
	player->_Fill((float*)buffer,
		size / (sizeof(float) * format.channel_count));
}


void
Player::_Fill(float* out, size_t frames)
{
	BAutolock _(fLock);
	uint32 channels = fFormat.channel_count;
	float* start = out;
	size_t remaining = frames;

	while (remaining > 0 && fPlaying && !fAtEnd) {
		if (fDecodedPosition >= fDecodedFrames && !_Decode())
			break;
		size_t count = fDecodedFrames - fDecodedPosition;
		if (count > remaining)
			count = remaining;
		memcpy(out, fDecoded + fDecodedPosition * channels,
			count * channels * sizeof(float));
		fDecodedPosition += count;
		out += count * channels;
		remaining -= count;
	}
	if (remaining > 0)
		memset(out, 0, remaining * channels * sizeof(float));

	// The meter sees exactly what goes to the mixer, silence included.
	fMeter.ProcessInterleaved(start, frames, channels);
	fLastMetered = system_time();
}


bool
Player::_Decode()
{
	int64 frames = fDecodedCapacity;
	media_header header;
	status_t status = fTrack->ReadFrames(fDecoded, &frames, &header);
	if (status != B_OK || frames <= 0) {
		fDecodedFrames = fDecodedPosition = 0;
		fAtEnd = true;
		fPlaying = false;
		be_app->PostMessage(kMsgPlaybackEnded);
		return false;
	}
	// ReadFrames can return more than one buffer's worth; it never writes
	// more than the track's own buffer size, which Open() sized us for.
	fDecodedFrames = frames;
	fDecodedPosition = 0;
	return true;
}

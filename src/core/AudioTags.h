/*
 * PowerPlay — song titles, artists and albums from the files' own tags:
 * ID3v2 and ID3v1 (MP3), FLAC's and Ogg's Vorbis comments (FLAC, Vorbis,
 * Opus); and songs' lengths, where the file says without being decoded
 * (MP3, FLAC, Ogg, WAV).
 *
 * Portable: no Haiku headers, so the host tests can use it.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#ifndef POWERPLAY_AUDIO_TAGS_H
#define POWERPLAY_AUDIO_TAGS_H

#include <cstdint>
#include <functional>
#include <string>

namespace audio {

struct Tags {
	// UTF-8; empty when the file doesn't say
	std::string			title;
	std::string			artist;
	std::string			album;
	int					track = 0;
	// Seconds, when the file says without decoding it; 0 otherwise
	double				duration = 0;
};

// Reads size bytes at offset into buffer; returns how many it read.
typedef std::function<size_t(uint64_t offset, void* buffer, size_t size)>
	TagSource;

// Fills in what the file's tags say, and its length. False when it found
// no tags.
bool ReadTags(const TagSource& read, uint64_t fileSize, Tags& tags);

// "Song.mp3" -> "Song", for files with no title.
std::string TitleFromFileName(const std::string& path);

}	// namespace audio

#endif	// POWERPLAY_AUDIO_TAGS_H

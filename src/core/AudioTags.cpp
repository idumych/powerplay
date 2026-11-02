/*
 * PowerPlay — song tags.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#include "AudioTags.h"

#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <vector>

namespace audio {

// Tags bigger than this are cover art; the text frames come before it in
// practice, and nothing is read past it.
static const uint32_t kMaxTagRead = 1024 * 1024;


static uint32_t
BigEndian(const uint8_t* p, int bytes)
{
	uint32_t value = 0;
	for (int i = 0; i < bytes; i++)
		value = (value << 8) | p[i];
	return value;
}


static uint32_t
LittleEndian32(const uint8_t* p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}


static uint32_t
SyncSafe(const uint8_t* p)
{
	return (p[0] & 0x7f) << 21 | (p[1] & 0x7f) << 14 | (p[2] & 0x7f) << 7
		| (p[3] & 0x7f);
}


static void
AppendUTF8(std::string& out, uint32_t c)
{
	if (c < 0x80)
		out += char(c);
	else if (c < 0x800) {
		out += char(0xc0 | (c >> 6));
		out += char(0x80 | (c & 0x3f));
	} else if (c < 0x10000) {
		out += char(0xe0 | (c >> 12));
		out += char(0x80 | ((c >> 6) & 0x3f));
		out += char(0x80 | (c & 0x3f));
	} else {
		out += char(0xf0 | (c >> 18));
		out += char(0x80 | ((c >> 12) & 0x3f));
		out += char(0x80 | ((c >> 6) & 0x3f));
		out += char(0x80 | (c & 0x3f));
	}
}


static std::string
Latin1(const uint8_t* p, size_t size)
{
	std::string out;
	for (size_t i = 0; i < size && p[i] != 0; i++)
		AppendUTF8(out, p[i]);
	return out;
}


// UTF-16 up to a NUL; little-endian unless a BOM says otherwise.
static std::string
UTF16(const uint8_t* p, size_t size, bool bigEndian)
{
	size_t i = 0;
	if (size >= 2 && p[0] == 0xff && p[1] == 0xfe) {
		bigEndian = false;
		i = 2;
	} else if (size >= 2 && p[0] == 0xfe && p[1] == 0xff) {
		bigEndian = true;
		i = 2;
	}
	std::string out;
	for (; i + 1 < size; i += 2) {
		uint32_t c = bigEndian ? (p[i] << 8 | p[i + 1])
			: (p[i] | p[i + 1] << 8);
		if (c == 0)
			break;
		if (c >= 0xd800 && c < 0xdc00 && i + 3 < size) {
			uint32_t low = bigEndian ? (p[i + 2] << 8 | p[i + 3])
				: (p[i + 2] | p[i + 3] << 8);
			if (low >= 0xdc00 && low < 0xe000) {
				c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
				i += 2;
			}
		}
		AppendUTF8(out, c);
	}
	return out;
}


static std::string
Trim(std::string text)
{
	size_t end = text.find('\0');
	if (end != std::string::npos)
		text.erase(end);
	while (!text.empty() && (text.back() == ' ' || text.back() == '\t'
			|| text.back() == '\r' || text.back() == '\n'))
		text.pop_back();
	size_t start = 0;
	while (start < text.size() && text[start] == ' ')
		start++;
	return text.substr(start);
}


// An ID3v2 text frame's value: its encoding byte, then the text (only the
// first of several NUL-separated values).
static std::string
ID3Text(const uint8_t* p, size_t size)
{
	if (size < 1)
		return "";
	switch (p[0]) {
		case 0:
			return Trim(Latin1(p + 1, size - 1));
		case 1:
			return Trim(UTF16(p + 1, size - 1, false));
		case 2:
			return Trim(UTF16(p + 1, size - 1, true));
		default:
			return Trim(std::string((const char*)p + 1, size - 1));
	}
}


static void
SetIfEmpty(std::string& field, const std::string& value)
{
	if (field.empty() && !value.empty())
		field = value;
}


// Undoes ID3's unsynchronisation: FF 00 -> FF.
static void
Resync(std::vector<uint8_t>& data)
{
	size_t out = 0;
	for (size_t i = 0; i < data.size(); i++) {
		data[out++] = data[i];
		if (data[i] == 0xff && i + 1 < data.size() && data[i + 1] == 0)
			i++;
	}
	data.resize(out);
}


// The ID3v2 tag at offset, if there is one; its whole size (0 if none).
static uint32_t
ReadID3v2(const TagSource& read, uint64_t offset, Tags& tags, bool& found)
{
	uint8_t header[10];
	if (read(offset, header, 10) != 10 || memcmp(header, "ID3", 3) != 0
		|| header[3] < 2 || header[3] > 4)
		return 0;
	int version = header[3];
	uint8_t flags = header[5];
	uint32_t size = SyncSafe(header + 6);
	uint32_t total = 10 + size + ((flags & 0x10) != 0 ? 10 : 0);

	std::vector<uint8_t> data(size < kMaxTagRead ? size : kMaxTagRead);
	data.resize(read(offset + 10, data.data(), data.size()));
	if (version < 4 && (flags & 0x80) != 0)
		Resync(data);

	size_t pos = 0;
	if ((flags & 0x40) != 0 && data.size() >= 4) {
		// The extended header
		if (version == 3)
			pos = 4 + BigEndian(data.data(), 4);
		else if (version == 4)
			pos = SyncSafe(data.data());
	}

	size_t idSize = version == 2 ? 3 : 4;
	size_t headerSize = version == 2 ? 6 : 10;
	while (pos + headerSize <= data.size()) {
		const uint8_t* frame = data.data() + pos;
		if (frame[0] == 0)
			break;	// padding
		std::string id((const char*)frame, idSize);
		uint32_t frameSize = version == 2 ? BigEndian(frame + 3, 3)
			: version == 3 ? BigEndian(frame + 4, 4) : SyncSafe(frame + 4);
		uint8_t formatFlags = version == 4 ? frame[9] : 0;
		pos += headerSize;
		if (frameSize > data.size() - pos)
			break;
		if (id[0] == 'T') {
			std::vector<uint8_t> body(data.begin() + pos,
				data.begin() + pos + frameSize);
			// v2.4: per-frame unsynchronisation and a data length field
			if ((formatFlags & 0x01) != 0 && body.size() >= 4)
				body.erase(body.begin(), body.begin() + 4);
			if ((formatFlags & 0x02) != 0)
				Resync(body);
			bool compressed = (formatFlags & 0x08) != 0
				|| (version == 3 && (frame[9] & 0x80) != 0);
			if (!compressed) {
				std::string text = ID3Text(body.data(), body.size());
				if (id == "TIT2" || id == "TT2")
					SetIfEmpty(tags.title, text);
				else if (id == "TPE1" || id == "TP1")
					SetIfEmpty(tags.artist, text);
				else if (id == "TALB" || id == "TAL")
					SetIfEmpty(tags.album, text);
				else if ((id == "TRCK" || id == "TRK") && tags.track == 0)
					tags.track = atoi(text.c_str());
				else if ((id == "TLEN" || id == "TLE") && tags.duration == 0)
					tags.duration = atof(text.c_str()) / 1000.0;
				found = true;
			}
		}
		pos += frameSize;
	}
	return total;
}


static bool
ReadID3v1(const TagSource& read, uint64_t fileSize, Tags& tags)
{
	if (fileSize < 128)
		return false;
	uint8_t tag[128];
	if (read(fileSize - 128, tag, 128) != 128 || memcmp(tag, "TAG", 3) != 0)
		return false;
	SetIfEmpty(tags.title, Trim(Latin1(tag + 3, 30)));
	SetIfEmpty(tags.artist, Trim(Latin1(tag + 33, 30)));
	SetIfEmpty(tags.album, Trim(Latin1(tag + 63, 30)));
	// ID3v1.1: the comment's last byte is the track
	if (tags.track == 0 && tag[125] == 0 && tag[126] != 0)
		tags.track = tag[126];
	return true;
}


// A Vorbis comment block (FLAC's, Vorbis's and Opus's): a vendor string,
// then KEY=value pairs.
static bool
ReadVorbisComment(const uint8_t* p, size_t size, Tags& tags)
{
	if (size < 8)
		return false;
	size_t pos = 4 + LittleEndian32(p);
	if (pos + 4 > size)
		return false;
	uint32_t count = LittleEndian32(p + pos);
	pos += 4;
	for (uint32_t i = 0; i < count && pos + 4 <= size; i++) {
		uint32_t length = LittleEndian32(p + pos);
		pos += 4;
		if (length > size - pos)
			break;
		std::string comment((const char*)p + pos, length);
		pos += length;
		size_t equals = comment.find('=');
		if (equals == std::string::npos)
			continue;
		std::string key = comment.substr(0, equals);
		std::string value = Trim(comment.substr(equals + 1));
		if (strcasecmp(key.c_str(), "TITLE") == 0)
			SetIfEmpty(tags.title, value);
		else if (strcasecmp(key.c_str(), "ARTIST") == 0)
			SetIfEmpty(tags.artist, value);
		else if (strcasecmp(key.c_str(), "ALBUM") == 0)
			SetIfEmpty(tags.album, value);
		else if (strcasecmp(key.c_str(), "TRACKNUMBER") == 0
			&& tags.track == 0)
			tags.track = atoi(value.c_str());
	}
	return true;
}


static bool
ReadFLAC(const TagSource& read, uint64_t offset, Tags& tags)
{
	uint8_t magic[4];
	if (read(offset, magic, 4) != 4 || memcmp(magic, "fLaC", 4) != 0)
		return false;
	offset += 4;
	bool found = false;
	for (int blocks = 0; blocks < 64; blocks++) {
		uint8_t header[4];
		if (read(offset, header, 4) != 4)
			break;
		int type = header[0] & 0x7f;
		uint32_t size = BigEndian(header + 1, 3);
		offset += 4;
		if (type == 0 && size >= 18) {
			// STREAMINFO: 20 bits of rate and 36 of samples, from byte 10
			uint8_t info[18];
			if (read(offset, info, 18) == 18) {
				uint32_t rate = info[10] << 12 | info[11] << 4 | info[12] >> 4;
				uint64_t samples = (uint64_t)(info[13] & 0x0f) << 32
					| BigEndian(info + 14, 4);
				if (rate > 0 && samples > 0)
					tags.duration = double(samples) / rate;
			}
		} else if (type == 4 && size <= kMaxTagRead) {
			std::vector<uint8_t> block(size);
			if (read(offset, block.data(), size) == size)
				found = ReadVorbisComment(block.data(), size, tags) || found;
		}
		offset += size;
		if ((header[0] & 0x80) != 0)
			break;
	}
	return found || tags.duration > 0;
}


// The granule position of the last Ogg page: where the stream ends, in
// samples. -1 if there's none.
static int64_t
LastGranule(const TagSource& read, uint64_t fileSize)
{
	// A page is at most 64 KiB
	uint64_t size = fileSize < 70000 ? fileSize : 70000;
	std::vector<uint8_t> tail(size);
	if (read(fileSize - size, tail.data(), size) != size || size < 14)
		return -1;
	for (size_t i = size - 14 + 1; i-- > 0;) {
		if (memcmp(tail.data() + i, "OggS", 4) != 0)
			continue;
		uint64_t granule = 0;
		for (int b = 7; b >= 0; b--)
			granule = granule << 8 | tail[i + 6 + b];
		if (granule != ~(uint64_t)0)
			return (int64_t)granule;
	}
	return -1;
}


// Ogg: the first packet of the first stream says what it is (and its
// sample rate), the second holds the comments ("\x03vorbis" or "OpusTags"
// first), maybe across several pages; the last page's position gives the
// length.
static bool
ReadOgg(const TagSource& read, uint64_t fileSize, Tags& tags)
{
	std::vector<uint8_t> packets[2];
	int packetIndex = 0;
	uint64_t offset = 0;
	for (int pages = 0; pages < 64 && packetIndex < 2; pages++) {
		uint8_t header[27];
		if (read(offset, header, 27) != 27 || memcmp(header, "OggS", 4) != 0)
			return false;
		int segments = header[26];
		uint8_t table[255];
		if (read(offset + 27, table, segments) != (size_t)segments)
			return false;
		uint64_t data = offset + 27 + segments;
		for (int i = 0; i < segments && packetIndex < 2; i++) {
			std::vector<uint8_t>& packet = packets[packetIndex];
			if (packet.size() + table[i] > kMaxTagRead)
				return false;
			size_t at = packet.size();
			packet.resize(at + table[i]);
			if (read(data, packet.data() + at, table[i]) != table[i])
				return false;
			data += table[i];
			// A segment under 255 bytes ends its packet
			if (table[i] < 255)
				packetIndex++;
		}
		offset = data;
	}
	if (packetIndex < 2)
		return false;

	const std::vector<uint8_t>& head = packets[0];
	const std::vector<uint8_t>& comments = packets[1];
	uint32_t rate = 0;
	uint32_t preSkip = 0;
	bool found = false;
	if (head.size() >= 16 && memcmp(head.data(), "\x01vorbis", 7) == 0)
		rate = LittleEndian32(head.data() + 12);
	else if (head.size() >= 12 && memcmp(head.data(), "OpusHead", 8) == 0) {
		// Opus always counts at 48 kHz, after a pre-skip
		rate = 48000;
		preSkip = head[10] | head[11] << 8;
	}
	if (comments.size() > 7
		&& memcmp(comments.data(), "\x03vorbis", 7) == 0)
		found = ReadVorbisComment(comments.data() + 7, comments.size() - 7,
			tags);
	else if (comments.size() > 8
		&& memcmp(comments.data(), "OpusTags", 8) == 0)
		found = ReadVorbisComment(comments.data() + 8, comments.size() - 8,
			tags);

	int64_t granule = LastGranule(read, fileSize);
	if (rate > 0 && granule > (int64_t)preSkip)
		tags.duration = double(granule - preSkip) / rate;
	return found || tags.duration > 0;
}


// WAV: the data chunk's size over the format's bytes per second.
static bool
ReadWAV(const TagSource& read, uint64_t fileSize, Tags& tags)
{
	uint8_t header[12];
	if (read(0, header, 12) != 12 || memcmp(header, "RIFF", 4) != 0
		|| memcmp(header + 8, "WAVE", 4) != 0)
		return false;
	uint64_t offset = 12;
	uint32_t byteRate = 0;
	for (int chunks = 0; chunks < 64 && offset + 8 <= fileSize; chunks++) {
		// The chunk header, then the format's first 12 bytes: the format,
		// channels, sample rate and bytes per second
		uint8_t chunk[20];
		if (read(offset, chunk, 8) != 8)
			break;
		uint32_t size = LittleEndian32(chunk + 4);
		if (memcmp(chunk, "fmt ", 4) == 0 && size >= 12
			&& read(offset + 8, chunk + 8, 12) == 12)
			byteRate = LittleEndian32(chunk + 16);
		else if (memcmp(chunk, "data", 4) == 0) {
			// Streams write 0 or ~0 and leave it at that
			uint64_t data = fileSize - (offset + 8);
			if (size != 0 && size != 0xffffffff && size < data)
				data = size;
			if (byteRate > 0)
				tags.duration = double(data) / byteRate;
			return true;
		}
		offset += 8 + size + (size & 1);
	}
	return true;
}


struct MPEGFrame {
	int		version;	// 1, 2 or 25 (2.5)
	int		layer;
	int		bitrate;	// kbit/s
	int		rate;
	int		samples;	// per frame
	int		length;		// bytes
	bool	mono;
};


static bool
ParseMPEGHeader(const uint8_t* p, MPEGFrame& frame)
{
	if (p[0] != 0xff || (p[1] & 0xe0) != 0xe0)
		return false;
	int versionBits = (p[1] >> 3) & 3;
	int layerBits = (p[1] >> 1) & 3;
	int bitrateIndex = p[2] >> 4;
	int rateIndex = (p[2] >> 2) & 3;
	if (versionBits == 1 || layerBits == 0 || bitrateIndex == 0
		|| bitrateIndex == 15 || rateIndex == 3)
		return false;
	static const int kBitrates[5][15] = {
		{0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448},
		{0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384},
		{0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320},
		{0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256},
		{0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160}
	};
	static const int kRates[3] = {44100, 48000, 32000};
	frame.version = versionBits == 3 ? 1 : versionBits == 2 ? 2 : 25;
	frame.layer = 4 - layerBits;
	int table = frame.version == 1 ? frame.layer - 1
		: frame.layer == 1 ? 3 : 4;
	frame.bitrate = kBitrates[table][bitrateIndex];
	frame.rate = kRates[rateIndex] / (frame.version == 1 ? 1
		: frame.version == 2 ? 2 : 4);
	frame.samples = frame.layer == 1 ? 384
		: frame.layer == 2 || frame.version == 1 ? 1152 : 576;
	int padding = (p[2] >> 1) & 1;
	frame.length = frame.layer == 1
		? (12 * frame.bitrate * 1000 / frame.rate + padding) * 4
		: frame.samples / 8 * frame.bitrate * 1000 / frame.rate + padding;
	frame.mono = (p[3] >> 6) == 3;
	return frame.length > 4;
}


// MP3 (and MP2): the frame count from a Xing, Info or VBRI header, or the
// size over the bit rate when there's none (constant bit rate).
static bool
ReadMPEG(const TagSource& read, uint64_t start, uint64_t fileSize,
	bool hasID3v1, Tags& tags)
{
	std::vector<uint8_t> data(64 * 1024);
	data.resize(read(start, data.data(), data.size()));
	for (size_t i = 0; i + 4 <= data.size(); i++) {
		MPEGFrame frame;
		if (!ParseMPEGHeader(data.data() + i, frame))
			continue;
		// A second frame where the first ends, or it's chance
		MPEGFrame next;
		size_t after = i + frame.length;
		if (after + 4 <= data.size()
			&& !ParseMPEGHeader(data.data() + after, next))
			continue;

		uint64_t end = fileSize - (hasID3v1 ? 128 : 0);
		uint64_t audioStart = start + i;
		if (end <= audioStart)
			return false;
		size_t side = frame.version == 1 ? (frame.mono ? 17 : 32)
			: (frame.mono ? 9 : 17);
		const uint8_t* xing = data.data() + i + 4 + side;
		const uint8_t* vbri = data.data() + i + 36;
		uint32_t frames = 0;
		if (i + 4 + side + 12 <= data.size()
			&& (memcmp(xing, "Xing", 4) == 0 || memcmp(xing, "Info", 4) == 0)
			&& (BigEndian(xing + 4, 4) & 1) != 0)
			frames = BigEndian(xing + 8, 4);
		else if (i + 36 + 18 <= data.size() && memcmp(vbri, "VBRI", 4) == 0)
			frames = BigEndian(vbri + 14, 4);
		if (frames > 0)
			tags.duration = double(frames) * frame.samples / frame.rate;
		else
			tags.duration = double(end - audioStart) * 8
				/ (frame.bitrate * 1000.0);
		return true;
	}
	return false;
}


bool
ReadTags(const TagSource& read, uint64_t fileSize, Tags& tags)
{
	bool found = false;
	uint32_t id3Size = ReadID3v2(read, 0, tags, found);
	// ID3v1 only fills in what the other tags leave out
	Tags old;
	bool hasID3v1 = ReadID3v1(read, fileSize, old);
	if (ReadFLAC(read, id3Size, tags))
		found = true;
	else if (id3Size == 0 && ReadOgg(read, fileSize, tags))
		found = true;
	else if (id3Size == 0 && ReadWAV(read, fileSize, tags)) {
	} else if (tags.duration <= 0)
		ReadMPEG(read, id3Size, fileSize, hasID3v1, tags);
	if (hasID3v1) {
		SetIfEmpty(tags.title, old.title);
		SetIfEmpty(tags.artist, old.artist);
		SetIfEmpty(tags.album, old.album);
		if (tags.track == 0)
			tags.track = old.track;
		found = true;
	}
	return found;
}


std::string
TitleFromFileName(const std::string& path)
{
	size_t slash = path.rfind('/');
	std::string name = slash == std::string::npos ? path
		: path.substr(slash + 1);
	size_t dot = name.rfind('.');
	if (dot != std::string::npos && dot > 0)
		name.erase(dot);
	return name;
}

}	// namespace audio

/*
 * PowerPlay — host tests for the tag reader.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#include "AudioTags.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int sFailures = 0;

#define CHECK(cond, ...) do { \
	if (!(cond)) { \
		std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
		std::printf(__VA_ARGS__); \
		std::printf("\n"); \
		sFailures++; \
	} \
} while (0)

typedef std::vector<uint8_t> Bytes;


static void
Append(Bytes& out, const std::string& text)
{
	out.insert(out.end(), text.begin(), text.end());
}


static void
BigEndian(Bytes& out, uint32_t value, int bytes)
{
	for (int i = bytes - 1; i >= 0; i--)
		out.push_back(uint8_t(value >> (8 * i)));
}


static void
LittleEndian32(Bytes& out, uint32_t value)
{
	for (int i = 0; i < 4; i++)
		out.push_back(uint8_t(value >> (8 * i)));
}


static void
SyncSafe(Bytes& out, uint32_t value)
{
	for (int i = 3; i >= 0; i--)
		out.push_back(uint8_t((value >> (7 * i)) & 0x7f));
}


static audio::Tags
Read(const Bytes& file, bool* found = NULL)
{
	audio::Tags tags;
	bool result = audio::ReadTags(
		[&](uint64_t offset, void* buffer, size_t size) -> size_t {
			if (offset >= file.size())
				return 0;
			size_t n = std::min(size, size_t(file.size() - offset));
			memcpy(buffer, file.data() + offset, n);
			return n;
		}, file.size(), tags);
	if (found != NULL)
		*found = result;
	return tags;
}


static Bytes
ID3v2(int version, const std::vector<std::pair<std::string, Bytes>>& frames)
{
	Bytes body;
	for (const auto& frame : frames) {
		Append(body, frame.first);
		if (version == 2)
			BigEndian(body, frame.second.size(), 3);
		else if (version == 3)
			BigEndian(body, frame.second.size(), 4);
		else
			SyncSafe(body, frame.second.size());
		if (version > 2) {
			body.push_back(0);
			body.push_back(0);
		}
		body.insert(body.end(), frame.second.begin(), frame.second.end());
	}
	// Padding
	body.resize(body.size() + 32, 0);
	Bytes tag;
	Append(tag, "ID3");
	tag.push_back(uint8_t(version));
	tag.push_back(0);
	tag.push_back(0);
	SyncSafe(tag, body.size());
	tag.insert(tag.end(), body.begin(), body.end());
	return tag;
}


static Bytes
Text(int encoding, const std::string& text)
{
	Bytes out;
	out.push_back(uint8_t(encoding));
	Append(out, text);
	return out;
}


static void
TestID3v23()
{
	// Latin-1 "Café", and UTF-16 with a BOM
	Bytes utf16 = {1, 0xff, 0xfe};
	for (char c : std::string("Band"))
		utf16.insert(utf16.end(), {uint8_t(c), 0});
	utf16.insert(utf16.end(), {0x34, 0xd8, 0x1e, 0xdd});	// U+1D11E
	utf16.insert(utf16.end(), {0, 0});
	Bytes file = ID3v2(3, {
		{"TIT2", Text(0, "Caf\xe9")},
		{"TPE1", utf16},
		{"TALB", Text(0, "Album  ")},
		{"TRCK", Text(0, "7/12")},
		{"APIC", Bytes(100, 0x55)},
	});
	file.resize(file.size() + 1000, 0xff);	// "audio"
	bool found;
	audio::Tags tags = Read(file, &found);
	CHECK(found, "ID3v2.3 found");
	CHECK(tags.title == "Caf\xc3\xa9", "title %s", tags.title.c_str());
	CHECK(tags.artist == "Band\xf0\x9d\x84\x9e", "artist %s",
		tags.artist.c_str());
	CHECK(tags.album == "Album", "album '%s'", tags.album.c_str());
	CHECK(tags.track == 7, "track %d", tags.track);
}


static void
TestID3v24AndV22()
{
	Bytes title = Text(3, std::string(200, 'x'));	// over 127: syncsafe
	Bytes file = ID3v2(4, {{"TIT2", title}, {"TPE1", Text(3, std::string("A\0B", 3))},
		{"TLEN", Text(3, "185000")}});
	audio::Tags tags = Read(file);
	CHECK(tags.title == std::string(200, 'x'), "v2.4 title length %zu",
		tags.title.size());
	CHECK(tags.artist == "A", "v2.4 first value: %s", tags.artist.c_str());
	CHECK(std::fabs(tags.duration - 185) < 0.01, "TLEN %g", tags.duration);

	file = ID3v2(2, {{"TT2", Text(0, "Old")}, {"TP1", Text(0, "Timer")}});
	tags = Read(file);
	CHECK(tags.title == "Old" && tags.artist == "Timer", "v2.2 %s / %s",
		tags.title.c_str(), tags.artist.c_str());
}


static void
TestID3v1()
{
	Bytes file(500, 0xff);
	Bytes tag(128, 0);
	memcpy(tag.data(), "TAG", 3);
	memcpy(tag.data() + 3, "Short Title", 11);
	memcpy(tag.data() + 33, "Artist Name                   ", 30);
	memcpy(tag.data() + 63, "Record", 6);
	tag[126] = 4;
	file.insert(file.end(), tag.begin(), tag.end());
	audio::Tags tags = Read(file);
	CHECK(tags.title == "Short Title", "v1 title %s", tags.title.c_str());
	CHECK(tags.artist == "Artist Name", "v1 artist '%s'", tags.artist.c_str());
	CHECK(tags.album == "Record", "v1 album %s", tags.album.c_str());
	CHECK(tags.track == 4, "v1 track %d", tags.track);

	// ID3v2 wins over v1
	Bytes both = ID3v2(3, {{"TIT2", Text(0, "Long Title")}});
	both.insert(both.end(), file.begin(), file.end());
	tags = Read(both);
	CHECK(tags.title == "Long Title", "v2 first: %s", tags.title.c_str());
	CHECK(tags.artist == "Artist Name", "v1 fills in: %s",
		tags.artist.c_str());
}


static Bytes
VorbisComment(const std::vector<std::string>& comments)
{
	Bytes out;
	std::string vendor = "test";
	LittleEndian32(out, vendor.size());
	Append(out, vendor);
	LittleEndian32(out, comments.size());
	for (const std::string& comment : comments) {
		LittleEndian32(out, comment.size());
		Append(out, comment);
	}
	return out;
}


static void
TestFLAC()
{
	Bytes file;
	Append(file, "fLaC");
	// STREAMINFO: 44100 Hz, 441000 samples = 10 s
	Bytes info(18, 0);
	uint32_t rate = 44100;
	uint64_t samples = 441000;
	info[10] = uint8_t(rate >> 12);
	info[11] = uint8_t(rate >> 4);
	info[12] = uint8_t((rate & 0xf) << 4 | (1 << 1));
	info[13] = uint8_t((15 << 4) | ((samples >> 32) & 0xf));
	info[14] = uint8_t(samples >> 24);
	info[15] = uint8_t(samples >> 16);
	info[16] = uint8_t(samples >> 8);
	info[17] = uint8_t(samples);
	file.push_back(0);
	BigEndian(file, info.size(), 3);
	file.insert(file.end(), info.begin(), info.end());
	Bytes comment = VorbisComment({"title=Flac Song", "ARTIST=Lossless",
		"Album=Big Box", "TRACKNUMBER=3"});
	file.push_back(0x80 | 4);
	BigEndian(file, comment.size(), 3);
	file.insert(file.end(), comment.begin(), comment.end());
	file.resize(file.size() + 300, 0);

	audio::Tags tags = Read(file);
	CHECK(tags.title == "Flac Song", "flac title %s", tags.title.c_str());
	CHECK(tags.artist == "Lossless", "flac artist %s", tags.artist.c_str());
	CHECK(tags.album == "Big Box", "flac album %s", tags.album.c_str());
	CHECK(tags.track == 3, "flac track %d", tags.track);
	CHECK(std::fabs(tags.duration - 10) < 1e-6, "flac length %g",
		tags.duration);
}


// One Ogg page with the given packets (each under 255 * 255 bytes).
static void
OggPage(Bytes& out, const std::vector<Bytes>& packets, int64_t granule = 0)
{
	Bytes table, data;
	for (const Bytes& packet : packets) {
		size_t left = packet.size();
		while (left >= 255) {
			table.push_back(255);
			left -= 255;
		}
		table.push_back(uint8_t(left));
		data.insert(data.end(), packet.begin(), packet.end());
	}
	Append(out, "OggS");
	out.push_back(0);
	out.push_back(0);
	for (int i = 0; i < 8; i++)
		out.push_back(uint8_t(uint64_t(granule) >> (8 * i)));
	out.resize(out.size() + 12, 0);
	out.push_back(uint8_t(table.size()));
	out.insert(out.end(), table.begin(), table.end());
	out.insert(out.end(), data.begin(), data.end());
}


static void
TestOgg()
{
	Bytes identification;
	Append(identification, "\x01vorbis");
	identification.resize(30, 0);
	identification[12] = 0x44;	// 44100 Hz
	identification[13] = 0xac;
	Bytes comments;
	Append(comments, "\x03vorbis");
	// Long enough to need several lacing segments
	Bytes body = VorbisComment({"TITLE=" + std::string(600, 'v'),
		"ARTIST=Ogg"});
	comments.insert(comments.end(), body.begin(), body.end());
	Bytes file;
	OggPage(file, {identification});
	OggPage(file, {comments, Bytes(40, 1)});
	OggPage(file, {Bytes(3000, 2)}, 44100 * 5);
	OggPage(file, {Bytes(3000, 2)}, 44100 * 12);
	audio::Tags tags = Read(file);
	CHECK(std::fabs(tags.duration - 12) < 1e-6, "ogg length %g",
		tags.duration);
	CHECK(tags.title == std::string(600, 'v'), "ogg title length %zu",
		tags.title.size());
	CHECK(tags.artist == "Ogg", "ogg artist %s", tags.artist.c_str());

	Bytes opus;
	Bytes head;
	Append(head, "OpusHead");
	head.resize(19, 0);
	head[10] = 0x38;	// a pre-skip of 312
	head[11] = 0x01;
	Bytes opusTags;
	Append(opusTags, "OpusTags");
	body = VorbisComment({"title=Opus Song"});
	opusTags.insert(opusTags.end(), body.begin(), body.end());
	OggPage(opus, {head});
	OggPage(opus, {opusTags});
	OggPage(opus, {Bytes(100, 3)}, 48000 * 3 + 312);
	tags = Read(opus);
	CHECK(tags.title == "Opus Song", "opus title %s", tags.title.c_str());
	CHECK(std::fabs(tags.duration - 3) < 1e-6, "opus length %g",
		tags.duration);
}


// MPEG-1 layer III frames at 128 kbit/s, 44.1 kHz: 417 bytes each.
static Bytes
MP3Frames(int count, bool xing, uint32_t xingFrames)
{
	Bytes file;
	for (int i = 0; i < count; i++) {
		Bytes frame(417, 0);
		frame[0] = 0xff;
		frame[1] = 0xfb;
		frame[2] = 0x90;
		frame[3] = 0x00;	// stereo
		if (i == 0 && xing) {
			memcpy(frame.data() + 36, "Xing", 4);
			frame[43] = 1;	// frame count present
			frame[44] = uint8_t(xingFrames >> 24);
			frame[45] = uint8_t(xingFrames >> 16);
			frame[46] = uint8_t(xingFrames >> 8);
			frame[47] = uint8_t(xingFrames);
		}
		file.insert(file.end(), frame.begin(), frame.end());
	}
	return file;
}


static void
TestLengths()
{
	// Constant bit rate: from the size
	Bytes cbr = ID3v2(3, {{"TIT2", Text(0, "CBR")}});
	Bytes frames = MP3Frames(200, false, 0);
	cbr.insert(cbr.end(), frames.begin(), frames.end());
	audio::Tags tags = Read(cbr);
	double expected = 200 * 417 * 8 / 128000.0;
	CHECK(std::fabs(tags.duration - expected) < 0.001, "cbr mp3 %g, not %g",
		tags.duration, expected);

	// Variable: the Xing header's count
	Bytes vbr = MP3Frames(50, true, 1000);
	tags = Read(vbr);
	expected = 1000 * 1152 / 44100.0;
	CHECK(std::fabs(tags.duration - expected) < 0.001, "xing mp3 %g, not %g",
		tags.duration, expected);

	// Not MPEG at all: no length
	tags = Read(Bytes(5000, 0x11));
	CHECK(tags.duration == 0, "noise has a length %g", tags.duration);

	// WAV: data size over bytes per second
	Bytes wav;
	Append(wav, "RIFF");
	LittleEndian32(wav, 0);
	Append(wav, "WAVEfmt ");
	LittleEndian32(wav, 16);
	wav.insert(wav.end(), {1, 0, 2, 0});
	LittleEndian32(wav, 44100);
	LittleEndian32(wav, 176400);
	wav.insert(wav.end(), {4, 0, 16, 0});
	Append(wav, "data");
	LittleEndian32(wav, 352800);
	wav.resize(wav.size() + 352800, 0);
	tags = Read(wav);
	CHECK(std::fabs(tags.duration - 2) < 1e-9, "wav %g", tags.duration);
}


static void
TestNothing()
{
	Bytes file(4000, 0x42);
	bool found = true;
	audio::Tags tags = Read(file, &found);
	CHECK(!found && tags.title.empty(), "no tags");
	// Truncated and corrupt tags don't crash or hang
	Bytes broken = ID3v2(3, {{"TIT2", Text(0, "x")}});
	broken[13] = 0x7f;	// a frame size past the end
	Read(broken);
	broken.resize(12);
	Read(broken);
	Bytes flac;
	Append(flac, "fLaC");
	flac.insert(flac.end(), {0x84, 0xff, 0xff, 0xff});
	Read(flac);

	CHECK(audio::TitleFromFileName("/music/A Song.mp3") == "A Song",
		"file name title");
	CHECK(audio::TitleFromFileName("noext") == "noext", "no extension");
	CHECK(audio::TitleFromFileName("/x/.hidden") == ".hidden", "dot file");
}


int
main()
{
	TestID3v23();
	TestID3v24AndV22();
	TestID3v1();
	TestFLAC();
	TestOgg();
	TestLengths();
	TestNothing();
	if (sFailures == 0)
		std::printf("test_tags: all passed\n");
	return sFailures == 0 ? 0 : 1;
}
